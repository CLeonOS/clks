#include <clks/net.h>
#include <clks/boot.h>
#include <clks/cpu.h>
#include <clks/log.h>
#include <clks/pmm.h>
#include <clks/string.h>
#include <clks/types.h>

/*
 * Networking backend migrated from ToaruOS e1000 driver (NCSA license):
 * - modules/e1000.c
 * - base/usr/include/kernel/net/e1000.h
 * Local copy is stored in clks/third_party/toaru/.
 */

#if defined(CLKS_ARCH_X86_64)

#include "../../third_party/toaru/e1000_dev.h"

#define CLKS_NET_PCI_CFG_ADDR_PORT 0xCF8U
#define CLKS_NET_PCI_CFG_DATA_PORT 0xCFCU
#define CLKS_NET_E1000_VENDOR 0x8086U

#define CLKS_NET_REG_CTRL 0x0000U
#define CLKS_NET_REG_STATUS 0x0008U
#define CLKS_NET_REG_EERD 0x0014U
#define CLKS_NET_REG_ICR 0x00C0U
#define CLKS_NET_REG_IMC 0x00D8U
#define CLKS_NET_REG_TIPG 0x0410U
#define CLKS_NET_REG_MTA 0x5200U

#define CLKS_NET_STATUS_LINK_UP (1U << 1U)
#define CLKS_NET_CTRL_RST (1U << 26U)
#define CLKS_NET_CTRL_SLU (1U << 6U)
#define CLKS_NET_CTRL_PHY_RST (1U << 31U)
#define CLKS_NET_CTRL_LRST (1U << 3U)

#define CLKS_NET_PTE_PRESENT (1ULL << 0U)
#define CLKS_NET_PTE_WRITE (1ULL << 1U)
#define CLKS_NET_PTE_PWT (1ULL << 3U)
#define CLKS_NET_PTE_PCD (1ULL << 4U)
#define CLKS_NET_PTE_PS (1ULL << 7U)
#define CLKS_NET_PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define CLKS_NET_DESC_COUNT 128U
#define CLKS_NET_BUF_SIZE 2048U
#define CLKS_NET_MMIO_MAP_SIZE 0x20000ULL
#define CLKS_NET_TX_WAIT_LOOPS 200000U
#define CLKS_NET_POLL_SPIN_PAUSE_MASK 0x3FFU

#define CLKS_NET_ETH_ADDR_LEN 6U
#define CLKS_NET_ETHERTYPE_ARP 0x0806U
#define CLKS_NET_ETHERTYPE_IPV4 0x0800U

#define CLKS_NET_ARP_HTYPE_ETHERNET 1U
#define CLKS_NET_ARP_OP_REQUEST 1U
#define CLKS_NET_ARP_OP_REPLY 2U

#define CLKS_NET_IPV4_PROTO_ICMP 1U
#define CLKS_NET_IPV4_PROTO_UDP 17U

#define CLKS_NET_DEFAULT_IP_BE 0U
#define CLKS_NET_DEFAULT_NETMASK_BE 0U
#define CLKS_NET_DEFAULT_GATEWAY_BE 0U
#define CLKS_NET_DEFAULT_DNS_BE 0U

#define CLKS_NET_FALLBACK_IP_BE 0x0A00020FU
#define CLKS_NET_FALLBACK_NETMASK_BE 0xFFFFFF00U
#define CLKS_NET_FALLBACK_GATEWAY_BE 0x0A000202U
#define CLKS_NET_CLOUDFLARE_DNS_PRIMARY_BE 0x01010101U
#define CLKS_NET_CLOUDFLARE_DNS_SECONDARY_BE 0x01000001U
#define CLKS_NET_FALLBACK_DNS_BE CLKS_NET_CLOUDFLARE_DNS_PRIMARY_BE

#define CLKS_NET_DHCP_SERVER_PORT 67U
#define CLKS_NET_DHCP_CLIENT_PORT 68U
#define CLKS_NET_DHCP_MAGIC 0x63825363U
#define CLKS_NET_DHCP_MSG_DISCOVER 1U
#define CLKS_NET_DHCP_MSG_OFFER 2U
#define CLKS_NET_DHCP_MSG_REQUEST 3U
#define CLKS_NET_DHCP_MSG_ACK 5U
#define CLKS_NET_DHCP_MSG_NAK 6U
#define CLKS_NET_DHCP_OPT_SUBNET_MASK 1U
#define CLKS_NET_DHCP_OPT_ROUTER 3U
#define CLKS_NET_DHCP_OPT_DNS 6U
#define CLKS_NET_DHCP_OPT_HOSTNAME 12U
#define CLKS_NET_DHCP_OPT_REQ_IP 50U
#define CLKS_NET_DHCP_OPT_MSG_TYPE 53U
#define CLKS_NET_DHCP_OPT_SERVER_ID 54U
#define CLKS_NET_DHCP_OPT_PARAM_REQ 55U
#define CLKS_NET_DHCP_OPT_CLIENT_ID 61U
#define CLKS_NET_DHCP_OPT_END 255U
#define CLKS_NET_DHCP_FLAG_BROADCAST 0x8000U
#define CLKS_NET_DHCP_DISCOVER_POLL_LOOPS 300000ULL
#define CLKS_NET_DHCP_ACK_POLL_LOOPS 300000ULL

#define CLKS_NET_ARP_TABLE_CAP 16U
#define CLKS_NET_UDP_QUEUE_CAP 16U
#define CLKS_NET_UDP_PAYLOAD_MAX 1472U

#define CLKS_NET_IP_HEADER_MIN_LEN 20U
#define CLKS_NET_ICMP_HEADER_LEN 8U
#define CLKS_NET_UDP_HEADER_LEN 8U
#define CLKS_NET_ETH_HEADER_LEN 14U
#define CLKS_NET_MAX_IPV4_PAYLOAD 1480U
#define CLKS_NET_MAX_FRAME_NO_FCS 1514U

enum clks_net_reg_mode {
    CLKS_NET_REG_NONE = 0,
    CLKS_NET_REG_MMIO = 1,
    CLKS_NET_REG_IO = 2,
};

struct clks_net_pci_location {
    u8 bus;
    u8 dev;
    u8 func;
};

struct clks_net_eth_hdr {
    u8 dst[6];
    u8 src[6];
    u8 ethertype[2];
} __attribute__((packed));

struct clks_net_arp_packet {
    u8 htype[2];
    u8 ptype[2];
    u8 hlen;
    u8 plen;
    u8 oper[2];
    u8 sha[6];
    u8 spa[4];
    u8 tha[6];
    u8 tpa[4];
} __attribute__((packed));

struct clks_net_ipv4_hdr {
    u8 ver_ihl;
    u8 dscp_ecn;
    u8 total_len[2];
    u8 ident[2];
    u8 flags_frag[2];
    u8 ttl;
    u8 proto;
    u8 checksum[2];
    u8 src[4];
    u8 dst[4];
} __attribute__((packed));

struct clks_net_icmp_echo_hdr {
    u8 type;
    u8 code;
    u8 checksum[2];
    u8 ident[2];
    u8 seq[2];
} __attribute__((packed));

struct clks_net_udp_hdr {
    u8 src_port[2];
    u8 dst_port[2];
    u8 len[2];
    u8 checksum[2];
} __attribute__((packed));

struct clks_net_arp_entry {
    clks_bool valid;
    u32 ipv4_be;
    u8 mac[6];
    u64 stamp;
};

struct clks_net_udp_packet {
    clks_bool valid;
    u32 src_ipv4_be;
    u16 src_port;
    u16 dst_port;
    u16 payload_len;
    u8 payload[CLKS_NET_UDP_PAYLOAD_MAX];
};

static clks_bool clks_net_ready = CLKS_FALSE;
static clks_bool clks_net_hw_ready = CLKS_FALSE;

static u32 clks_net_ipv4_be = CLKS_NET_DEFAULT_IP_BE;
static u32 clks_net_netmask_be = CLKS_NET_DEFAULT_NETMASK_BE;
static u32 clks_net_gateway_be = CLKS_NET_DEFAULT_GATEWAY_BE;
static u32 clks_net_dns_be = CLKS_NET_DEFAULT_DNS_BE;

static u8 clks_net_mac[CLKS_NET_ETH_ADDR_LEN];
static const u8 clks_net_mac_fallback[CLKS_NET_ETH_ADDR_LEN] = {0x02U, 0x43U, 0x4CU, 0x4BU, 0x53U, 0x01U};
static const u8 clks_net_mac_broadcast[CLKS_NET_ETH_ADDR_LEN] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};

static enum clks_net_reg_mode clks_net_reg_mode = CLKS_NET_REG_NONE;
static u8 *clks_net_mmio_base = CLKS_NULL;
static u16 clks_net_io_base = 0U;

static volatile struct e1000_rx_desc *clks_net_rx_descs = CLKS_NULL;
static volatile struct e1000_tx_desc *clks_net_tx_descs = CLKS_NULL;
static u64 clks_net_rx_descs_phys = 0ULL;
static u64 clks_net_tx_descs_phys = 0ULL;
static u8 *clks_net_rx_buf_virt[CLKS_NET_DESC_COUNT];
static u64 clks_net_rx_buf_phys[CLKS_NET_DESC_COUNT];
static u8 *clks_net_tx_buf_virt[CLKS_NET_DESC_COUNT];
static u64 clks_net_tx_buf_phys[CLKS_NET_DESC_COUNT];
static u32 clks_net_rx_index = 0U;
static u32 clks_net_tx_index = 0U;

static struct clks_net_arp_entry clks_net_arp_table[CLKS_NET_ARP_TABLE_CAP];
static u64 clks_net_arp_stamp = 1ULL;

static struct clks_net_udp_packet clks_net_udp_queue[CLKS_NET_UDP_QUEUE_CAP];
static u32 clks_net_udp_head = 0U;
static u32 clks_net_udp_tail = 0U;
static u32 clks_net_udp_count = 0U;

static u16 clks_net_ipv4_ident = 1U;
static u16 clks_net_ping_ident = 0x434CU;
static u16 clks_net_ping_seq = 0U;
static clks_bool clks_net_ping_waiting = CLKS_FALSE;
static clks_bool clks_net_ping_reply_ok = CLKS_FALSE;
static u32 clks_net_ping_wait_dst_be = 0U;
static u16 clks_net_ping_wait_seq = 0U;

static u32 clks_net_dhcp_xid_seed = 0x434C4B53U;
static clks_bool clks_net_dhcp_active = CLKS_FALSE;
static clks_bool clks_net_dhcp_offer_ready = CLKS_FALSE;
static clks_bool clks_net_dhcp_ack_ready = CLKS_FALSE;
static clks_bool clks_net_dhcp_failed = CLKS_FALSE;
static u32 clks_net_dhcp_xid = 0U;
static u32 clks_net_dhcp_offer_ip_be = 0U;
static u32 clks_net_dhcp_server_id_be = 0U;
static u32 clks_net_dhcp_offer_netmask_be = 0U;
static u32 clks_net_dhcp_offer_gateway_be = 0U;
static u32 clks_net_dhcp_offer_dns_be = 0U;

static const u16 clks_net_e1000_dev_ids[] = {
    0x1000U, 0x1001U, 0x1004U, 0x100EU, 0x100FU, 0x1010U, 0x1011U, 0x1012U, 0x1013U, 0x1015U, 0x1016U, 0x1017U,
    0x1018U, 0x101DU, 0x101EU, 0x1026U, 0x1027U, 0x1028U, 0x1075U, 0x1076U, 0x1077U, 0x10B5U, 0x10D3U, 0x10F5U,
};

static void clks_net_poll_internal(void);

static inline void clks_net_outl(u16 port, u32 value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline u32 clks_net_inl(u16 port) {
    u32 value = 0U;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline u64 clks_net_read_cr3(void) {
    u64 value = 0ULL;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void clks_net_invlpg(u64 virt_addr) {
    void *ptr = (void *)(usize)virt_addr;
    __asm__ volatile("invlpg (%0)" : : "r"(ptr) : "memory");
}

static inline u16 clks_net_read_be16(const u8 *ptr) {
    return (u16)(((u16)ptr[0] << 8U) | (u16)ptr[1]);
}

static inline void clks_net_write_be16(u8 *ptr, u16 value) {
    ptr[0] = (u8)((value >> 8U) & 0xFFU);
    ptr[1] = (u8)(value & 0xFFU);
}

static inline u32 clks_net_read_be32(const u8 *ptr) {
    return ((u32)ptr[0] << 24U) | ((u32)ptr[1] << 16U) | ((u32)ptr[2] << 8U) | (u32)ptr[3];
}

static inline void clks_net_write_be32(u8 *ptr, u32 value) {
    ptr[0] = (u8)((value >> 24U) & 0xFFU);
    ptr[1] = (u8)((value >> 16U) & 0xFFU);
    ptr[2] = (u8)((value >> 8U) & 0xFFU);
    ptr[3] = (u8)(value & 0xFFU);
}

static clks_bool clks_net_mac_equal(const u8 *left, const u8 *right) {
    u32 i;
    if (left == CLKS_NULL || right == CLKS_NULL) {
        return CLKS_FALSE;
    }
    for (i = 0U; i < CLKS_NET_ETH_ADDR_LEN; i++) {
        if (left[i] != right[i]) {
            return CLKS_FALSE;
        }
    }
    return CLKS_TRUE;
}

static clks_bool clks_net_mac_invalid(const u8 *mac) {
    u32 i;
    clks_bool all_zero = CLKS_TRUE;
    clks_bool all_ff = CLKS_TRUE;

    if (mac == CLKS_NULL) {
        return CLKS_TRUE;
    }

    for (i = 0U; i < CLKS_NET_ETH_ADDR_LEN; i++) {
        if (mac[i] != 0U) {
            all_zero = CLKS_FALSE;
        }
        if (mac[i] != 0xFFU) {
            all_ff = CLKS_FALSE;
        }
    }

    return (all_zero == CLKS_TRUE || all_ff == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;
}

static void clks_net_format_mac(const u8 *mac, char out[18]) {
    static const char hex[] = "0123456789ABCDEF";
    u32 i;
    u32 at = 0U;

    for (i = 0U; i < CLKS_NET_ETH_ADDR_LEN; i++) {
        u8 v = mac[i];
        out[at++] = hex[(v >> 4U) & 0x0FU];
        out[at++] = hex[v & 0x0FU];
        if (i + 1U < CLKS_NET_ETH_ADDR_LEN) {
            out[at++] = ':';
        }
    }
    out[at] = '\0';
}

static u16 clks_net_checksum16(const u8 *data, u64 len) {
    u32 sum = 0U;
    u64 i = 0ULL;

    if (data == CLKS_NULL) {
        return 0U;
    }

    while (i + 1ULL < len) {
        u16 word = (u16)(((u16)data[i] << 8U) | (u16)data[i + 1ULL]);
        sum += (u32)word;
        i += 2ULL;
    }

    if (i < len) {
        sum += ((u32)data[i] << 8U);
    }

    while ((sum >> 16U) != 0U) {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }

    return (u16)(~sum & 0xFFFFU);
}

static u16 clks_net_udp_checksum(u32 src_ip_be, u32 dst_ip_be, const u8 *udp, u16 udp_len) {
    u32 sum = 0U;
    u64 i = 0ULL;

    if (udp == CLKS_NULL) {
        return 0U;
    }

    sum += (src_ip_be >> 16U) & 0xFFFFU;
    sum += src_ip_be & 0xFFFFU;
    sum += (dst_ip_be >> 16U) & 0xFFFFU;
    sum += dst_ip_be & 0xFFFFU;
    sum += (u32)CLKS_NET_IPV4_PROTO_UDP;
    sum += (u32)udp_len;

    while (i + 1ULL < (u64)udp_len) {
        u16 word = (u16)(((u16)udp[i] << 8U) | (u16)udp[i + 1ULL]);
        sum += (u32)word;
        i += 2ULL;
    }

    if (i < (u64)udp_len) {
        sum += ((u32)udp[i] << 8U);
    }

    while ((sum >> 16U) != 0U) {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }

    {
        u16 out = (u16)(~sum & 0xFFFFU);
        if (out == 0U) {
            out = 0xFFFFU;
        }
        return out;
    }
}

static void clks_net_apply_fallback_config(void) {
    clks_net_ipv4_be = CLKS_NET_FALLBACK_IP_BE;
    clks_net_netmask_be = CLKS_NET_FALLBACK_NETMASK_BE;
    clks_net_gateway_be = CLKS_NET_FALLBACK_GATEWAY_BE;
    clks_net_dns_be = CLKS_NET_FALLBACK_DNS_BE;
}

static void clks_net_dhcp_state_reset(void) {
    clks_net_dhcp_active = CLKS_FALSE;
    clks_net_dhcp_offer_ready = CLKS_FALSE;
    clks_net_dhcp_ack_ready = CLKS_FALSE;
    clks_net_dhcp_failed = CLKS_FALSE;
    clks_net_dhcp_xid = 0U;
    clks_net_dhcp_offer_ip_be = 0U;
    clks_net_dhcp_server_id_be = 0U;
    clks_net_dhcp_offer_netmask_be = 0U;
    clks_net_dhcp_offer_gateway_be = 0U;
    clks_net_dhcp_offer_dns_be = 0U;
}

static clks_bool clks_net_pt_next_level(u64 *entry, u64 **out_table) {
    u64 phys;
    u64 *table;

    if (entry == CLKS_NULL || out_table == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if ((*entry & CLKS_NET_PTE_PRESENT) == 0ULL) {
        u64 new_phys = clks_pmm_alloc_page();
        void *new_virt;

        if (new_phys == 0ULL) {
            return CLKS_FALSE;
        }

        new_virt = clks_boot_phys_to_virt(new_phys);
        if (new_virt == CLKS_NULL) {
            clks_pmm_free_page(new_phys);
            return CLKS_FALSE;
        }

        clks_memset(new_virt, 0, (usize)CLKS_PAGE_SIZE);
        *entry = (new_phys & CLKS_NET_PTE_ADDR_MASK) | CLKS_NET_PTE_PRESENT | CLKS_NET_PTE_WRITE;
    } else if ((*entry & CLKS_NET_PTE_PS) != 0ULL) {
        return CLKS_FALSE;
    }

    phys = *entry & CLKS_NET_PTE_ADDR_MASK;
    table = (u64 *)clks_boot_phys_to_virt(phys);
    if (table == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_table = table;
    return CLKS_TRUE;
}

static clks_bool clks_net_map_page_4k(u64 virt_addr, u64 phys_addr, u64 extra_flags) {
    u64 *pml4;
    u64 *pdpt;
    u64 *pd;
    u64 *pt;
    u64 *entry;
    u64 pml4_index;
    u64 pdpt_index;
    u64 pd_index;
    u64 pt_index;

    pml4 = (u64 *)clks_boot_phys_to_virt(clks_net_read_cr3() & CLKS_NET_PTE_ADDR_MASK);
    if (pml4 == CLKS_NULL) {
        return CLKS_FALSE;
    }

    pml4_index = (virt_addr >> 39U) & 0x1FFULL;
    pdpt_index = (virt_addr >> 30U) & 0x1FFULL;
    pd_index = (virt_addr >> 21U) & 0x1FFULL;
    pt_index = (virt_addr >> 12U) & 0x1FFULL;

    entry = &pml4[pml4_index];
    if (clks_net_pt_next_level(entry, &pdpt) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    entry = &pdpt[pdpt_index];
    if ((*entry & CLKS_NET_PTE_PRESENT) != 0ULL && (*entry & CLKS_NET_PTE_PS) != 0ULL) {
        return CLKS_TRUE;
    }
    if (clks_net_pt_next_level(entry, &pd) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    entry = &pd[pd_index];
    if ((*entry & CLKS_NET_PTE_PRESENT) != 0ULL && (*entry & CLKS_NET_PTE_PS) != 0ULL) {
        return CLKS_TRUE;
    }
    if (clks_net_pt_next_level(entry, &pt) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    pt[pt_index] = (phys_addr & CLKS_NET_PTE_ADDR_MASK) | CLKS_NET_PTE_PRESENT | CLKS_NET_PTE_WRITE | extra_flags;
    clks_net_invlpg(virt_addr);
    return CLKS_TRUE;
}

static clks_bool clks_net_map_mmio_hhdm(u64 phys_addr, u64 size, u8 **out_virt) {
    u64 hhdm_offset;
    u64 start;
    u64 end;
    u64 phys;

    if (out_virt == CLKS_NULL || size == 0ULL) {
        return CLKS_FALSE;
    }

    hhdm_offset = clks_boot_get_hhdm_offset();
    if (hhdm_offset == 0ULL) {
        return CLKS_FALSE;
    }

    start = phys_addr & ~(CLKS_PAGE_SIZE - 1ULL);
    end = (phys_addr + size + (CLKS_PAGE_SIZE - 1ULL)) & ~(CLKS_PAGE_SIZE - 1ULL);

    for (phys = start; phys < end; phys += CLKS_PAGE_SIZE) {
        u64 virt = hhdm_offset + phys;

        if (clks_net_map_page_4k(virt, phys, CLKS_NET_PTE_PWT | CLKS_NET_PTE_PCD) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    *out_virt = (u8 *)(usize)(hhdm_offset + phys_addr);
    return CLKS_TRUE;
}

static u32 clks_net_pci_cfg_addr(u8 bus, u8 dev, u8 func, u8 offset) {
    return 0x80000000U | ((u32)bus << 16U) | ((u32)dev << 11U) | ((u32)func << 8U) | ((u32)offset & 0xFCU);
}

static u32 clks_net_pci_read32(u8 bus, u8 dev, u8 func, u8 offset) {
    clks_net_outl(CLKS_NET_PCI_CFG_ADDR_PORT, clks_net_pci_cfg_addr(bus, dev, func, offset));
    return clks_net_inl(CLKS_NET_PCI_CFG_DATA_PORT);
}

static void clks_net_pci_write32(u8 bus, u8 dev, u8 func, u8 offset, u32 value) {
    clks_net_outl(CLKS_NET_PCI_CFG_ADDR_PORT, clks_net_pci_cfg_addr(bus, dev, func, offset));
    clks_net_outl(CLKS_NET_PCI_CFG_DATA_PORT, value);
}

static u16 clks_net_pci_read16(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 v = clks_net_pci_read32(bus, dev, func, offset);
    u32 shift = (u32)(offset & 2U) * 8U;
    return (u16)((v >> shift) & 0xFFFFU);
}

static u8 clks_net_pci_read8(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 v = clks_net_pci_read32(bus, dev, func, offset);
    u32 shift = (u32)(offset & 3U) * 8U;
    return (u8)((v >> shift) & 0xFFU);
}

static void clks_net_pci_write16(u8 bus, u8 dev, u8 func, u8 offset, u16 value) {
    u32 v = clks_net_pci_read32(bus, dev, func, offset);
    u32 shift = (u32)(offset & 2U) * 8U;
    u32 mask = 0xFFFFU << shift;
    v = (v & ~mask) | ((u32)value << shift);
    clks_net_pci_write32(bus, dev, func, offset, v);
}

static clks_bool clks_net_pci_find_capability(u8 bus, u8 dev, u8 func, u8 cap_id, u8 *out_cap_ptr) {
    u16 status;
    u8 cap_ptr;
    u32 guard = 0U;

    if (out_cap_ptr == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_cap_ptr = 0U;
    status = clks_net_pci_read16(bus, dev, func, 0x06U);
    if ((status & 0x0010U) == 0U) {
        return CLKS_FALSE;
    }

    cap_ptr = (u8)(clks_net_pci_read8(bus, dev, func, 0x34U) & 0xFCU);

    while (cap_ptr >= 0x40U && guard < 64U) {
        u8 current_id = clks_net_pci_read8(bus, dev, func, cap_ptr);
        u8 next_ptr = (u8)(clks_net_pci_read8(bus, dev, func, (u8)(cap_ptr + 1U)) & 0xFCU);

        if (current_id == cap_id) {
            *out_cap_ptr = cap_ptr;
            return CLKS_TRUE;
        }

        if (next_ptr == 0U || next_ptr == cap_ptr) {
            break;
        }

        cap_ptr = next_ptr;
        guard++;
    }

    return CLKS_FALSE;
}

static void clks_net_pci_force_d0(u8 bus, u8 dev, u8 func) {
    u8 pm_cap_ptr = 0U;

    if (clks_net_pci_find_capability(bus, dev, func, 0x01U, &pm_cap_ptr) == CLKS_TRUE) {
        u16 pmcsr_before = clks_net_pci_read16(bus, dev, func, (u8)(pm_cap_ptr + 4U));
        u16 pmcsr_after = (u16)(pmcsr_before & ~0x0003U);

        clks_log_hex(CLKS_LOG_INFO, "NET", "PMCSR_BEFORE", pmcsr_before);
        if ((pmcsr_before & 0x0003U) != 0U) {
            clks_net_pci_write16(bus, dev, func, (u8)(pm_cap_ptr + 4U), pmcsr_after);
            pmcsr_after = clks_net_pci_read16(bus, dev, func, (u8)(pm_cap_ptr + 4U));
        }
        clks_log_hex(CLKS_LOG_INFO, "NET", "PMCSR_AFTER", pmcsr_after);
    }
}

static clks_bool clks_net_is_known_e1000(u16 device_id) {
    u32 i;
    for (i = 0U; i < (u32)(sizeof(clks_net_e1000_dev_ids) / sizeof(clks_net_e1000_dev_ids[0])); i++) {
        if (clks_net_e1000_dev_ids[i] == device_id) {
            return CLKS_TRUE;
        }
    }
    return CLKS_FALSE;
}

static clks_bool clks_net_pci_find_e1000(struct clks_net_pci_location *out_loc) {
    u32 bus;

    if (out_loc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (bus = 0U; bus <= 255U; bus++) {
        u32 dev;
        for (dev = 0U; dev <= 31U; dev++) {
            u16 vendor0 = clks_net_pci_read16((u8)bus, (u8)dev, 0U, 0x00U);
            u8 header_type;
            u32 fn_count;
            u32 fn;

            if (vendor0 == 0xFFFFU) {
                continue;
            }

            header_type = clks_net_pci_read8((u8)bus, (u8)dev, 0U, 0x0EU);
            fn_count = ((header_type & 0x80U) != 0U) ? 8U : 1U;

            for (fn = 0U; fn < fn_count; fn++) {
                u16 vendor = clks_net_pci_read16((u8)bus, (u8)dev, (u8)fn, 0x00U);
                u16 device = clks_net_pci_read16((u8)bus, (u8)dev, (u8)fn, 0x02U);

                if (vendor == 0xFFFFU) {
                    continue;
                }

                if (vendor == CLKS_NET_E1000_VENDOR && clks_net_is_known_e1000(device) == CLKS_TRUE) {
                    out_loc->bus = (u8)bus;
                    out_loc->dev = (u8)dev;
                    out_loc->func = (u8)fn;
                    return CLKS_TRUE;
                }
            }
        }
    }

    return CLKS_FALSE;
}

static u32 clks_net_reg_read(u32 reg) {
    if (clks_net_reg_mode == CLKS_NET_REG_MMIO && clks_net_mmio_base != CLKS_NULL) {
        volatile u32 *ptr = (volatile u32 *)(void *)(clks_net_mmio_base + reg);
        return *ptr;
    }

    if (clks_net_reg_mode == CLKS_NET_REG_IO && clks_net_io_base != 0U) {
        clks_net_outl(clks_net_io_base, reg);
        return clks_net_inl((u16)(clks_net_io_base + 4U));
    }

    return 0U;
}

static void clks_net_reg_write(u32 reg, u32 value) {
    if (clks_net_reg_mode == CLKS_NET_REG_MMIO && clks_net_mmio_base != CLKS_NULL) {
        volatile u32 *ptr = (volatile u32 *)(void *)(clks_net_mmio_base + reg);
        *ptr = value;
        return;
    }

    if (clks_net_reg_mode == CLKS_NET_REG_IO && clks_net_io_base != 0U) {
        clks_net_outl(clks_net_io_base, reg);
        clks_net_outl((u16)(clks_net_io_base + 4U), value);
    }
}

static void clks_net_e1000_delay(u32 loops) {
    u32 i;
    for (i = 0U; i < loops; i++) {
        clks_cpu_pause();
    }
}

static void clks_net_e1000_ints_off(void) {
    clks_net_reg_write(E1000_REG_IMC, 0xFFFFFFFFU);
    clks_net_reg_write(E1000_REG_ICR, 0xFFFFFFFFU);
    (void)clks_net_reg_read(E1000_REG_STATUS);
}

static clks_bool clks_net_alloc_dma_page(u64 *out_phys, void **out_virt) {
    u64 phys;
    void *virt;

    if (out_phys == CLKS_NULL || out_virt == CLKS_NULL) {
        return CLKS_FALSE;
    }

    phys = clks_pmm_alloc_page();
    if (phys == 0ULL) {
        return CLKS_FALSE;
    }

    virt = clks_boot_phys_to_virt(phys);
    if (virt == CLKS_NULL) {
        clks_pmm_free_page(phys);
        return CLKS_FALSE;
    }

    clks_memset(virt, 0, (usize)CLKS_PAGE_SIZE);
    *out_phys = phys;
    *out_virt = virt;
    return CLKS_TRUE;
}

static clks_bool clks_net_eeprom_detect(void) {
    u32 i;

    clks_net_reg_write(CLKS_NET_REG_EERD, 1U);

    for (i = 0U; i < 10000U; i++) {
        u32 val = clks_net_reg_read(CLKS_NET_REG_EERD);
        if ((val & (1U << 4U)) != 0U) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static u16 clks_net_eeprom_read(u8 addr) {
    u32 i;

    clks_net_reg_write(CLKS_NET_REG_EERD, 1U | ((u32)addr << 8U));
    for (i = 0U; i < 10000U; i++) {
        u32 val = clks_net_reg_read(CLKS_NET_REG_EERD);
        if ((val & (1U << 4U)) != 0U) {
            return (u16)((val >> 16U) & 0xFFFFU);
        }
    }

    return 0U;
}

static void clks_net_read_mac(void) {
    clks_bool has_eeprom = clks_net_eeprom_detect();

    if (has_eeprom == CLKS_TRUE) {
        u16 w0 = clks_net_eeprom_read(0U);
        u16 w1 = clks_net_eeprom_read(1U);
        u16 w2 = clks_net_eeprom_read(2U);
        clks_net_mac[0] = (u8)(w0 & 0xFFU);
        clks_net_mac[1] = (u8)((w0 >> 8U) & 0xFFU);
        clks_net_mac[2] = (u8)(w1 & 0xFFU);
        clks_net_mac[3] = (u8)((w1 >> 8U) & 0xFFU);
        clks_net_mac[4] = (u8)(w2 & 0xFFU);
        clks_net_mac[5] = (u8)((w2 >> 8U) & 0xFFU);
    } else {
        u32 low = clks_net_reg_read(E1000_REG_RXADDR + 0U);
        u32 high = clks_net_reg_read(E1000_REG_RXADDR + 4U);
        clks_net_mac[0] = (u8)((low >> 0U) & 0xFFU);
        clks_net_mac[1] = (u8)((low >> 8U) & 0xFFU);
        clks_net_mac[2] = (u8)((low >> 16U) & 0xFFU);
        clks_net_mac[3] = (u8)((low >> 24U) & 0xFFU);
        clks_net_mac[4] = (u8)((high >> 0U) & 0xFFU);
        clks_net_mac[5] = (u8)((high >> 8U) & 0xFFU);
    }

    if (clks_net_mac_invalid(clks_net_mac) == CLKS_TRUE) {
        clks_memcpy(clks_net_mac, clks_net_mac_fallback, CLKS_NET_ETH_ADDR_LEN);
        clks_log(CLKS_LOG_WARN, "NET", "INVALID HW MAC, USING FALLBACK");
    }
}

static void clks_net_program_mac_filter(void) {
    u32 i;
    u32 low = ((u32)clks_net_mac[0]) | ((u32)clks_net_mac[1] << 8U) | ((u32)clks_net_mac[2] << 16U) |
              ((u32)clks_net_mac[3] << 24U);
    u32 high = ((u32)clks_net_mac[4]) | ((u32)clks_net_mac[5] << 8U) | 0x80000000U;

    clks_net_reg_write(E1000_REG_RXADDR + 0U, low);
    clks_net_reg_write(E1000_REG_RXADDR + 4U, high);

    for (i = 0U; i < 128U; i++) {
        clks_net_reg_write(CLKS_NET_REG_MTA + (i * 4U), 0U);
    }
}

static clks_bool clks_net_setup_descs(void) {
    u32 i;
    u32 rctl;
    u32 tctl;

    if (clks_net_alloc_dma_page(&clks_net_rx_descs_phys, (void **)&clks_net_rx_descs) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_net_alloc_dma_page(&clks_net_tx_descs_phys, (void **)&clks_net_tx_descs) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < CLKS_NET_DESC_COUNT; i++) {
        if (clks_net_alloc_dma_page(&clks_net_rx_buf_phys[i], (void **)&clks_net_rx_buf_virt[i]) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (clks_net_alloc_dma_page(&clks_net_tx_buf_phys[i], (void **)&clks_net_tx_buf_virt[i]) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        clks_net_rx_descs[i].addr = clks_net_rx_buf_phys[i];
        clks_net_rx_descs[i].status = 0U;
        clks_net_rx_descs[i].length = 0U;
        clks_net_rx_descs[i].checksum = 0U;
        clks_net_rx_descs[i].errors = 0U;
        clks_net_rx_descs[i].special = 0U;

        clks_net_tx_descs[i].addr = clks_net_tx_buf_phys[i];
        clks_net_tx_descs[i].length = 0U;
        clks_net_tx_descs[i].cso = 0U;
        clks_net_tx_descs[i].cmd = 0U;
        clks_net_tx_descs[i].status = 0x1U;
        clks_net_tx_descs[i].css = 0U;
        clks_net_tx_descs[i].special = 0U;
    }

    clks_net_reg_write(E1000_REG_RXDESCLO, (u32)(clks_net_rx_descs_phys & 0xFFFFFFFFULL));
    clks_net_reg_write(E1000_REG_RXDESCHI, (u32)(clks_net_rx_descs_phys >> 32U));
    clks_net_reg_write(E1000_REG_RXDESCLEN, CLKS_NET_DESC_COUNT * (u32)sizeof(struct e1000_rx_desc));
    clks_net_reg_write(E1000_REG_RXDESCHEAD, 0U);
    clks_net_reg_write(E1000_REG_RXDESCTAIL, CLKS_NET_DESC_COUNT - 1U);

    clks_net_reg_write(E1000_REG_TXDESCLO, (u32)(clks_net_tx_descs_phys & 0xFFFFFFFFULL));
    clks_net_reg_write(E1000_REG_TXDESCHI, (u32)(clks_net_tx_descs_phys >> 32U));
    clks_net_reg_write(E1000_REG_TXDESCLEN, CLKS_NET_DESC_COUNT * (u32)sizeof(struct e1000_tx_desc));
    clks_net_reg_write(E1000_REG_TXDESCHEAD, 0U);
    clks_net_reg_write(E1000_REG_TXDESCTAIL, 0U);

    clks_net_reg_write(E1000_REG_RDTR, 0U);
    clks_net_reg_write(E1000_REG_ITR, 0U);

    rctl = RCTL_EN | RCTL_BAM | RCTL_SECRC | RCTL_BSIZE_2048;
    clks_net_reg_write(E1000_REG_RCTRL, rctl);

    tctl = TCTL_EN | TCTL_PSP | TCTL_RTLC | (15U << TCTL_CT_SHIFT) | (64U << TCTL_COLD_SHIFT);
    clks_net_reg_write(E1000_REG_TCTRL, tctl);
    clks_net_reg_write(CLKS_NET_REG_TIPG, 0x0060200AU);

    clks_net_rx_index = 0U;
    clks_net_tx_index = 0U;

    return CLKS_TRUE;
}

static clks_bool clks_net_init_register_access(const struct clks_net_pci_location *loc) {
    u32 bars[6];
    u32 i;
    clks_bool io_ok = CLKS_FALSE;
    clks_bool mmio_ok = CLKS_FALSE;
    u64 chosen_mmio_phys = 0ULL;
    u16 cmd;

    if (loc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    cmd = clks_net_pci_read16(loc->bus, loc->dev, loc->func, 0x04U);
    cmd |= 0x0007U;
    clks_net_pci_write16(loc->bus, loc->dev, loc->func, 0x04U, cmd);
    cmd = clks_net_pci_read16(loc->bus, loc->dev, loc->func, 0x04U);
    clks_log_hex(CLKS_LOG_INFO, "NET", "PCI_CMD", cmd);

    for (i = 0U; i < 6U; i++) {
        bars[i] = clks_net_pci_read32(loc->bus, loc->dev, loc->func, (u8)(0x10U + i * 4U));
    }

    clks_log_hex(CLKS_LOG_INFO, "NET", "PCI_BAR0", bars[0]);
    clks_log_hex(CLKS_LOG_INFO, "NET", "PCI_BAR1", bars[1]);
    clks_log_hex(CLKS_LOG_INFO, "NET", "PCI_BAR2", bars[2]);
    clks_log_hex(CLKS_LOG_INFO, "NET", "PCI_BAR3", bars[3]);
    clks_log_hex(CLKS_LOG_INFO, "NET", "PCI_BAR4", bars[4]);
    clks_log_hex(CLKS_LOG_INFO, "NET", "PCI_BAR5", bars[5]);

    for (i = 0U; i < 6U; i++) {
        u32 bar = bars[i];

        if (bar == 0U || bar == 0xFFFFFFFFU) {
            continue;
        }

        if ((bar & 0x1U) != 0U) {
            if (io_ok == CLKS_FALSE) {
                clks_net_io_base = (u16)(bar & ~0x3U);
                if (clks_net_io_base != 0U) {
                    io_ok = CLKS_TRUE;
                }
            }
            continue;
        }

        if (mmio_ok == CLKS_FALSE) {
            u64 mmio_phys = (u64)(bar & ~0xFU);
            u32 bar_type = (bar >> 1U) & 0x3U;

            if (bar_type == 0x2U && (i + 1U) < 6U) {
                mmio_phys |= ((u64)bars[i + 1U] << 32U);
                i++;
            }

            if (clks_net_map_mmio_hhdm(mmio_phys, CLKS_NET_MMIO_MAP_SIZE, &clks_net_mmio_base) == CLKS_TRUE) {
                mmio_ok = CLKS_TRUE;
                chosen_mmio_phys = mmio_phys;
                clks_log_hex(CLKS_LOG_INFO, "NET", "MMIO_PHYS", mmio_phys);
            } else {
                clks_log(CLKS_LOG_WARN, "NET", "MMIO MAP FAILED");
                clks_log_hex(CLKS_LOG_WARN, "NET", "MMIO_PHYS", mmio_phys);
            }
        }
    }

    clks_net_reg_mode = CLKS_NET_REG_NONE;
    if (mmio_ok == CLKS_TRUE) {
        clks_net_reg_mode = CLKS_NET_REG_MMIO;
        clks_log(CLKS_LOG_INFO, "NET", "REG ACCESS MODE MMIO");
        clks_log_hex(CLKS_LOG_INFO, "NET", "MMIO_ACTIVE", chosen_mmio_phys);
        if (io_ok == CLKS_TRUE) {
            clks_log_hex(CLKS_LOG_INFO, "NET", "IO_BASE", (u64)clks_net_io_base);
        }
        return CLKS_TRUE;
    }

    if (io_ok == CLKS_TRUE) {
        clks_net_reg_mode = CLKS_NET_REG_IO;
        clks_log_hex(CLKS_LOG_INFO, "NET", "IO_BASE", (u64)clks_net_io_base);
        clks_log(CLKS_LOG_INFO, "NET", "REG ACCESS MODE IO");
        clks_log(CLKS_LOG_WARN, "NET", "MMIO UNAVAILABLE, FALLING BACK TO IO");
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

static clks_bool clks_net_hw_reset(void) {
    u32 ctrl;
    u32 i;

    clks_net_e1000_ints_off();
    clks_net_reg_write(E1000_REG_RCTRL, 0U);
    clks_net_reg_write(E1000_REG_TCTRL, 0U);

    ctrl = clks_net_reg_read(CLKS_NET_REG_CTRL);
    clks_net_reg_write(CLKS_NET_REG_CTRL, ctrl | CLKS_NET_CTRL_RST);

    for (i = 0U; i < 1000000U; i++) {
        if ((clks_net_reg_read(CLKS_NET_REG_CTRL) & CLKS_NET_CTRL_RST) == 0U) {
            break;
        }
        clks_cpu_pause();
    }

    if (i >= 1000000U) {
        clks_log(CLKS_LOG_ERROR, "NET", "RESET TIMEOUT");
        return CLKS_FALSE;
    }

    clks_net_e1000_delay(200000U);
    clks_net_e1000_ints_off();

    ctrl = clks_net_reg_read(CLKS_NET_REG_CTRL);
    ctrl |= CLKS_NET_CTRL_SLU;
    ctrl |= (2U << 8U);
    ctrl &= ~CLKS_NET_CTRL_LRST;
    ctrl &= ~CLKS_NET_CTRL_PHY_RST;
    clks_net_reg_write(CLKS_NET_REG_CTRL, ctrl);

    return CLKS_TRUE;
}

static clks_bool clks_net_hw_init(void) {
    struct clks_net_pci_location loc;
    u16 vendor;
    u16 device;
    u32 status;
    char mac_text[18];

    if (clks_net_pci_find_e1000(&loc) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "NET", "E1000 PCI DEVICE NOT FOUND");
        return CLKS_FALSE;
    }

    vendor = clks_net_pci_read16(loc.bus, loc.dev, loc.func, 0x00U);
    device = clks_net_pci_read16(loc.bus, loc.dev, loc.func, 0x02U);
    clks_log_hex(CLKS_LOG_INFO, "NET", "PCI_VENDOR", vendor);
    clks_log_hex(CLKS_LOG_INFO, "NET", "PCI_DEVICE", device);

    clks_net_pci_force_d0(loc.bus, loc.dev, loc.func);

    if (clks_net_init_register_access(&loc) == CLKS_FALSE) {
        clks_log(CLKS_LOG_ERROR, "NET", "NO USABLE E1000 BAR");
        return CLKS_FALSE;
    }

    if (clks_net_hw_reset() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_net_read_mac();
    clks_net_program_mac_filter();

    if (clks_net_setup_descs() == CLKS_FALSE) {
        clks_log(CLKS_LOG_ERROR, "NET", "DESC INIT FAILED");
        return CLKS_FALSE;
    }

    status = clks_net_reg_read(CLKS_NET_REG_STATUS);
    clks_log(CLKS_LOG_INFO, "NET", "E1000 ONLINE");
    clks_net_format_mac(clks_net_mac, mac_text);
    clks_log(CLKS_LOG_INFO, "NET", mac_text);
    clks_log_hex(CLKS_LOG_INFO, "NET", "STATUS", status);

    if ((status & CLKS_NET_STATUS_LINK_UP) == 0U) {
        clks_log(CLKS_LOG_WARN, "NET", "SUSPICIOUS STATUS, LINK MAY BE DOWN");
    }

    return CLKS_TRUE;
}

static clks_bool clks_net_tx_frame(const u8 *frame, u16 len) {
    u32 index;
    volatile struct e1000_tx_desc *desc;
    u16 send_len = len;
    u32 wait_loops = 0U;

    if (clks_net_hw_ready == CLKS_FALSE || frame == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (len == 0U || len > CLKS_NET_BUF_SIZE) {
        return CLKS_FALSE;
    }

    if (send_len < 60U) {
        send_len = 60U;
    }

    index = clks_net_tx_index;
    desc = &clks_net_tx_descs[index];

    while ((desc->status & 0x1U) == 0U) {
        if (wait_loops >= CLKS_NET_TX_WAIT_LOOPS) {
            return CLKS_FALSE;
        }
        wait_loops++;
        clks_cpu_pause();
    }

    clks_memcpy(clks_net_tx_buf_virt[index], frame, len);
    if (send_len > len) {
        clks_memset(clks_net_tx_buf_virt[index] + len, 0, (usize)(send_len - len));
    }

    desc->length = send_len;
    desc->cso = 0U;
    desc->cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    desc->status = 0U;
    desc->css = 0U;
    desc->special = 0U;

    clks_net_tx_index = (index + 1U) % CLKS_NET_DESC_COUNT;
    clks_net_reg_write(E1000_REG_TXDESCTAIL, clks_net_tx_index);
    return CLKS_TRUE;
}

static void clks_net_arp_table_reset(void) {
    clks_memset(clks_net_arp_table, 0, sizeof(clks_net_arp_table));
    clks_net_arp_stamp = 1ULL;
}

static void clks_net_arp_upsert(u32 ipv4_be, const u8 *mac) {
    u32 i;
    u32 free_index = CLKS_NET_ARP_TABLE_CAP;
    u32 oldest_index = 0U;
    u64 oldest_stamp = (u64)-1;

    if (mac == CLKS_NULL || clks_net_mac_invalid(mac) == CLKS_TRUE || ipv4_be == 0U) {
        return;
    }

    for (i = 0U; i < CLKS_NET_ARP_TABLE_CAP; i++) {
        if (clks_net_arp_table[i].valid == CLKS_TRUE && clks_net_arp_table[i].ipv4_be == ipv4_be) {
            clks_memcpy(clks_net_arp_table[i].mac, mac, CLKS_NET_ETH_ADDR_LEN);
            clks_net_arp_table[i].stamp = clks_net_arp_stamp++;
            return;
        }

        if (clks_net_arp_table[i].valid == CLKS_FALSE && free_index == CLKS_NET_ARP_TABLE_CAP) {
            free_index = i;
        }

        if (clks_net_arp_table[i].stamp < oldest_stamp) {
            oldest_stamp = clks_net_arp_table[i].stamp;
            oldest_index = i;
        }
    }

    if (free_index == CLKS_NET_ARP_TABLE_CAP) {
        free_index = oldest_index;
    }

    clks_net_arp_table[free_index].valid = CLKS_TRUE;
    clks_net_arp_table[free_index].ipv4_be = ipv4_be;
    clks_memcpy(clks_net_arp_table[free_index].mac, mac, CLKS_NET_ETH_ADDR_LEN);
    clks_net_arp_table[free_index].stamp = clks_net_arp_stamp++;
}

static clks_bool clks_net_arp_lookup(u32 ipv4_be, u8 *out_mac) {
    u32 i;

    if (out_mac == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < CLKS_NET_ARP_TABLE_CAP; i++) {
        if (clks_net_arp_table[i].valid == CLKS_TRUE && clks_net_arp_table[i].ipv4_be == ipv4_be) {
            clks_memcpy(out_mac, clks_net_arp_table[i].mac, CLKS_NET_ETH_ADDR_LEN);
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static void clks_net_udp_queue_reset(void) {
    clks_memset(clks_net_udp_queue, 0, sizeof(clks_net_udp_queue));
    clks_net_udp_head = 0U;
    clks_net_udp_tail = 0U;
    clks_net_udp_count = 0U;
}

static void clks_net_udp_queue_push(u32 src_ipv4_be, u16 src_port, u16 dst_port, const u8 *payload, u16 payload_len) {
    struct clks_net_udp_packet *pkt;

    if (payload == CLKS_NULL && payload_len != 0U) {
        return;
    }

    if (payload_len > CLKS_NET_UDP_PAYLOAD_MAX) {
        payload_len = CLKS_NET_UDP_PAYLOAD_MAX;
    }

    if (clks_net_udp_count >= CLKS_NET_UDP_QUEUE_CAP) {
        clks_net_udp_head = (clks_net_udp_head + 1U) % CLKS_NET_UDP_QUEUE_CAP;
        clks_net_udp_count--;
    }

    pkt = &clks_net_udp_queue[clks_net_udp_tail];
    pkt->valid = CLKS_TRUE;
    pkt->src_ipv4_be = src_ipv4_be;
    pkt->src_port = src_port;
    pkt->dst_port = dst_port;
    pkt->payload_len = payload_len;
    if (payload_len > 0U) {
        clks_memcpy(pkt->payload, payload, payload_len);
    }

    clks_net_udp_tail = (clks_net_udp_tail + 1U) % CLKS_NET_UDP_QUEUE_CAP;
    clks_net_udp_count++;
}

static u32 clks_net_next_hop_for(u32 dst_ipv4_be) {
    u32 local_net;
    u32 dst_net;

    if (dst_ipv4_be == 0U || dst_ipv4_be == 0xFFFFFFFFU) {
        return dst_ipv4_be;
    }

    if (clks_net_netmask_be == 0U) {
        return (clks_net_gateway_be != 0U) ? clks_net_gateway_be : dst_ipv4_be;
    }

    local_net = clks_net_ipv4_be & clks_net_netmask_be;
    dst_net = dst_ipv4_be & clks_net_netmask_be;

    if (dst_net == local_net) {
        return dst_ipv4_be;
    }
    return (clks_net_gateway_be != 0U) ? clks_net_gateway_be : dst_ipv4_be;
}

static clks_bool clks_net_send_arp_packet(u16 op, const u8 *dst_mac, u32 target_ip_be) {
    u8 frame[CLKS_NET_ETH_HEADER_LEN + sizeof(struct clks_net_arp_packet)];
    struct clks_net_eth_hdr *eth = (struct clks_net_eth_hdr *)(void *)frame;
    struct clks_net_arp_packet *arp = (struct clks_net_arp_packet *)(void *)(frame + CLKS_NET_ETH_HEADER_LEN);

    if (dst_mac == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_memcpy(eth->dst, dst_mac, CLKS_NET_ETH_ADDR_LEN);
    clks_memcpy(eth->src, clks_net_mac, CLKS_NET_ETH_ADDR_LEN);
    clks_net_write_be16(eth->ethertype, CLKS_NET_ETHERTYPE_ARP);

    clks_net_write_be16(arp->htype, CLKS_NET_ARP_HTYPE_ETHERNET);
    clks_net_write_be16(arp->ptype, CLKS_NET_ETHERTYPE_IPV4);
    arp->hlen = 6U;
    arp->plen = 4U;
    clks_net_write_be16(arp->oper, op);
    clks_memcpy(arp->sha, clks_net_mac, CLKS_NET_ETH_ADDR_LEN);
    clks_net_write_be32(arp->spa, clks_net_ipv4_be);

    if (op == CLKS_NET_ARP_OP_REQUEST) {
        clks_memset(arp->tha, 0, CLKS_NET_ETH_ADDR_LEN);
    } else {
        clks_memcpy(arp->tha, dst_mac, CLKS_NET_ETH_ADDR_LEN);
    }
    clks_net_write_be32(arp->tpa, target_ip_be);

    return clks_net_tx_frame(frame, (u16)sizeof(frame));
}

static clks_bool clks_net_send_ipv4_packet_from(u32 src_ipv4_be, u32 dst_ipv4_be, const u8 *dst_mac, u8 proto,
                                                const u8 *payload, u16 payload_len) {
    u8 frame[CLKS_NET_MAX_FRAME_NO_FCS];
    struct clks_net_eth_hdr *eth = (struct clks_net_eth_hdr *)(void *)frame;
    struct clks_net_ipv4_hdr *ip = (struct clks_net_ipv4_hdr *)(void *)(frame + CLKS_NET_ETH_HEADER_LEN);
    u16 ip_total_len = (u16)(CLKS_NET_IP_HEADER_MIN_LEN + payload_len);
    u16 frame_len = (u16)(CLKS_NET_ETH_HEADER_LEN + ip_total_len);
    u16 csum;
    u8 *payload_out;

    if (dst_mac == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (payload_len > CLKS_NET_MAX_IPV4_PAYLOAD) {
        return CLKS_FALSE;
    }

    if (payload_len > 0U && payload == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_memcpy(eth->dst, dst_mac, CLKS_NET_ETH_ADDR_LEN);
    clks_memcpy(eth->src, clks_net_mac, CLKS_NET_ETH_ADDR_LEN);
    clks_net_write_be16(eth->ethertype, CLKS_NET_ETHERTYPE_IPV4);

    ip->ver_ihl = 0x45U;
    ip->dscp_ecn = 0U;
    clks_net_write_be16(ip->total_len, ip_total_len);
    clks_net_write_be16(ip->ident, clks_net_ipv4_ident++);
    clks_net_write_be16(ip->flags_frag, 0x4000U);
    ip->ttl = 64U;
    ip->proto = proto;
    clks_net_write_be16(ip->checksum, 0U);
    clks_net_write_be32(ip->src, src_ipv4_be);
    clks_net_write_be32(ip->dst, dst_ipv4_be);

    payload_out = frame + CLKS_NET_ETH_HEADER_LEN + CLKS_NET_IP_HEADER_MIN_LEN;
    if (payload_len > 0U) {
        clks_memcpy(payload_out, payload, payload_len);
    }

    csum = clks_net_checksum16((const u8 *)(const void *)ip, CLKS_NET_IP_HEADER_MIN_LEN);
    clks_net_write_be16(ip->checksum, csum);

    return clks_net_tx_frame(frame, frame_len);
}

static clks_bool clks_net_send_ipv4_packet(u32 dst_ipv4_be, const u8 *dst_mac, u8 proto, const u8 *payload,
                                           u16 payload_len) {
    return clks_net_send_ipv4_packet_from(clks_net_ipv4_be, dst_ipv4_be, dst_mac, proto, payload, payload_len);
}

static clks_bool clks_net_send_udp_datagram(u32 src_ipv4_be, u32 dst_ipv4_be, const u8 *dst_mac, u16 src_port,
                                            u16 dst_port, const void *payload, u16 payload_len) {
    u8 udp[CLKS_NET_UDP_HEADER_LEN + CLKS_NET_UDP_PAYLOAD_MAX];
    struct clks_net_udp_hdr *hdr = (struct clks_net_udp_hdr *)(void *)udp;
    u16 udp_len = (u16)(CLKS_NET_UDP_HEADER_LEN + payload_len);
    u16 checksum;

    if (dst_mac == CLKS_NULL || src_port == 0U || dst_port == 0U) {
        return CLKS_FALSE;
    }

    if (payload_len > CLKS_NET_UDP_PAYLOAD_MAX) {
        return CLKS_FALSE;
    }

    if (payload_len > 0U && payload == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_net_write_be16(hdr->src_port, src_port);
    clks_net_write_be16(hdr->dst_port, dst_port);
    clks_net_write_be16(hdr->len, udp_len);
    clks_net_write_be16(hdr->checksum, 0U);

    if (payload_len > 0U) {
        clks_memcpy(udp + CLKS_NET_UDP_HEADER_LEN, payload, payload_len);
    }

    checksum = clks_net_udp_checksum(src_ipv4_be, dst_ipv4_be, udp, udp_len);
    clks_net_write_be16(hdr->checksum, checksum);

    return clks_net_send_ipv4_packet_from(src_ipv4_be, dst_ipv4_be, dst_mac, CLKS_NET_IPV4_PROTO_UDP, udp, udp_len);
}

static clks_bool clks_net_send_icmp_echo(u32 dst_ipv4_be, const u8 *dst_mac, u8 type, u16 seq, const u8 *payload,
                                         u16 payload_len) {
    u8 icmp[CLKS_NET_ICMP_HEADER_LEN + 64U];
    struct clks_net_icmp_echo_hdr *hdr = (struct clks_net_icmp_echo_hdr *)(void *)icmp;
    u16 total_len = (u16)(CLKS_NET_ICMP_HEADER_LEN + payload_len);
    u16 csum;

    if (payload_len > 64U) {
        return CLKS_FALSE;
    }

    if (payload_len > 0U && payload == CLKS_NULL) {
        return CLKS_FALSE;
    }

    hdr->type = type;
    hdr->code = 0U;
    clks_net_write_be16(hdr->checksum, 0U);
    clks_net_write_be16(hdr->ident, clks_net_ping_ident);
    clks_net_write_be16(hdr->seq, seq);

    if (payload_len > 0U) {
        clks_memcpy(icmp + CLKS_NET_ICMP_HEADER_LEN, payload, payload_len);
    }

    csum = clks_net_checksum16(icmp, total_len);
    clks_net_write_be16(hdr->checksum, csum);

    return clks_net_send_ipv4_packet(dst_ipv4_be, dst_mac, CLKS_NET_IPV4_PROTO_ICMP, icmp, total_len);
}

static clks_bool clks_net_send_icmp_echo_reply(u32 dst_ipv4_be, const u8 *dst_mac, u16 seq, const u8 *payload,
                                               u16 payload_len) {
    return clks_net_send_icmp_echo(dst_ipv4_be, dst_mac, 0U, seq, payload, payload_len);
}

static clks_bool clks_net_send_icmp_echo_request(u32 dst_ipv4_be, const u8 *dst_mac, u16 seq) {
    u8 payload[32];
    u32 i;

    for (i = 0U; i < 32U; i++) {
        payload[i] = (u8)(i + (seq & 0xFFU));
    }

    return clks_net_send_icmp_echo(dst_ipv4_be, dst_mac, 8U, seq, payload, (u16)sizeof(payload));
}

static clks_bool clks_net_dhcp_append_opt(u8 *packet, u16 capacity, u16 *io_at, u8 code, const void *data, u8 len) {
    if (packet == CLKS_NULL || io_at == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if ((u32)(*io_at) + 2U + (u32)len > (u32)capacity) {
        return CLKS_FALSE;
    }

    packet[*io_at] = code;
    packet[*io_at + 1U] = len;
    if (len > 0U && data != CLKS_NULL) {
        clks_memcpy(packet + *io_at + 2U, data, len);
    }
    *io_at = (u16)(*io_at + 2U + (u16)len);
    return CLKS_TRUE;
}

static clks_bool clks_net_dhcp_send_discover(void) {
    u8 packet[300];
    u8 param_req[4] = {CLKS_NET_DHCP_OPT_SUBNET_MASK, CLKS_NET_DHCP_OPT_ROUTER, CLKS_NET_DHCP_OPT_DNS, 15U};
    u8 client_id[7];
    static const char hostname[] = "cleonos";
    u16 at = 240U;

    clks_memset(packet, 0, sizeof(packet));
    packet[0] = 1U;
    packet[1] = 1U;
    packet[2] = 6U;
    clks_net_write_be32(&packet[4], clks_net_dhcp_xid);
    clks_net_write_be16(&packet[10], CLKS_NET_DHCP_FLAG_BROADCAST);
    clks_memcpy(&packet[28], clks_net_mac, CLKS_NET_ETH_ADDR_LEN);
    clks_net_write_be32(&packet[236], CLKS_NET_DHCP_MAGIC);

    client_id[0] = 1U;
    clks_memcpy(&client_id[1], clks_net_mac, CLKS_NET_ETH_ADDR_LEN);

    if (clks_net_dhcp_append_opt(packet, (u16)sizeof(packet), &at, CLKS_NET_DHCP_OPT_MSG_TYPE,
                                 (const void *)&(u8){CLKS_NET_DHCP_MSG_DISCOVER}, 1U) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_net_dhcp_append_opt(packet, (u16)sizeof(packet), &at, CLKS_NET_DHCP_OPT_PARAM_REQ, param_req,
                                 (u8)sizeof(param_req)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_net_dhcp_append_opt(packet, (u16)sizeof(packet), &at, CLKS_NET_DHCP_OPT_CLIENT_ID, client_id,
                                 (u8)sizeof(client_id)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_net_dhcp_append_opt(packet, (u16)sizeof(packet), &at, CLKS_NET_DHCP_OPT_HOSTNAME, hostname,
                                 (u8)(sizeof(hostname) - 1U)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (at >= (u16)sizeof(packet)) {
        return CLKS_FALSE;
    }
    packet[at++] = CLKS_NET_DHCP_OPT_END;

    return clks_net_send_udp_datagram(0U, 0xFFFFFFFFU, clks_net_mac_broadcast, CLKS_NET_DHCP_CLIENT_PORT,
                                      CLKS_NET_DHCP_SERVER_PORT, packet, (u16)at);
}

static clks_bool clks_net_dhcp_send_request(void) {
    u8 packet[300];
    u8 param_req[4] = {CLKS_NET_DHCP_OPT_SUBNET_MASK, CLKS_NET_DHCP_OPT_ROUTER, CLKS_NET_DHCP_OPT_DNS, 15U};
    u8 client_id[7];
    u8 req_ip_raw[4];
    u8 server_id_raw[4];
    u32 req_ip = clks_net_dhcp_offer_ip_be;
    u32 server_id = clks_net_dhcp_server_id_be;
    u16 at = 240U;

    if (req_ip == 0U || server_id == 0U) {
        return CLKS_FALSE;
    }

    clks_memset(packet, 0, sizeof(packet));
    packet[0] = 1U;
    packet[1] = 1U;
    packet[2] = 6U;
    clks_net_write_be32(&packet[4], clks_net_dhcp_xid);
    clks_net_write_be16(&packet[10], CLKS_NET_DHCP_FLAG_BROADCAST);
    clks_memcpy(&packet[28], clks_net_mac, CLKS_NET_ETH_ADDR_LEN);
    clks_net_write_be32(&packet[236], CLKS_NET_DHCP_MAGIC);

    client_id[0] = 1U;
    clks_memcpy(&client_id[1], clks_net_mac, CLKS_NET_ETH_ADDR_LEN);
    clks_net_write_be32(req_ip_raw, req_ip);
    clks_net_write_be32(server_id_raw, server_id);

    if (clks_net_dhcp_append_opt(packet, (u16)sizeof(packet), &at, CLKS_NET_DHCP_OPT_MSG_TYPE,
                                 (const void *)&(u8){CLKS_NET_DHCP_MSG_REQUEST}, 1U) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_net_dhcp_append_opt(packet, (u16)sizeof(packet), &at, CLKS_NET_DHCP_OPT_REQ_IP, req_ip_raw, 4U) ==
        CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_net_dhcp_append_opt(packet, (u16)sizeof(packet), &at, CLKS_NET_DHCP_OPT_SERVER_ID, server_id_raw, 4U) ==
        CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_net_dhcp_append_opt(packet, (u16)sizeof(packet), &at, CLKS_NET_DHCP_OPT_PARAM_REQ, param_req,
                                 (u8)sizeof(param_req)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_net_dhcp_append_opt(packet, (u16)sizeof(packet), &at, CLKS_NET_DHCP_OPT_CLIENT_ID, client_id,
                                 (u8)sizeof(client_id)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (at >= (u16)sizeof(packet)) {
        return CLKS_FALSE;
    }
    packet[at++] = CLKS_NET_DHCP_OPT_END;

    return clks_net_send_udp_datagram(0U, 0xFFFFFFFFU, clks_net_mac_broadcast, CLKS_NET_DHCP_CLIENT_PORT,
                                      CLKS_NET_DHCP_SERVER_PORT, packet, (u16)at);
}

static void clks_net_dhcp_process_payload(u32 src_ipv4_be, const u8 *payload, u16 payload_len) {
    u8 msg_type = 0U;
    u32 yiaddr_be;
    u32 server_id_be = 0U;
    u32 subnet_be = 0U;
    u32 router_be = 0U;
    u32 dns_be = 0U;
    u16 at = 240U;
    u32 xid;

    if (payload == CLKS_NULL || payload_len < 240U || clks_net_dhcp_active == CLKS_FALSE) {
        return;
    }

    if (payload[0] != 2U || payload[1] != 1U || payload[2] != 6U) {
        return;
    }

    xid = clks_net_read_be32(&payload[4]);
    if (xid != clks_net_dhcp_xid) {
        return;
    }

    if (clks_net_mac_equal(&payload[28], clks_net_mac) == CLKS_FALSE) {
        return;
    }

    if (clks_net_read_be32(&payload[236]) != CLKS_NET_DHCP_MAGIC) {
        return;
    }

    yiaddr_be = clks_net_read_be32(&payload[16]);

    while (at < payload_len) {
        u8 code = payload[at++];
        u8 len;

        if (code == 0U) {
            continue;
        }
        if (code == CLKS_NET_DHCP_OPT_END) {
            break;
        }
        if (at >= payload_len) {
            break;
        }

        len = payload[at++];
        if ((u32)at + (u32)len > (u32)payload_len) {
            break;
        }

        if (code == CLKS_NET_DHCP_OPT_MSG_TYPE && len >= 1U) {
            msg_type = payload[at];
        } else if (code == CLKS_NET_DHCP_OPT_SUBNET_MASK && len >= 4U) {
            subnet_be = clks_net_read_be32(&payload[at]);
        } else if (code == CLKS_NET_DHCP_OPT_ROUTER && len >= 4U) {
            router_be = clks_net_read_be32(&payload[at]);
        } else if (code == CLKS_NET_DHCP_OPT_DNS && len >= 4U) {
            dns_be = clks_net_read_be32(&payload[at]);
        } else if (code == CLKS_NET_DHCP_OPT_SERVER_ID && len >= 4U) {
            server_id_be = clks_net_read_be32(&payload[at]);
        }

        at = (u16)(at + len);
    }

    if (msg_type == CLKS_NET_DHCP_MSG_OFFER) {
        if (server_id_be == 0U) {
            server_id_be = src_ipv4_be;
        }
        clks_net_dhcp_offer_ip_be = yiaddr_be;
        clks_net_dhcp_server_id_be = server_id_be;
        clks_net_dhcp_offer_netmask_be = subnet_be;
        clks_net_dhcp_offer_gateway_be = router_be;
        clks_net_dhcp_offer_dns_be = dns_be;
        clks_net_dhcp_offer_ready = CLKS_TRUE;
        return;
    }

    if (msg_type == CLKS_NET_DHCP_MSG_ACK) {
        if (server_id_be == 0U) {
            server_id_be = src_ipv4_be;
        }
        if (yiaddr_be != 0U) {
            clks_net_dhcp_offer_ip_be = yiaddr_be;
        }
        if (server_id_be != 0U) {
            clks_net_dhcp_server_id_be = server_id_be;
        }
        if (subnet_be != 0U) {
            clks_net_dhcp_offer_netmask_be = subnet_be;
        }
        if (router_be != 0U) {
            clks_net_dhcp_offer_gateway_be = router_be;
        }
        if (dns_be != 0U) {
            clks_net_dhcp_offer_dns_be = dns_be;
        }
        clks_net_dhcp_ack_ready = CLKS_TRUE;
        return;
    }

    if (msg_type == CLKS_NET_DHCP_MSG_NAK) {
        clks_net_dhcp_failed = CLKS_TRUE;
    }
}

static clks_bool clks_net_dhcp_autoconfigure(void) {
    u64 i;

    clks_net_dhcp_state_reset();
    clks_net_dhcp_active = CLKS_TRUE;
    clks_net_dhcp_xid_seed++;
    if (clks_net_dhcp_xid_seed == 0U) {
        clks_net_dhcp_xid_seed = 1U;
    }
    clks_net_dhcp_xid = clks_net_dhcp_xid_seed;

    if (clks_net_dhcp_send_discover() == CLKS_FALSE) {
        clks_net_dhcp_active = CLKS_FALSE;
        return CLKS_FALSE;
    }

    for (i = 0ULL; i < CLKS_NET_DHCP_DISCOVER_POLL_LOOPS; i++) {
        clks_net_poll_internal();
        if (clks_net_dhcp_failed == CLKS_TRUE) {
            clks_net_dhcp_active = CLKS_FALSE;
            return CLKS_FALSE;
        }
        if (clks_net_dhcp_offer_ready == CLKS_TRUE) {
            break;
        }
        if ((i & CLKS_NET_POLL_SPIN_PAUSE_MASK) == 0ULL) {
            clks_cpu_pause();
        }
    }

    if (clks_net_dhcp_offer_ready == CLKS_FALSE || clks_net_dhcp_offer_ip_be == 0U ||
        clks_net_dhcp_server_id_be == 0U) {
        clks_net_dhcp_active = CLKS_FALSE;
        return CLKS_FALSE;
    }

    if (clks_net_dhcp_send_request() == CLKS_FALSE) {
        clks_net_dhcp_active = CLKS_FALSE;
        return CLKS_FALSE;
    }

    for (i = 0ULL; i < CLKS_NET_DHCP_ACK_POLL_LOOPS; i++) {
        clks_net_poll_internal();
        if (clks_net_dhcp_failed == CLKS_TRUE) {
            clks_net_dhcp_active = CLKS_FALSE;
            return CLKS_FALSE;
        }
        if (clks_net_dhcp_ack_ready == CLKS_TRUE) {
            break;
        }
        if ((i & CLKS_NET_POLL_SPIN_PAUSE_MASK) == 0ULL) {
            clks_cpu_pause();
        }
    }

    clks_net_dhcp_active = CLKS_FALSE;
    if (clks_net_dhcp_ack_ready == CLKS_FALSE || clks_net_dhcp_offer_ip_be == 0U) {
        return CLKS_FALSE;
    }

    clks_net_ipv4_be = clks_net_dhcp_offer_ip_be;
    clks_net_netmask_be =
        (clks_net_dhcp_offer_netmask_be != 0U) ? clks_net_dhcp_offer_netmask_be : CLKS_NET_FALLBACK_NETMASK_BE;
    clks_net_gateway_be =
        (clks_net_dhcp_offer_gateway_be != 0U)
            ? clks_net_dhcp_offer_gateway_be
            : ((clks_net_dhcp_server_id_be != 0U) ? clks_net_dhcp_server_id_be : CLKS_NET_FALLBACK_GATEWAY_BE);
    (void)clks_net_dhcp_offer_dns_be;
    clks_net_dns_be = CLKS_NET_CLOUDFLARE_DNS_PRIMARY_BE;

    return CLKS_TRUE;
}

static clks_bool clks_net_resolve_ipv4(u32 dst_ipv4_be, u8 *out_mac, u64 poll_budget) {
    u32 target_ip_be;
    u64 i;

    if (out_mac == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (dst_ipv4_be == 0U) {
        return CLKS_FALSE;
    }

    if (dst_ipv4_be == clks_net_ipv4_be) {
        clks_memcpy(out_mac, clks_net_mac, CLKS_NET_ETH_ADDR_LEN);
        return CLKS_TRUE;
    }

    target_ip_be = clks_net_next_hop_for(dst_ipv4_be);
    if (target_ip_be == 0U) {
        return CLKS_FALSE;
    }

    if (clks_net_arp_lookup(target_ip_be, out_mac) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    (void)clks_net_send_arp_packet(CLKS_NET_ARP_OP_REQUEST, clks_net_mac_broadcast, target_ip_be);

    if (poll_budget == 0ULL) {
        poll_budget = 100000ULL;
    }

    for (i = 0ULL; i < poll_budget; i++) {
        clks_net_poll();
        if (clks_net_arp_lookup(target_ip_be, out_mac) == CLKS_TRUE) {
            return CLKS_TRUE;
        }
        if ((i & CLKS_NET_POLL_SPIN_PAUSE_MASK) == 0ULL) {
            clks_cpu_pause();
        }
    }

    return CLKS_FALSE;
}

static void clks_net_process_icmp(u32 src_ipv4_be, const u8 *src_mac, const u8 *icmp, u16 icmp_len) {
    const struct clks_net_icmp_echo_hdr *hdr;
    u16 ident;
    u16 seq;
    const u8 *payload;
    u16 payload_len;

    if (src_mac == CLKS_NULL || icmp == CLKS_NULL || icmp_len < CLKS_NET_ICMP_HEADER_LEN) {
        return;
    }

    hdr = (const struct clks_net_icmp_echo_hdr *)(const void *)icmp;
    ident = clks_net_read_be16(hdr->ident);
    seq = clks_net_read_be16(hdr->seq);
    payload = icmp + CLKS_NET_ICMP_HEADER_LEN;
    payload_len = (u16)(icmp_len - CLKS_NET_ICMP_HEADER_LEN);

    if (hdr->type == 8U && hdr->code == 0U) {
        if (ident == clks_net_ping_ident) {
            (void)clks_net_send_icmp_echo_reply(src_ipv4_be, src_mac, seq, payload, payload_len);
        }
        return;
    }

    if (hdr->type == 0U && hdr->code == 0U) {
        if (ident == clks_net_ping_ident && clks_net_ping_waiting == CLKS_TRUE && seq == clks_net_ping_wait_seq &&
            src_ipv4_be == clks_net_ping_wait_dst_be) {
            clks_net_ping_reply_ok = CLKS_TRUE;
            clks_net_ping_waiting = CLKS_FALSE;
        }
    }
}

static void clks_net_process_udp(u32 src_ipv4_be, const u8 *udp, u16 udp_len) {
    const struct clks_net_udp_hdr *hdr;
    u16 src_port;
    u16 dst_port;
    u16 wire_len;
    const u8 *payload;
    u16 payload_len;

    if (udp == CLKS_NULL || udp_len < CLKS_NET_UDP_HEADER_LEN) {
        return;
    }

    hdr = (const struct clks_net_udp_hdr *)(const void *)udp;
    src_port = clks_net_read_be16(hdr->src_port);
    dst_port = clks_net_read_be16(hdr->dst_port);
    wire_len = clks_net_read_be16(hdr->len);
    if (wire_len < CLKS_NET_UDP_HEADER_LEN || wire_len > udp_len) {
        return;
    }

    payload = udp + CLKS_NET_UDP_HEADER_LEN;
    payload_len = (u16)(wire_len - CLKS_NET_UDP_HEADER_LEN);

    if (dst_port == CLKS_NET_DHCP_CLIENT_PORT && src_port == CLKS_NET_DHCP_SERVER_PORT) {
        clks_net_dhcp_process_payload(src_ipv4_be, payload, payload_len);
        return;
    }

    clks_net_udp_queue_push(src_ipv4_be, src_port, dst_port, payload, payload_len);
}

static void clks_net_process_ipv4(const u8 *frame, u16 frame_len) {
    const struct clks_net_eth_hdr *eth;
    const struct clks_net_ipv4_hdr *ip;
    u16 ip_len;
    u8 ihl_words;
    u16 ihl_bytes;
    u16 total_len;
    u16 frag;
    u32 dst_ipv4_be;
    u32 src_ipv4_be;
    const u8 *l4;
    u16 l4_len;

    if (frame == CLKS_NULL || frame_len < (CLKS_NET_ETH_HEADER_LEN + CLKS_NET_IP_HEADER_MIN_LEN)) {
        return;
    }

    eth = (const struct clks_net_eth_hdr *)(const void *)frame;
    ip = (const struct clks_net_ipv4_hdr *)(const void *)(frame + CLKS_NET_ETH_HEADER_LEN);
    ip_len = (u16)(frame_len - CLKS_NET_ETH_HEADER_LEN);

    if ((ip->ver_ihl >> 4U) != 4U) {
        return;
    }

    ihl_words = (u8)(ip->ver_ihl & 0x0FU);
    if (ihl_words < 5U) {
        return;
    }

    ihl_bytes = (u16)(ihl_words * 4U);
    if (ihl_bytes > ip_len) {
        return;
    }

    total_len = clks_net_read_be16(ip->total_len);
    if (total_len < ihl_bytes || total_len > ip_len) {
        return;
    }

    dst_ipv4_be = clks_net_read_be32(ip->dst);
    if (dst_ipv4_be != clks_net_ipv4_be && dst_ipv4_be != 0xFFFFFFFFU) {
        return;
    }

    src_ipv4_be = clks_net_read_be32(ip->src);
    if (src_ipv4_be == 0U) {
        return;
    }

    clks_net_arp_upsert(src_ipv4_be, eth->src);

    frag = clks_net_read_be16(ip->flags_frag);
    if ((frag & 0x3FFFU) != 0U) {
        return;
    }

    l4 = frame + CLKS_NET_ETH_HEADER_LEN + ihl_bytes;
    l4_len = (u16)(total_len - ihl_bytes);

    if (ip->proto == CLKS_NET_IPV4_PROTO_ICMP) {
        clks_net_process_icmp(src_ipv4_be, eth->src, l4, l4_len);
        return;
    }

    if (ip->proto == CLKS_NET_IPV4_PROTO_UDP) {
        clks_net_process_udp(src_ipv4_be, l4, l4_len);
        return;
    }
}

static void clks_net_process_arp(const u8 *frame, u16 frame_len) {
    const struct clks_net_eth_hdr *eth;
    const struct clks_net_arp_packet *arp;
    u16 op;
    u32 sender_ip_be;
    u32 target_ip_be;

    if (frame == CLKS_NULL || frame_len < (CLKS_NET_ETH_HEADER_LEN + sizeof(struct clks_net_arp_packet))) {
        return;
    }

    eth = (const struct clks_net_eth_hdr *)(const void *)frame;
    arp = (const struct clks_net_arp_packet *)(const void *)(frame + CLKS_NET_ETH_HEADER_LEN);

    if (clks_net_read_be16(arp->htype) != CLKS_NET_ARP_HTYPE_ETHERNET) {
        return;
    }
    if (clks_net_read_be16(arp->ptype) != CLKS_NET_ETHERTYPE_IPV4) {
        return;
    }
    if (arp->hlen != 6U || arp->plen != 4U) {
        return;
    }

    op = clks_net_read_be16(arp->oper);
    sender_ip_be = clks_net_read_be32(arp->spa);
    target_ip_be = clks_net_read_be32(arp->tpa);

    if (sender_ip_be != 0U && clks_net_mac_invalid(arp->sha) == CLKS_FALSE) {
        clks_net_arp_upsert(sender_ip_be, arp->sha);
    }

    if (op == CLKS_NET_ARP_OP_REQUEST && target_ip_be == clks_net_ipv4_be) {
        (void)clks_net_send_arp_packet(CLKS_NET_ARP_OP_REPLY, eth->src, sender_ip_be);
    }
}

static void clks_net_process_frame(const u8 *frame, u16 frame_len) {
    const struct clks_net_eth_hdr *eth;
    u16 ethertype;

    if (frame == CLKS_NULL || frame_len < CLKS_NET_ETH_HEADER_LEN) {
        return;
    }

    eth = (const struct clks_net_eth_hdr *)(const void *)frame;
    if (clks_net_mac_equal(eth->dst, clks_net_mac) == CLKS_FALSE &&
        clks_net_mac_equal(eth->dst, clks_net_mac_broadcast) == CLKS_FALSE) {
        return;
    }

    ethertype = clks_net_read_be16(eth->ethertype);
    if (ethertype == CLKS_NET_ETHERTYPE_ARP) {
        clks_net_process_arp(frame, frame_len);
        return;
    }
    if (ethertype == CLKS_NET_ETHERTYPE_IPV4) {
        clks_net_process_ipv4(frame, frame_len);
        return;
    }
}

static void clks_net_poll_internal(void) {
    u32 processed = 0U;

    if (clks_net_hw_ready == CLKS_FALSE || clks_net_rx_descs == CLKS_NULL) {
        return;
    }

    while (processed < CLKS_NET_DESC_COUNT) {
        volatile struct e1000_rx_desc *desc = &clks_net_rx_descs[clks_net_rx_index];
        u16 len;

        if ((desc->status & 0x01U) == 0U) {
            break;
        }

        len = desc->length;
        if (len > 0U && len <= CLKS_NET_BUF_SIZE) {
            clks_net_process_frame(clks_net_rx_buf_virt[clks_net_rx_index], len);
        }

        desc->status = 0U;
        desc->length = 0U;
        clks_net_reg_write(E1000_REG_RXDESCTAIL, clks_net_rx_index);
        clks_net_rx_index = (clks_net_rx_index + 1U) % CLKS_NET_DESC_COUNT;
        processed++;
    }
}

void clks_net_init(void) {
    clks_memset(clks_net_mac, 0, sizeof(clks_net_mac));
    clks_memset((void *)clks_net_rx_buf_virt, 0, sizeof(clks_net_rx_buf_virt));
    clks_memset((void *)clks_net_tx_buf_virt, 0, sizeof(clks_net_tx_buf_virt));
    clks_memset((void *)clks_net_rx_buf_phys, 0, sizeof(clks_net_rx_buf_phys));
    clks_memset((void *)clks_net_tx_buf_phys, 0, sizeof(clks_net_tx_buf_phys));

    clks_net_reg_mode = CLKS_NET_REG_NONE;
    clks_net_mmio_base = CLKS_NULL;
    clks_net_io_base = 0U;
    clks_net_rx_descs = CLKS_NULL;
    clks_net_tx_descs = CLKS_NULL;
    clks_net_rx_descs_phys = 0ULL;
    clks_net_tx_descs_phys = 0ULL;
    clks_net_rx_index = 0U;
    clks_net_tx_index = 0U;

    clks_net_arp_table_reset();
    clks_net_udp_queue_reset();
    clks_net_dhcp_state_reset();

    clks_net_ipv4_be = CLKS_NET_DEFAULT_IP_BE;
    clks_net_netmask_be = CLKS_NET_DEFAULT_NETMASK_BE;
    clks_net_gateway_be = CLKS_NET_DEFAULT_GATEWAY_BE;
    clks_net_dns_be = CLKS_NET_DEFAULT_DNS_BE;
    clks_net_ipv4_ident = 1U;
    clks_net_ping_seq = 0U;
    clks_net_ping_waiting = CLKS_FALSE;
    clks_net_ping_reply_ok = CLKS_FALSE;
    clks_net_ping_wait_dst_be = 0U;
    clks_net_ping_wait_seq = 0U;

    clks_net_ready = CLKS_FALSE;
    clks_net_hw_ready = CLKS_FALSE;

    clks_log(CLKS_LOG_INFO, "NET", "INIT START");
    if (clks_net_hw_init() == CLKS_TRUE) {
        clks_net_hw_ready = CLKS_TRUE;
        if (clks_net_dhcp_autoconfigure() == CLKS_TRUE) {
            clks_log(CLKS_LOG_INFO, "NET", "DHCP CONFIG APPLIED");
        } else {
            clks_log(CLKS_LOG_WARN, "NET", "DHCP FAILED, USING FALLBACK");
            clks_net_apply_fallback_config();
        }
        clks_log_hex(CLKS_LOG_INFO, "NET", "IPV4_BE", clks_net_ipv4_be);
        clks_log_hex(CLKS_LOG_INFO, "NET", "NETMASK_BE", clks_net_netmask_be);
        clks_log_hex(CLKS_LOG_INFO, "NET", "GATEWAY_BE", clks_net_gateway_be);
        clks_log_hex(CLKS_LOG_INFO, "NET", "DNS_BE", clks_net_dns_be);
        clks_net_ready = CLKS_TRUE;
    }
}

void clks_net_poll(void) {
    clks_net_poll_internal();
}

clks_bool clks_net_available(void) {
    return (clks_net_ready == CLKS_TRUE && clks_net_hw_ready == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;
}

u32 clks_net_ipv4_addr_be(void) {
    if (clks_net_available() == CLKS_FALSE) {
        return 0U;
    }
    return clks_net_ipv4_be;
}

u32 clks_net_ipv4_netmask_be(void) {
    if (clks_net_available() == CLKS_FALSE) {
        return 0U;
    }
    return clks_net_netmask_be;
}

u32 clks_net_ipv4_gateway_be(void) {
    if (clks_net_available() == CLKS_FALSE) {
        return 0U;
    }
    return clks_net_gateway_be;
}

u32 clks_net_ipv4_dns_be(void) {
    if (clks_net_available() == CLKS_FALSE) {
        return 0U;
    }
    return clks_net_dns_be;
}

clks_bool clks_net_ping_ipv4(u32 dst_ipv4_be, u64 poll_budget) {
    u8 dst_mac[CLKS_NET_ETH_ADDR_LEN];
    u64 i;
    u64 resolve_budget;
    u16 seq;

    if (clks_net_available() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (dst_ipv4_be == 0U) {
        return CLKS_FALSE;
    }

    if (dst_ipv4_be == clks_net_ipv4_be) {
        return CLKS_TRUE;
    }

    if (poll_budget == 0ULL) {
        poll_budget = 300000ULL;
    }

    resolve_budget = poll_budget / 2ULL;
    if (resolve_budget < 20000ULL) {
        resolve_budget = 20000ULL;
    }

    if (clks_net_resolve_ipv4(dst_ipv4_be, dst_mac, resolve_budget) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    seq = ++clks_net_ping_seq;
    clks_net_ping_wait_dst_be = dst_ipv4_be;
    clks_net_ping_wait_seq = seq;
    clks_net_ping_reply_ok = CLKS_FALSE;
    clks_net_ping_waiting = CLKS_TRUE;

    if (clks_net_send_icmp_echo_request(dst_ipv4_be, dst_mac, seq) == CLKS_FALSE) {
        clks_net_ping_waiting = CLKS_FALSE;
        return CLKS_FALSE;
    }

    for (i = 0ULL; i < poll_budget; i++) {
        clks_net_poll_internal();

        if (clks_net_ping_reply_ok == CLKS_TRUE) {
            clks_net_ping_reply_ok = CLKS_FALSE;
            return CLKS_TRUE;
        }

        if ((i & CLKS_NET_POLL_SPIN_PAUSE_MASK) == 0ULL) {
            clks_cpu_pause();
        }
    }

    clks_net_ping_waiting = CLKS_FALSE;
    clks_net_ping_reply_ok = CLKS_FALSE;
    return CLKS_FALSE;
}

u64 clks_net_udp_send(u32 dst_ipv4_be, u16 dst_port, u16 src_port, const void *payload, u64 payload_len) {
    u8 dst_mac[CLKS_NET_ETH_ADDR_LEN];

    if (clks_net_available() == CLKS_FALSE) {
        return 0ULL;
    }

    if (dst_ipv4_be == 0U || dst_port == 0U || src_port == 0U) {
        return 0ULL;
    }

    if (payload_len > CLKS_NET_UDP_PAYLOAD_MAX) {
        return 0ULL;
    }

    if (payload_len > 0ULL && payload == CLKS_NULL) {
        return 0ULL;
    }

    if (dst_ipv4_be == 0xFFFFFFFFU) {
        clks_memcpy(dst_mac, clks_net_mac_broadcast, CLKS_NET_ETH_ADDR_LEN);
    } else {
        if (clks_net_resolve_ipv4(dst_ipv4_be, dst_mac, 120000ULL) == CLKS_FALSE) {
            return 0ULL;
        }
    }

    if (clks_net_send_udp_datagram(clks_net_ipv4_be, dst_ipv4_be, dst_mac, src_port, dst_port, payload,
                                   (u16)payload_len) == CLKS_FALSE) {
        return 0ULL;
    }

    return payload_len;
}

u64 clks_net_udp_recv(void *out_payload, u64 payload_capacity, u32 *out_src_ipv4_be, u16 *out_src_port,
                      u16 *out_dst_port) {
    struct clks_net_udp_packet *pkt;
    u64 copy_len;

    if (clks_net_available() == CLKS_FALSE || out_payload == CLKS_NULL || payload_capacity == 0ULL) {
        return 0ULL;
    }

    clks_net_poll_internal();

    if (clks_net_udp_count == 0U) {
        return 0ULL;
    }

    pkt = &clks_net_udp_queue[clks_net_udp_head];
    if (pkt->valid == CLKS_FALSE) {
        clks_net_udp_head = (clks_net_udp_head + 1U) % CLKS_NET_UDP_QUEUE_CAP;
        if (clks_net_udp_count > 0U) {
            clks_net_udp_count--;
        }
        return 0ULL;
    }

    copy_len = pkt->payload_len;
    if (copy_len > payload_capacity) {
        copy_len = payload_capacity;
    }

    if (copy_len > 0ULL) {
        clks_memcpy(out_payload, pkt->payload, (usize)copy_len);
    }

    if (out_src_ipv4_be != CLKS_NULL) {
        *out_src_ipv4_be = pkt->src_ipv4_be;
    }
    if (out_src_port != CLKS_NULL) {
        *out_src_port = pkt->src_port;
    }
    if (out_dst_port != CLKS_NULL) {
        *out_dst_port = pkt->dst_port;
    }

    pkt->valid = CLKS_FALSE;
    clks_net_udp_head = (clks_net_udp_head + 1U) % CLKS_NET_UDP_QUEUE_CAP;
    if (clks_net_udp_count > 0U) {
        clks_net_udp_count--;
    }

    return copy_len;
}

#else

void clks_net_init(void) {}

void clks_net_poll(void) {}

clks_bool clks_net_available(void) {
    return CLKS_FALSE;
}

u32 clks_net_ipv4_addr_be(void) {
    return 0U;
}

u32 clks_net_ipv4_netmask_be(void) {
    return 0U;
}

u32 clks_net_ipv4_gateway_be(void) {
    return 0U;
}

u32 clks_net_ipv4_dns_be(void) {
    return 0U;
}

clks_bool clks_net_ping_ipv4(u32 dst_ipv4_be, u64 poll_budget) {
    (void)dst_ipv4_be;
    (void)poll_budget;
    return CLKS_FALSE;
}

u64 clks_net_udp_send(u32 dst_ipv4_be, u16 dst_port, u16 src_port, const void *payload, u64 payload_len) {
    (void)dst_ipv4_be;
    (void)dst_port;
    (void)src_port;
    (void)payload;
    (void)payload_len;
    return 0ULL;
}

u64 clks_net_udp_recv(void *out_payload, u64 payload_capacity, u32 *out_src_ipv4_be, u16 *out_src_port,
                      u16 *out_dst_port) {
    (void)out_payload;
    (void)payload_capacity;
    (void)out_src_ipv4_be;
    (void)out_src_port;
    (void)out_dst_port;
    return 0ULL;
}

#endif
