#include <clks/disk.h>
#include <clks/heap.h>
#include <clks/log.h>
#include <clks/string.h>
#include <clks/types.h>

/* FAT32 needs at least 65525 clusters; 66581 sectors (512B) is the practical floor for our formatter. */
#define CLKS_DISK_MIN_BYTES (66581ULL * 512ULL)
#define CLKS_DISK_CACHE_MAX_BYTES (256ULL * 1024ULL * 1024ULL)
#define CLKS_DISK_ALLOC_RETRY_STEP_BYTES (8ULL * 1024ULL * 1024ULL)
#define CLKS_DISK_HEAP_RESERVE_BYTES (16ULL * 1024ULL * 1024ULL)

#define CLKS_DISK_FAT32_MIN_CLUSTER_COUNT 65525U
#define CLKS_DISK_FAT32_EOC_MIN 0x0FFFFFF8U
#define CLKS_DISK_FAT32_EOC 0x0FFFFFFFU
#define CLKS_DISK_FAT32_FREE_CLUSTER 0x00000000U

#define CLKS_DISK_FAT32_BOOT_SIG_OFFSET 510U
#define CLKS_DISK_FAT32_TYPE_OFFSET 82U
#define CLKS_DISK_FAT32_TYPE_LEN 5U

#define CLKS_DISK_DIRENT_SIZE 32U
#define CLKS_DISK_DIRENT_NAME_LEN 11U

#define CLKS_DISK_ATTR_READ_ONLY 0x01U
#define CLKS_DISK_ATTR_HIDDEN 0x02U
#define CLKS_DISK_ATTR_SYSTEM 0x04U
#define CLKS_DISK_ATTR_VOLUME_ID 0x08U
#define CLKS_DISK_ATTR_DIRECTORY 0x10U
#define CLKS_DISK_ATTR_ARCHIVE 0x20U
#define CLKS_DISK_ATTR_LFN 0x0FU

#define CLKS_DISK_INVALID_CLUSTER 0x00000000U

#if defined(CLKS_ARCH_X86_64)
#define CLKS_DISK_ATA_PRIMARY_IO_BASE 0x1F0U
#define CLKS_DISK_ATA_PRIMARY_CTRL_BASE 0x3F6U
#define CLKS_DISK_ATA_SECONDARY_IO_BASE 0x170U
#define CLKS_DISK_ATA_SECONDARY_CTRL_BASE 0x376U
#define CLKS_DISK_ATA_DRIVE_MASTER 0xA0U
#define CLKS_DISK_ATA_DRIVE_SLAVE 0xB0U
#define CLKS_DISK_ATA_DRIVE_LBA_MASTER 0xE0U
#define CLKS_DISK_ATA_DRIVE_LBA_SLAVE 0xF0U

#define CLKS_DISK_ATA_STATUS_ERR 0x01U
#define CLKS_DISK_ATA_STATUS_DRQ 0x08U
#define CLKS_DISK_ATA_STATUS_DF 0x20U
#define CLKS_DISK_ATA_STATUS_DRDY 0x40U
#define CLKS_DISK_ATA_STATUS_BSY 0x80U

#define CLKS_DISK_ATA_CMD_READ_SECTORS 0x20U
#define CLKS_DISK_ATA_CMD_WRITE_SECTORS 0x30U
#define CLKS_DISK_ATA_CMD_CACHE_FLUSH 0xE7U
#define CLKS_DISK_ATA_CMD_IDENTIFY 0xECU
#endif

struct clks_disk_fat32_layout {
    clks_bool valid;
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u32 sectors_per_fat;
    u32 total_sectors;
    u32 root_cluster;
    u32 cluster_count;
    u32 cluster_size_bytes;
    u32 entries_per_cluster;
    u64 fat_lba;
    u64 data_lba;
};

struct clks_disk_dir_slot {
    u32 cluster;
    u32 entry_index;
};

struct clks_disk_dir_info {
    u8 attr;
    u32 first_cluster;
    u32 size;
};

struct clks_disk_path_lookup {
    clks_bool found;
    u32 parent_cluster;
    u8 sfn[CLKS_DISK_DIRENT_NAME_LEN];
    struct clks_disk_dir_slot slot;
    struct clks_disk_dir_info info;
};

static u8 *clks_disk_bytes = CLKS_NULL;
static u64 clks_disk_bytes_len = 0ULL;
static u64 clks_disk_sector_total = 0ULL;
static clks_bool clks_disk_ready = CLKS_FALSE;
static clks_bool clks_disk_formatted = CLKS_FALSE;
static clks_bool clks_disk_mounted = CLKS_FALSE;
static clks_bool clks_disk_hw_backed = CLKS_FALSE;
static char clks_disk_mount_path_buf[CLKS_DISK_PATH_MAX];
static struct clks_disk_fat32_layout clks_disk_fat32;
static u32 clks_disk_alloc_hint = 2U;

static void *clks_disk_read_cache = CLKS_NULL;
static u64 clks_disk_read_cache_size = 0ULL;
static const u8 clks_disk_empty_file_data[1] = {0U};

#if defined(CLKS_ARCH_X86_64)
static u16 clks_disk_ata_io_base = CLKS_DISK_ATA_PRIMARY_IO_BASE;
static u16 clks_disk_ata_ctrl_base = CLKS_DISK_ATA_PRIMARY_CTRL_BASE;
static u8 clks_disk_ata_drive_select = CLKS_DISK_ATA_DRIVE_MASTER;
static u8 clks_disk_ata_drive_lba = CLKS_DISK_ATA_DRIVE_LBA_MASTER;
#endif

static u16 clks_disk_read_u16(const u8 *ptr) {
    return (u16)((u16)ptr[0] | ((u16)ptr[1] << 8U));
}

static u32 clks_disk_read_u32(const u8 *ptr) {
    return (u32)((u32)ptr[0] | ((u32)ptr[1] << 8U) | ((u32)ptr[2] << 16U) | ((u32)ptr[3] << 24U));
}

static void clks_disk_write_u16(u8 *ptr, u16 value) {
    ptr[0] = (u8)(value & 0xFFU);
    ptr[1] = (u8)((value >> 8U) & 0xFFU);
}

static void clks_disk_write_u32(u8 *ptr, u32 value) {
    ptr[0] = (u8)(value & 0xFFU);
    ptr[1] = (u8)((value >> 8U) & 0xFFU);
    ptr[2] = (u8)((value >> 16U) & 0xFFU);
    ptr[3] = (u8)((value >> 24U) & 0xFFU);
}

#if defined(CLKS_ARCH_X86_64)
static inline u16 clks_disk_ata_reg_data(void) {
    return (u16)(clks_disk_ata_io_base + 0U);
}

static inline u16 clks_disk_ata_reg_sector_count(void) {
    return (u16)(clks_disk_ata_io_base + 2U);
}

static inline u16 clks_disk_ata_reg_lba_low(void) {
    return (u16)(clks_disk_ata_io_base + 3U);
}

static inline u16 clks_disk_ata_reg_lba_mid(void) {
    return (u16)(clks_disk_ata_io_base + 4U);
}

static inline u16 clks_disk_ata_reg_lba_high(void) {
    return (u16)(clks_disk_ata_io_base + 5U);
}

static inline u16 clks_disk_ata_reg_drive(void) {
    return (u16)(clks_disk_ata_io_base + 6U);
}

static inline u16 clks_disk_ata_reg_status(void) {
    return (u16)(clks_disk_ata_io_base + 7U);
}

static inline u16 clks_disk_ata_reg_command(void) {
    return (u16)(clks_disk_ata_io_base + 7U);
}

static inline u16 clks_disk_ata_reg_alt_status(void) {
    return (u16)(clks_disk_ata_ctrl_base + 0U);
}

static void clks_disk_ata_select_device(u16 io_base, u16 ctrl_base, u8 drive_select, u8 drive_lba) {
    clks_disk_ata_io_base = io_base;
    clks_disk_ata_ctrl_base = ctrl_base;
    clks_disk_ata_drive_select = drive_select;
    clks_disk_ata_drive_lba = drive_lba;
}

static inline void clks_disk_ata_outb(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 clks_disk_ata_inb(u16 port) {
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void clks_disk_ata_outw(u16 port, u16 value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline u16 clks_disk_ata_inw(u16 port) {
    u16 value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void clks_disk_ata_delay_400ns(void) {
    (void)clks_disk_ata_inb(clks_disk_ata_reg_alt_status());
    (void)clks_disk_ata_inb(clks_disk_ata_reg_alt_status());
    (void)clks_disk_ata_inb(clks_disk_ata_reg_alt_status());
    (void)clks_disk_ata_inb(clks_disk_ata_reg_alt_status());
}

static clks_bool clks_disk_ata_wait_not_busy(void) {
    u32 i;

    for (i = 0U; i < 1000000U; i++) {
        u8 status = clks_disk_ata_inb(clks_disk_ata_reg_status());

        if ((status & CLKS_DISK_ATA_STATUS_BSY) == 0U) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_wait_drq_ready(void) {
    u32 i;

    for (i = 0U; i < 1000000U; i++) {
        u8 status = clks_disk_ata_inb(clks_disk_ata_reg_status());

        if ((status & CLKS_DISK_ATA_STATUS_BSY) != 0U) {
            continue;
        }

        if ((status & (CLKS_DISK_ATA_STATUS_ERR | CLKS_DISK_ATA_STATUS_DF)) != 0U) {
            return CLKS_FALSE;
        }

        if ((status & CLKS_DISK_ATA_STATUS_DRQ) != 0U) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_wait_ready_no_drq(void) {
    u32 i;

    for (i = 0U; i < 1000000U; i++) {
        u8 status = clks_disk_ata_inb(clks_disk_ata_reg_status());

        if ((status & CLKS_DISK_ATA_STATUS_BSY) != 0U) {
            continue;
        }

        if ((status & (CLKS_DISK_ATA_STATUS_ERR | CLKS_DISK_ATA_STATUS_DF)) != 0U) {
            return CLKS_FALSE;
        }

        if ((status & CLKS_DISK_ATA_STATUS_DRDY) != 0U) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_identify(u64 *out_sector_total) {
    u16 identify_words[256];
    u32 i;
    u64 sector_total;
    u8 status;
    u8 sig_mid;
    u8 sig_high;

    if (out_sector_total == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_outb(clks_disk_ata_reg_drive(), clks_disk_ata_drive_select);
    clks_disk_ata_delay_400ns();

    clks_disk_ata_outb(clks_disk_ata_reg_sector_count(), 0U);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_low(), 0U);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_mid(), 0U);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_high(), 0U);
    clks_disk_ata_outb(clks_disk_ata_reg_command(), CLKS_DISK_ATA_CMD_IDENTIFY);

    status = clks_disk_ata_inb(clks_disk_ata_reg_status());
    if (status == 0U || status == 0xFFU) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_delay_400ns();
    sig_mid = clks_disk_ata_inb(clks_disk_ata_reg_lba_mid());
    sig_high = clks_disk_ata_inb(clks_disk_ata_reg_lba_high());
    if (sig_mid != 0U || sig_high != 0U) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_drq_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < 256U; i++) {
        identify_words[i] = clks_disk_ata_inw(clks_disk_ata_reg_data());
    }

    sector_total = (u64)identify_words[60] | ((u64)identify_words[61] << 16U);
    if (sector_total == 0ULL) {
        /* Some devices report 28-bit size as 0 and only provide 48-bit LBA size. */
        sector_total = (u64)identify_words[100] | ((u64)identify_words[101] << 16U) |
                       ((u64)identify_words[102] << 32U) | ((u64)identify_words[103] << 48U);
    }

    if (sector_total == 0ULL) {
        return CLKS_FALSE;
    }

    *out_sector_total = sector_total;
    return CLKS_TRUE;
}

static clks_bool clks_disk_ata_probe(u64 *out_sector_total) {
    static const struct {
        u16 io_base;
        u16 ctrl_base;
        u8 drive_select;
        u8 drive_lba;
    } probes[] = {
        {CLKS_DISK_ATA_PRIMARY_IO_BASE, CLKS_DISK_ATA_PRIMARY_CTRL_BASE, CLKS_DISK_ATA_DRIVE_MASTER,
         CLKS_DISK_ATA_DRIVE_LBA_MASTER},
        {CLKS_DISK_ATA_PRIMARY_IO_BASE, CLKS_DISK_ATA_PRIMARY_CTRL_BASE, CLKS_DISK_ATA_DRIVE_SLAVE,
         CLKS_DISK_ATA_DRIVE_LBA_SLAVE},
        {CLKS_DISK_ATA_SECONDARY_IO_BASE, CLKS_DISK_ATA_SECONDARY_CTRL_BASE, CLKS_DISK_ATA_DRIVE_MASTER,
         CLKS_DISK_ATA_DRIVE_LBA_MASTER},
        {CLKS_DISK_ATA_SECONDARY_IO_BASE, CLKS_DISK_ATA_SECONDARY_CTRL_BASE, CLKS_DISK_ATA_DRIVE_SLAVE,
         CLKS_DISK_ATA_DRIVE_LBA_SLAVE},
    };
    u32 i;

    if (out_sector_total == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < (u32)(sizeof(probes) / sizeof(probes[0])); i++) {
        u64 sectors = 0ULL;

        clks_disk_ata_select_device(probes[i].io_base, probes[i].ctrl_base, probes[i].drive_select,
                                    probes[i].drive_lba);
        if (clks_disk_ata_identify(&sectors) == CLKS_TRUE && sectors != 0ULL) {
            *out_sector_total = sectors;
            clks_log(CLKS_LOG_INFO, "DISK", "ATA DISK DETECTED");
            clks_log_hex(CLKS_LOG_INFO, "DISK", "ATA_IO_BASE", probes[i].io_base);
            clks_log_hex(CLKS_LOG_INFO, "DISK", "ATA_CTRL_BASE", probes[i].ctrl_base);
            clks_log_hex(CLKS_LOG_INFO, "DISK", "ATA_DRIVE", probes[i].drive_select);
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_read_sector_hw(u64 lba, void *out_sector) {
    u8 *out = (u8 *)out_sector;
    u32 i;
    u8 status;

    if (out_sector == CLKS_NULL || lba > 0x0FFFFFFFULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_outb(clks_disk_ata_reg_drive(), (u8)(clks_disk_ata_drive_lba | ((u8)((lba >> 24U) & 0x0FU))));
    clks_disk_ata_outb(clks_disk_ata_reg_sector_count(), 1U);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_low(), (u8)(lba & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_lba_mid(), (u8)((lba >> 8U) & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_lba_high(), (u8)((lba >> 16U) & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_command(), CLKS_DISK_ATA_CMD_READ_SECTORS);

    if (clks_disk_ata_wait_drq_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < 256U; i++) {
        u16 word = clks_disk_ata_inw(clks_disk_ata_reg_data());
        out[i * 2U] = (u8)(word & 0x00FFU);
        out[i * 2U + 1U] = (u8)((word >> 8U) & 0x00FFU);
    }

    clks_disk_ata_delay_400ns();
    status = clks_disk_ata_inb(clks_disk_ata_reg_status());
    if ((status & (CLKS_DISK_ATA_STATUS_ERR | CLKS_DISK_ATA_STATUS_DF)) != 0U) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_ata_write_sector_hw(u64 lba, const void *sector_data) {
    const u8 *src = (const u8 *)sector_data;
    u32 i;

    if (sector_data == CLKS_NULL || lba > 0x0FFFFFFFULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_outb(clks_disk_ata_reg_drive(), (u8)(clks_disk_ata_drive_lba | ((u8)((lba >> 24U) & 0x0FU))));
    clks_disk_ata_outb(clks_disk_ata_reg_sector_count(), 1U);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_low(), (u8)(lba & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_lba_mid(), (u8)((lba >> 8U) & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_lba_high(), (u8)((lba >> 16U) & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_command(), CLKS_DISK_ATA_CMD_WRITE_SECTORS);

    if (clks_disk_ata_wait_drq_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < 256U; i++) {
        u16 word = (u16)src[i * 2U] | ((u16)src[i * 2U + 1U] << 8U);
        clks_disk_ata_outw(clks_disk_ata_reg_data(), word);
    }

    clks_disk_ata_delay_400ns();
    if (clks_disk_ata_wait_ready_no_drq() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_ata_cache_flush_hw(void) {
    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_outb(clks_disk_ata_reg_command(), CLKS_DISK_ATA_CMD_CACHE_FLUSH);
    return clks_disk_ata_wait_ready_no_drq();
}
#else
static clks_bool clks_disk_ata_identify(u64 *out_sector_total) {
    (void)out_sector_total;
    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_probe(u64 *out_sector_total) {
    return clks_disk_ata_identify(out_sector_total);
}

static clks_bool clks_disk_ata_read_sector_hw(u64 lba, void *out_sector) {
    (void)lba;
    (void)out_sector;
    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_write_sector_hw(u64 lba, const void *sector_data) {
    (void)lba;
    (void)sector_data;
    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_cache_flush_hw(void) {
    return CLKS_FALSE;
}
#endif

static clks_bool clks_disk_bytes_range_valid(u64 offset, u64 size) {
    if (clks_disk_bytes == CLKS_NULL || clks_disk_bytes_len == 0ULL) {
        return CLKS_FALSE;
    }

    if (offset > clks_disk_bytes_len) {
        return CLKS_FALSE;
    }

    if (size > (clks_disk_bytes_len - offset)) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_sync_sector_range(u64 start_lba, u64 sector_count) {
    u64 end_lba;
    u64 lba;

    if (sector_count == 0ULL) {
        return CLKS_TRUE;
    }

    if (clks_disk_hw_backed == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (start_lba >= clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    if (sector_count > (clks_disk_sector_total - start_lba)) {
        return CLKS_FALSE;
    }

    end_lba = start_lba + sector_count;
    for (lba = start_lba; lba < end_lba; lba++) {
        const u8 *sector_ptr = clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE);

        if (clks_disk_ata_write_sector_hw(lba, sector_ptr) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return clks_disk_ata_cache_flush_hw();
}

static clks_bool clks_disk_sync_bytes_to_hw(u64 used_bytes) {
    u64 sectors_to_sync;
    u64 lba;

    if (clks_disk_hw_backed == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (clks_disk_bytes == CLKS_NULL || clks_disk_sector_total == 0ULL) {
        return CLKS_FALSE;
    }

    if (used_bytes == 0ULL) {
        return CLKS_TRUE;
    }

    if (used_bytes > clks_disk_bytes_len) {
        used_bytes = clks_disk_bytes_len;
    }

    sectors_to_sync = (used_bytes + (CLKS_DISK_SECTOR_SIZE - 1ULL)) / CLKS_DISK_SECTOR_SIZE;
    if (sectors_to_sync > clks_disk_sector_total) {
        sectors_to_sync = clks_disk_sector_total;
    }

    for (lba = 0ULL; lba < sectors_to_sync; lba++) {
        const u8 *sector_ptr = clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE);
        if (clks_disk_ata_write_sector_hw(lba, sector_ptr) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    if (clks_disk_ata_cache_flush_hw() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_bytes_equal(const u8 *left, const u8 *right, usize count) {
    usize i;

    if (left == CLKS_NULL || right == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < count; i++) {
        if (left[i] != right[i]) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_text_prefix_equals(const char *text, const char *prefix, usize prefix_len) {
    usize i;

    if (text == CLKS_NULL || prefix == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < prefix_len; i++) {
        if (text[i] != prefix[i]) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static void clks_disk_copy_text(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || dst_size == 0U) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1U < dst_size) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static clks_bool clks_disk_normalize_absolute_path(const char *path, char *out_path, usize out_size) {
    usize in_pos = 0U;
    usize out_pos = 0U;

    if (path == CLKS_NULL || out_path == CLKS_NULL || out_size < 2U) {
        return CLKS_FALSE;
    }

    if (path[0] != '/') {
        return CLKS_FALSE;
    }

    out_path[out_pos++] = '/';

    while (path[in_pos] == '/') {
        in_pos++;
    }

    while (path[in_pos] != '\0') {
        usize comp_start = in_pos;
        usize comp_len;

        while (path[in_pos] != '\0' && path[in_pos] != '/') {
            in_pos++;
        }

        comp_len = in_pos - comp_start;

        if (comp_len == 0U) {
            while (path[in_pos] == '/') {
                in_pos++;
            }
            continue;
        }

        if (comp_len == 1U && path[comp_start] == '.') {
            while (path[in_pos] == '/') {
                in_pos++;
            }
            continue;
        }

        if (comp_len == 2U && path[comp_start] == '.' && path[comp_start + 1U] == '.') {
            return CLKS_FALSE;
        }

        if (out_pos > 1U) {
            if (out_pos + 1U >= out_size) {
                return CLKS_FALSE;
            }
            out_path[out_pos++] = '/';
        }

        if (out_pos + comp_len >= out_size) {
            return CLKS_FALSE;
        }

        clks_memcpy(out_path + out_pos, path + comp_start, comp_len);
        out_pos += comp_len;

        while (path[in_pos] == '/') {
            in_pos++;
        }
    }

    out_path[out_pos] = '\0';
    return CLKS_TRUE;
}

static clks_bool clks_disk_normalize_relative_path(const char *path, char *out_path, usize out_size) {
    usize in_pos = 0U;
    usize out_pos = 0U;

    if (path == CLKS_NULL || out_path == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    if (path[0] == '/') {
        return CLKS_FALSE;
    }

    while (path[in_pos] == '/') {
        in_pos++;
    }

    while (path[in_pos] != '\0') {
        usize comp_start = in_pos;
        usize comp_len;

        while (path[in_pos] != '\0' && path[in_pos] != '/') {
            in_pos++;
        }

        comp_len = in_pos - comp_start;

        if (comp_len == 0U) {
            while (path[in_pos] == '/') {
                in_pos++;
            }
            continue;
        }

        if (comp_len == 1U && path[comp_start] == '.') {
            while (path[in_pos] == '/') {
                in_pos++;
            }
            continue;
        }

        if (comp_len == 2U && path[comp_start] == '.' && path[comp_start + 1U] == '.') {
            return CLKS_FALSE;
        }

        if (out_pos != 0U) {
            if (out_pos + 1U >= out_size) {
                return CLKS_FALSE;
            }
            out_path[out_pos++] = '/';
        }

        if (out_pos + comp_len >= out_size) {
            return CLKS_FALSE;
        }

        clks_memcpy(out_path + out_pos, path + comp_start, comp_len);
        out_pos += comp_len;

        while (path[in_pos] == '/') {
            in_pos++;
        }
    }

    out_path[out_pos] = '\0';
    return CLKS_TRUE;
}

static clks_bool clks_disk_path_to_relative(const char *path, char *out_relative, usize out_size) {
    char normalized[CLKS_DISK_PATH_MAX];
    usize mount_len;

    if (out_relative == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    if (clks_disk_mounted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_normalize_absolute_path(path, normalized, sizeof(normalized)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    mount_len = clks_strlen(clks_disk_mount_path_buf);

    if (clks_strcmp(normalized, clks_disk_mount_path_buf) == 0) {
        out_relative[0] = '\0';
        return CLKS_TRUE;
    }

    if (mount_len >= clks_strlen(normalized)) {
        return CLKS_FALSE;
    }

    if (clks_disk_text_prefix_equals(normalized, clks_disk_mount_path_buf, mount_len) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (normalized[mount_len] != '/') {
        return CLKS_FALSE;
    }

    return clks_disk_normalize_relative_path(normalized + mount_len + 1U, out_relative, out_size);
}

static clks_bool clks_disk_parse_fat32_layout(void) {
    const u8 *boot;
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u32 sectors_per_fat;
    u32 total_sectors_16;
    u32 total_sectors_32;
    u32 total_sectors;
    u32 root_cluster;
    u64 data_sectors;
    u64 cluster_count;
    const u8 fat32_sig[CLKS_DISK_FAT32_TYPE_LEN] = {'F', 'A', 'T', '3', '2'};

    clks_memset(&clks_disk_fat32, 0, sizeof(clks_disk_fat32));
    clks_disk_alloc_hint = 2U;

    if (clks_disk_ready == CLKS_FALSE || clks_disk_bytes == CLKS_NULL || clks_disk_bytes_len < CLKS_DISK_SECTOR_SIZE) {
        return CLKS_FALSE;
    }

    boot = clks_disk_bytes;

    if (boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET] != 0x55U || boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET + 1U] != 0xAAU) {
        return CLKS_FALSE;
    }

    bytes_per_sector = clks_disk_read_u16(boot + 11U);
    sectors_per_cluster = boot[13U];
    reserved_sectors = clks_disk_read_u16(boot + 14U);
    fat_count = boot[16U];
    sectors_per_fat = clks_disk_read_u32(boot + 36U);
    root_cluster = clks_disk_read_u32(boot + 44U);
    total_sectors_16 = (u32)clks_disk_read_u16(boot + 19U);
    total_sectors_32 = clks_disk_read_u32(boot + 32U);
    total_sectors = (total_sectors_16 != 0U) ? total_sectors_16 : total_sectors_32;

    if (bytes_per_sector != (u16)CLKS_DISK_SECTOR_SIZE) {
        return CLKS_FALSE;
    }

    if (sectors_per_cluster == 0U || (sectors_per_cluster & (sectors_per_cluster - 1U)) != 0U) {
        return CLKS_FALSE;
    }

    if (reserved_sectors == 0U || fat_count < 1U || sectors_per_fat == 0U || total_sectors == 0U) {
        return CLKS_FALSE;
    }

    if (root_cluster < 2U) {
        return CLKS_FALSE;
    }

    if (clks_disk_bytes_equal(boot + CLKS_DISK_FAT32_TYPE_OFFSET, fat32_sig, CLKS_DISK_FAT32_TYPE_LEN) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if ((u64)total_sectors > clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    if ((u64)total_sectors <= (u64)reserved_sectors + ((u64)fat_count * (u64)sectors_per_fat)) {
        return CLKS_FALSE;
    }

    data_sectors = (u64)total_sectors - (u64)reserved_sectors - ((u64)fat_count * (u64)sectors_per_fat);
    cluster_count = data_sectors / (u64)sectors_per_cluster;

    if (cluster_count < (u64)CLKS_DISK_FAT32_MIN_CLUSTER_COUNT || cluster_count > 0x0FFFFFF5ULL) {
        return CLKS_FALSE;
    }

    if (root_cluster >= (u32)(cluster_count + 2ULL)) {
        return CLKS_FALSE;
    }

    clks_disk_fat32.valid = CLKS_TRUE;
    clks_disk_fat32.bytes_per_sector = bytes_per_sector;
    clks_disk_fat32.sectors_per_cluster = sectors_per_cluster;
    clks_disk_fat32.reserved_sectors = reserved_sectors;
    clks_disk_fat32.fat_count = fat_count;
    clks_disk_fat32.sectors_per_fat = sectors_per_fat;
    clks_disk_fat32.total_sectors = total_sectors;
    clks_disk_fat32.root_cluster = root_cluster;
    clks_disk_fat32.cluster_count = (u32)cluster_count;
    clks_disk_fat32.cluster_size_bytes = (u32)((u32)sectors_per_cluster * (u32)CLKS_DISK_SECTOR_SIZE);
    clks_disk_fat32.entries_per_cluster = clks_disk_fat32.cluster_size_bytes / CLKS_DISK_DIRENT_SIZE;
    clks_disk_fat32.fat_lba = (u64)reserved_sectors;
    clks_disk_fat32.data_lba = (u64)reserved_sectors + ((u64)fat_count * (u64)sectors_per_fat);

    return CLKS_TRUE;
}

static clks_bool clks_disk_cluster_valid(u32 cluster) {
    if (clks_disk_fat32.valid == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return (cluster >= 2U && cluster < (clks_disk_fat32.cluster_count + 2U)) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_disk_cluster_lba(u32 cluster, u64 *out_lba) {
    u64 lba;

    if (out_lba == CLKS_NULL || clks_disk_cluster_valid(cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    lba = clks_disk_fat32.data_lba + ((u64)(cluster - 2U) * (u64)clks_disk_fat32.sectors_per_cluster);

    if (lba >= clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    if ((u64)clks_disk_fat32.sectors_per_cluster > (clks_disk_sector_total - lba)) {
        return CLKS_FALSE;
    }

    *out_lba = lba;
    return CLKS_TRUE;
}

static clks_bool clks_disk_fat_is_eoc(u32 value) {
    return ((value & 0x0FFFFFFFU) >= CLKS_DISK_FAT32_EOC_MIN) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_disk_fat_read_entry(u32 cluster, u32 *out_value) {
    u64 fat_byte_offset;
    u64 fat_sector_index;
    u64 fat_sector_offset;
    u64 fat_lba;
    u64 disk_offset;
    u32 value;

    if (out_value == CLKS_NULL || clks_disk_fat32.valid == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (cluster >= (clks_disk_fat32.cluster_count + 2U)) {
        return CLKS_FALSE;
    }

    fat_byte_offset = (u64)cluster * 4ULL;
    fat_sector_index = fat_byte_offset / CLKS_DISK_SECTOR_SIZE;
    fat_sector_offset = fat_byte_offset % CLKS_DISK_SECTOR_SIZE;

    if (fat_sector_index >= (u64)clks_disk_fat32.sectors_per_fat) {
        return CLKS_FALSE;
    }

    fat_lba = clks_disk_fat32.fat_lba + fat_sector_index;
    disk_offset = fat_lba * CLKS_DISK_SECTOR_SIZE + fat_sector_offset;

    if (clks_disk_bytes_range_valid(disk_offset, 4ULL) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    value = clks_disk_read_u32(clks_disk_bytes + (usize)disk_offset);
    *out_value = value & 0x0FFFFFFFU;
    return CLKS_TRUE;
}

static clks_bool clks_disk_fat_write_entry(u32 cluster, u32 value) {
    u64 fat_byte_offset;
    u64 fat_sector_index;
    u64 fat_sector_offset;
    u8 fat_index;

    if (clks_disk_fat32.valid == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (cluster >= (clks_disk_fat32.cluster_count + 2U)) {
        return CLKS_FALSE;
    }

    fat_byte_offset = (u64)cluster * 4ULL;
    fat_sector_index = fat_byte_offset / CLKS_DISK_SECTOR_SIZE;
    fat_sector_offset = fat_byte_offset % CLKS_DISK_SECTOR_SIZE;

    if (fat_sector_index >= (u64)clks_disk_fat32.sectors_per_fat) {
        return CLKS_FALSE;
    }

    for (fat_index = 0U; fat_index < clks_disk_fat32.fat_count; fat_index++) {
        u64 fat_lba = clks_disk_fat32.fat_lba + ((u64)fat_index * (u64)clks_disk_fat32.sectors_per_fat) + fat_sector_index;
        u64 disk_offset = fat_lba * CLKS_DISK_SECTOR_SIZE + fat_sector_offset;
        u32 old_value;
        u32 new_value;

        if (clks_disk_bytes_range_valid(disk_offset, 4ULL) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        old_value = clks_disk_read_u32(clks_disk_bytes + (usize)disk_offset);
        new_value = (old_value & 0xF0000000U) | (value & 0x0FFFFFFFU);
        clks_disk_write_u32(clks_disk_bytes + (usize)disk_offset, new_value);

        if (clks_disk_sync_sector_range(fat_lba, 1ULL) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_fat_next_cluster(u32 cluster, u32 *out_next, clks_bool *out_eoc) {
    u32 value;

    if (out_next == CLKS_NULL || out_eoc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_fat_read_entry(cluster, &value) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_eoc = clks_disk_fat_is_eoc(value);
    *out_next = value & 0x0FFFFFFFU;
    return CLKS_TRUE;
}

static clks_bool clks_disk_zero_cluster(u32 cluster) {
    u64 lba;
    u64 offset;
    u64 byte_count;

    if (clks_disk_cluster_lba(cluster, &lba) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    offset = lba * CLKS_DISK_SECTOR_SIZE;
    byte_count = (u64)clks_disk_fat32.sectors_per_cluster * CLKS_DISK_SECTOR_SIZE;

    if (clks_disk_bytes_range_valid(offset, byte_count) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_memset(clks_disk_bytes + (usize)offset, 0, (usize)byte_count);
    return clks_disk_sync_sector_range(lba, (u64)clks_disk_fat32.sectors_per_cluster);
}

static clks_bool clks_disk_find_free_cluster(u32 *out_cluster) {
    u32 start;
    u32 cluster;

    if (out_cluster == CLKS_NULL || clks_disk_fat32.valid == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    start = clks_disk_alloc_hint;
    if (start < 2U || start >= (clks_disk_fat32.cluster_count + 2U)) {
        start = 2U;
    }

    for (cluster = start; cluster < (clks_disk_fat32.cluster_count + 2U); cluster++) {
        u32 fat_value;
        if (clks_disk_fat_read_entry(cluster, &fat_value) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (fat_value == CLKS_DISK_FAT32_FREE_CLUSTER) {
            *out_cluster = cluster;
            clks_disk_alloc_hint = cluster + 1U;
            if (clks_disk_alloc_hint >= (clks_disk_fat32.cluster_count + 2U)) {
                clks_disk_alloc_hint = 2U;
            }
            return CLKS_TRUE;
        }
    }

    for (cluster = 2U; cluster < start; cluster++) {
        u32 fat_value;
        if (clks_disk_fat_read_entry(cluster, &fat_value) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (fat_value == CLKS_DISK_FAT32_FREE_CLUSTER) {
            *out_cluster = cluster;
            clks_disk_alloc_hint = cluster + 1U;
            if (clks_disk_alloc_hint >= (clks_disk_fat32.cluster_count + 2U)) {
                clks_disk_alloc_hint = 2U;
            }
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static void clks_disk_free_chain(u32 first_cluster) {
    u32 current;
    u32 guard = 0U;

    if (clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
        return;
    }

    current = first_cluster;
    while (clks_disk_cluster_valid(current) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 next = CLKS_DISK_INVALID_CLUSTER;
        clks_bool is_eoc = CLKS_FALSE;

        if (clks_disk_fat_next_cluster(current, &next, &is_eoc) == CLKS_FALSE) {
            break;
        }

        (void)clks_disk_fat_write_entry(current, CLKS_DISK_FAT32_FREE_CLUSTER);

        if (is_eoc == CLKS_TRUE) {
            break;
        }

        if (clks_disk_cluster_valid(next) == CLKS_FALSE) {
            break;
        }

        current = next;
        guard++;
    }
}

static clks_bool clks_disk_alloc_cluster(u32 *out_cluster) {
    u32 cluster;

    if (out_cluster == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_find_free_cluster(&cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_fat_write_entry(cluster, CLKS_DISK_FAT32_EOC) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_zero_cluster(cluster) == CLKS_FALSE) {
        (void)clks_disk_fat_write_entry(cluster, CLKS_DISK_FAT32_FREE_CLUSTER);
        return CLKS_FALSE;
    }

    *out_cluster = cluster;
    return CLKS_TRUE;
}

static clks_bool clks_disk_alloc_chain(u32 needed_clusters, u32 *out_first_cluster) {
    u32 first = CLKS_DISK_INVALID_CLUSTER;
    u32 prev = CLKS_DISK_INVALID_CLUSTER;
    u32 i;

    if (out_first_cluster == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (needed_clusters == 0U) {
        *out_first_cluster = CLKS_DISK_INVALID_CLUSTER;
        return CLKS_TRUE;
    }

    for (i = 0U; i < needed_clusters; i++) {
        u32 current;

        if (clks_disk_alloc_cluster(&current) == CLKS_FALSE) {
            if (first != CLKS_DISK_INVALID_CLUSTER) {
                clks_disk_free_chain(first);
            }
            return CLKS_FALSE;
        }

        if (prev != CLKS_DISK_INVALID_CLUSTER) {
            if (clks_disk_fat_write_entry(prev, current) == CLKS_FALSE) {
                clks_disk_free_chain(first);
                return CLKS_FALSE;
            }
        }

        if (first == CLKS_DISK_INVALID_CLUSTER) {
            first = current;
        }
        prev = current;
    }

    *out_first_cluster = first;
    return CLKS_TRUE;
}

static clks_bool clks_disk_cluster_first_byte_offset(u32 cluster, u64 *out_offset) {
    u64 lba;

    if (out_offset == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_cluster_lba(cluster, &lba) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_offset = lba * CLKS_DISK_SECTOR_SIZE;
    return CLKS_TRUE;
}

static u32 clks_disk_dirent_first_cluster(const u8 *entry) {
    u32 high;
    u32 low;
    if (entry == CLKS_NULL) {
        return CLKS_DISK_INVALID_CLUSTER;
    }
    high = (u32)clks_disk_read_u16(entry + 20U);
    low = (u32)clks_disk_read_u16(entry + 26U);
    return (high << 16U) | low;
}

static void clks_disk_dirent_set_first_cluster(u8 *entry, u32 cluster) {
    if (entry == CLKS_NULL) {
        return;
    }
    clks_disk_write_u16(entry + 20U, (u16)((cluster >> 16U) & 0xFFFFU));
    clks_disk_write_u16(entry + 26U, (u16)(cluster & 0xFFFFU));
}

static u32 clks_disk_dirent_size(const u8 *entry) {
    if (entry == CLKS_NULL) {
        return 0U;
    }
    return clks_disk_read_u32(entry + 28U);
}

static clks_bool clks_disk_dirent_is_dot_like(const u8 *entry) {
    if (entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (entry[0] == '.' && entry[1] == ' ') {
        return CLKS_TRUE;
    }

    if (entry[0] == '.' && entry[1] == '.' && entry[2] == ' ') {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_dirent_is_visible(const u8 *entry) {
    u8 attr;

    if (entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (entry[0] == 0x00U || entry[0] == 0xE5U) {
        return CLKS_FALSE;
    }

    attr = entry[11U];
    if (attr == CLKS_DISK_ATTR_LFN) {
        return CLKS_FALSE;
    }
    if ((attr & CLKS_DISK_ATTR_VOLUME_ID) != 0U) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static char clks_disk_ascii_upper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static clks_bool clks_disk_sfn_char_valid(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return CLKS_TRUE;
    }
    if (ch >= '0' && ch <= '9') {
        return CLKS_TRUE;
    }

    switch (ch) {
    case '$':
    case '%':
    case '\'':
    case '-':
    case '_':
    case '@':
    case '~':
    case '`':
    case '!':
    case '(':
    case ')':
    case '{':
    case '}':
    case '^':
    case '#':
    case '&':
    case '+':
    case ',':
    case ';':
    case '=':
        return CLKS_TRUE;
    default:
        break;
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_component_to_sfn(const char *component, u8 out_name[CLKS_DISK_DIRENT_NAME_LEN]) {
    usize i = 0U;
    usize base_len = 0U;
    usize ext_len = 0U;
    clks_bool in_ext = CLKS_FALSE;

    if (component == CLKS_NULL || out_name == CLKS_NULL || component[0] == '\0') {
        return CLKS_FALSE;
    }

    for (i = 0U; i < CLKS_DISK_DIRENT_NAME_LEN; i++) {
        out_name[i] = ' ';
    }

    i = 0U;
    while (component[i] != '\0') {
        char ch = component[i];

        if (ch == '.') {
            if (in_ext == CLKS_TRUE || base_len == 0U) {
                return CLKS_FALSE;
            }
            in_ext = CLKS_TRUE;
            i++;
            continue;
        }

        ch = clks_disk_ascii_upper(ch);
        if (clks_disk_sfn_char_valid(ch) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (in_ext == CLKS_FALSE) {
            if (base_len >= 8U) {
                return CLKS_FALSE;
            }
            out_name[base_len++] = (u8)ch;
        } else {
            if (ext_len >= 3U) {
                return CLKS_FALSE;
            }
            out_name[8U + ext_len++] = (u8)ch;
        }

        i++;
    }

    if (base_len == 0U) {
        return CLKS_FALSE;
    }

    if (in_ext == CLKS_TRUE && ext_len == 0U) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static void clks_disk_sfn_to_text(const u8 *entry, char *out_name, usize out_size) {
    usize base_end = 8U;
    usize ext_end = 3U;
    usize pos = 0U;
    usize i;

    if (out_name == CLKS_NULL || out_size == 0U) {
        return;
    }

    out_name[0] = '\0';
    if (entry == CLKS_NULL) {
        return;
    }

    while (base_end > 0U && entry[base_end - 1U] == ' ') {
        base_end--;
    }
    while (ext_end > 0U && entry[8U + ext_end - 1U] == ' ') {
        ext_end--;
    }

    for (i = 0U; i < base_end && pos + 1U < out_size; i++) {
        out_name[pos++] = (char)entry[i];
    }

    if (ext_end > 0U && pos + 1U < out_size) {
        out_name[pos++] = '.';
        for (i = 0U; i < ext_end && pos + 1U < out_size; i++) {
            out_name[pos++] = (char)entry[8U + i];
        }
    }

    out_name[pos] = '\0';
}

static clks_bool clks_disk_dirent_name_matches(const u8 *entry, const u8 sfn[CLKS_DISK_DIRENT_NAME_LEN]) {
    usize i;

    if (entry == CLKS_NULL || sfn == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < CLKS_DISK_DIRENT_NAME_LEN; i++) {
        if (entry[i] != sfn[i]) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static u8 *clks_disk_dir_entry_ptr(u32 cluster, u32 entry_index, u64 *out_lba) {
    u64 cluster_lba;
    u64 lba;
    u32 entries_per_sector;
    u32 sector_in_cluster;
    u32 entry_in_sector;
    u64 entry_offset;
    u64 disk_offset;

    if (clks_disk_cluster_lba(cluster, &cluster_lba) == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if (entry_index >= clks_disk_fat32.entries_per_cluster) {
        return CLKS_NULL;
    }

    entries_per_sector = (u32)CLKS_DISK_SECTOR_SIZE / CLKS_DISK_DIRENT_SIZE;
    sector_in_cluster = entry_index / entries_per_sector;
    entry_in_sector = entry_index % entries_per_sector;

    lba = cluster_lba + (u64)sector_in_cluster;
    entry_offset = (u64)entry_in_sector * CLKS_DISK_DIRENT_SIZE;
    disk_offset = lba * CLKS_DISK_SECTOR_SIZE + entry_offset;

    if (clks_disk_bytes_range_valid(disk_offset, CLKS_DISK_DIRENT_SIZE) == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if (out_lba != CLKS_NULL) {
        *out_lba = lba;
    }

    return clks_disk_bytes + (usize)disk_offset;
}

static clks_bool clks_disk_dir_find_entry(u32 dir_cluster, const u8 target_sfn[CLKS_DISK_DIRENT_NAME_LEN],
                                          clks_bool *out_found, struct clks_disk_dir_slot *out_slot,
                                          struct clks_disk_dir_info *out_info) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;

    if (out_found == CLKS_NULL || out_slot == CLKS_NULL || out_info == CLKS_NULL || target_sfn == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_found = CLKS_FALSE;
    clks_memset(out_slot, 0, sizeof(*out_slot));
    clks_memset(out_info, 0, sizeof(*out_info));

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);
            u8 attr;

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            if (entry[0] == 0x00U) {
                return CLKS_TRUE;
            }

            if (entry[0] == 0xE5U) {
                continue;
            }

            attr = entry[11U];
            if (attr == CLKS_DISK_ATTR_LFN || (attr & CLKS_DISK_ATTR_VOLUME_ID) != 0U) {
                continue;
            }

            if (clks_disk_dirent_name_matches(entry, target_sfn) == CLKS_TRUE) {
                *out_found = CLKS_TRUE;
                out_slot->cluster = cluster;
                out_slot->entry_index = entry_index;
                out_info->attr = attr;
                out_info->first_cluster = clks_disk_dirent_first_cluster(entry);
                out_info->size = clks_disk_dirent_size(entry);
                return CLKS_TRUE;
            }
        }

        {
            u32 next_cluster = CLKS_DISK_INVALID_CLUSTER;
            clks_bool is_eoc = CLKS_FALSE;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            if (is_eoc == CLKS_TRUE) {
                return CLKS_TRUE;
            }
            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            cluster = next_cluster;
            guard++;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_dir_find_free_slot(u32 dir_cluster, struct clks_disk_dir_slot *out_slot) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;
    clks_bool deleted_found = CLKS_FALSE;
    struct clks_disk_dir_slot deleted_slot;

    if (out_slot == CLKS_NULL || clks_disk_cluster_valid(dir_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_memset(out_slot, 0, sizeof(*out_slot));
    clks_memset(&deleted_slot, 0, sizeof(deleted_slot));

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            if (entry[0] == 0xE5U && deleted_found == CLKS_FALSE) {
                deleted_found = CLKS_TRUE;
                deleted_slot.cluster = cluster;
                deleted_slot.entry_index = entry_index;
            }

            if (entry[0] == 0x00U) {
                if (deleted_found == CLKS_TRUE) {
                    *out_slot = deleted_slot;
                } else {
                    out_slot->cluster = cluster;
                    out_slot->entry_index = entry_index;
                }
                return CLKS_TRUE;
            }
        }

        {
            u32 next_cluster = CLKS_DISK_INVALID_CLUSTER;
            clks_bool is_eoc = CLKS_FALSE;

            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return CLKS_FALSE;
            }

            if (is_eoc == CLKS_TRUE) {
                if (deleted_found == CLKS_TRUE) {
                    *out_slot = deleted_slot;
                    return CLKS_TRUE;
                }

                {
                    u32 new_cluster;
                    if (clks_disk_alloc_cluster(&new_cluster) == CLKS_FALSE) {
                        return CLKS_FALSE;
                    }

                    if (clks_disk_fat_write_entry(cluster, new_cluster) == CLKS_FALSE) {
                        clks_disk_free_chain(new_cluster);
                        return CLKS_FALSE;
                    }

                    out_slot->cluster = new_cluster;
                    out_slot->entry_index = 0U;
                    return CLKS_TRUE;
                }
            }

            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                return CLKS_FALSE;
            }

            cluster = next_cluster;
            guard++;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_write_dir_entry(const struct clks_disk_dir_slot *slot, const u8 *entry_data) {
    u64 lba = 0ULL;
    u8 *entry;

    if (slot == CLKS_NULL || entry_data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    entry = clks_disk_dir_entry_ptr(slot->cluster, slot->entry_index, &lba);
    if (entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_memcpy(entry, entry_data, CLKS_DISK_DIRENT_SIZE);
    return clks_disk_sync_sector_range(lba, 1ULL);
}

static void clks_disk_build_dir_entry(u8 *entry_out, const u8 name[CLKS_DISK_DIRENT_NAME_LEN], u8 attr,
                                      u32 first_cluster, u32 size) {
    if (entry_out == CLKS_NULL || name == CLKS_NULL) {
        return;
    }

    clks_memset(entry_out, 0, CLKS_DISK_DIRENT_SIZE);
    clks_memcpy(entry_out, name, CLKS_DISK_DIRENT_NAME_LEN);
    entry_out[11U] = attr;
    clks_disk_dirent_set_first_cluster(entry_out, first_cluster);
    clks_disk_write_u32(entry_out + 28U, size);
}

static clks_bool clks_disk_init_directory_cluster(u32 cluster, u32 parent_cluster) {
    static const u8 dot_name[CLKS_DISK_DIRENT_NAME_LEN] = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    static const u8 dotdot_name[CLKS_DISK_DIRENT_NAME_LEN] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    u64 cluster_offset;
    u64 byte_count;
    u8 dot_entry[CLKS_DISK_DIRENT_SIZE];
    u8 dotdot_entry[CLKS_DISK_DIRENT_SIZE];
    u64 cluster_lba;

    if (clks_disk_cluster_first_byte_offset(cluster, &cluster_offset) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    byte_count = (u64)clks_disk_fat32.cluster_size_bytes;
    if (clks_disk_bytes_range_valid(cluster_offset, byte_count) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_cluster_lba(cluster, &cluster_lba) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_memset(clks_disk_bytes + (usize)cluster_offset, 0, (usize)byte_count);
    clks_disk_build_dir_entry(dot_entry, dot_name, CLKS_DISK_ATTR_DIRECTORY, cluster, 0U);
    clks_disk_build_dir_entry(dotdot_entry, dotdot_name, CLKS_DISK_ATTR_DIRECTORY, parent_cluster, 0U);

    clks_memcpy(clks_disk_bytes + (usize)cluster_offset + 0U * CLKS_DISK_DIRENT_SIZE, dot_entry, CLKS_DISK_DIRENT_SIZE);
    clks_memcpy(clks_disk_bytes + (usize)cluster_offset + 1U * CLKS_DISK_DIRENT_SIZE, dotdot_entry,
                CLKS_DISK_DIRENT_SIZE);

    return clks_disk_sync_sector_range(cluster_lba, (u64)clks_disk_fat32.sectors_per_cluster);
}

static clks_bool clks_disk_next_component(const char *path, usize *io_pos, char *out_component, usize out_size,
                                          clks_bool *out_last) {
    usize start;
    usize end;
    usize len;

    if (path == CLKS_NULL || io_pos == CLKS_NULL || out_component == CLKS_NULL || out_size == 0U || out_last == CLKS_NULL) {
        return CLKS_FALSE;
    }

    start = *io_pos;
    if (path[start] == '\0') {
        return CLKS_FALSE;
    }

    end = start;
    while (path[end] != '\0' && path[end] != '/') {
        end++;
    }

    len = end - start;
    if (len == 0U || len + 1U > out_size) {
        return CLKS_FALSE;
    }

    clks_memcpy(out_component, path + start, len);
    out_component[len] = '\0';

    if (path[end] == '/') {
        *io_pos = end + 1U;
    } else {
        *io_pos = end;
    }

    *out_last = (path[*io_pos] == '\0') ? CLKS_TRUE : CLKS_FALSE;
    return CLKS_TRUE;
}

static clks_bool clks_disk_lookup_relative_path(const char *relative_path, struct clks_disk_path_lookup *out_lookup) {
    u32 current_dir;
    usize pos = 0U;

    if (relative_path == CLKS_NULL || out_lookup == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_memset(out_lookup, 0, sizeof(*out_lookup));

    if (relative_path[0] == '\0') {
        out_lookup->found = CLKS_TRUE;
        out_lookup->parent_cluster = clks_disk_fat32.root_cluster;
        out_lookup->info.attr = CLKS_DISK_ATTR_DIRECTORY;
        out_lookup->info.first_cluster = clks_disk_fat32.root_cluster;
        out_lookup->info.size = 0U;
        return CLKS_TRUE;
    }

    current_dir = clks_disk_fat32.root_cluster;

    while (relative_path[pos] != '\0') {
        char component[CLKS_DISK_PATH_MAX];
        u8 sfn[CLKS_DISK_DIRENT_NAME_LEN];
        clks_bool last = CLKS_FALSE;
        clks_bool found = CLKS_FALSE;
        struct clks_disk_dir_slot slot;
        struct clks_disk_dir_info info;

        if (clks_disk_next_component(relative_path, &pos, component, sizeof(component), &last) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (clks_disk_component_to_sfn(component, sfn) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (clks_disk_dir_find_entry(current_dir, sfn, &found, &slot, &info) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (last == CLKS_TRUE) {
            out_lookup->found = found;
            out_lookup->parent_cluster = current_dir;
            clks_memcpy(out_lookup->sfn, sfn, CLKS_DISK_DIRENT_NAME_LEN);
            if (found == CLKS_TRUE) {
                out_lookup->slot = slot;
                out_lookup->info = info;
            }
            return CLKS_TRUE;
        }

        if (found == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if ((info.attr & CLKS_DISK_ATTR_DIRECTORY) == 0U) {
            return CLKS_FALSE;
        }

        if (clks_disk_cluster_valid(info.first_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        current_dir = info.first_cluster;
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_dir_is_empty(u32 dir_cluster) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            if (entry[0] == 0x00U) {
                return CLKS_TRUE;
            }

            if (clks_disk_dirent_is_visible(entry) == CLKS_FALSE) {
                continue;
            }

            if (clks_disk_dirent_is_dot_like(entry) == CLKS_TRUE) {
                continue;
            }

            return CLKS_FALSE;
        }

        {
            u32 next_cluster;
            clks_bool is_eoc;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            if (is_eoc == CLKS_TRUE) {
                return CLKS_TRUE;
            }
            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            cluster = next_cluster;
            guard++;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_prepare_read_cache(u64 required_size) {
    void *new_cache;
    u64 alloc_size;

    if (required_size == 0ULL) {
        return CLKS_TRUE;
    }

    if (required_size <= clks_disk_read_cache_size && clks_disk_read_cache != CLKS_NULL) {
        return CLKS_TRUE;
    }

    if (clks_disk_read_cache != CLKS_NULL) {
        clks_kfree(clks_disk_read_cache);
        clks_disk_read_cache = CLKS_NULL;
        clks_disk_read_cache_size = 0ULL;
    }

    alloc_size = required_size;
    if (alloc_size == 0ULL) {
        alloc_size = 1ULL;
    }

    new_cache = clks_kmalloc((usize)alloc_size);
    if (new_cache == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_disk_read_cache = new_cache;
    clks_disk_read_cache_size = alloc_size;
    return CLKS_TRUE;
}

static clks_bool clks_disk_read_file_chain(u32 first_cluster, void *out_data, u64 size) {
    u32 cluster = first_cluster;
    u64 copied = 0ULL;
    u32 guard = 0U;

    if (size == 0ULL) {
        return CLKS_TRUE;
    }

    if (out_data == CLKS_NULL || clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    while (copied < size && clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u64 cluster_offset;
        u64 to_copy;
        u32 next_cluster;
        clks_bool is_eoc;

        if (clks_disk_cluster_first_byte_offset(cluster, &cluster_offset) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        to_copy = size - copied;
        if (to_copy > (u64)clks_disk_fat32.cluster_size_bytes) {
            to_copy = (u64)clks_disk_fat32.cluster_size_bytes;
        }

        if (clks_disk_bytes_range_valid(cluster_offset, to_copy) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        clks_memcpy((u8 *)out_data + (usize)copied, clks_disk_bytes + (usize)cluster_offset, (usize)to_copy);
        copied += to_copy;

        if (copied >= size) {
            return CLKS_TRUE;
        }

        if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (is_eoc == CLKS_TRUE) {
            return CLKS_FALSE;
        }

        if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        cluster = next_cluster;
        guard++;
    }

    return (copied == size) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_disk_write_file_chain(u32 first_cluster, const void *data, u64 size) {
    u32 cluster = first_cluster;
    u64 written = 0ULL;
    u32 guard = 0U;

    if (size == 0ULL) {
        return CLKS_TRUE;
    }

    if (data == CLKS_NULL || clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    while (written < size && clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u64 cluster_lba;
        u64 cluster_offset;
        u64 to_copy;
        u32 next_cluster;
        clks_bool is_eoc;

        if (clks_disk_cluster_lba(cluster, &cluster_lba) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        cluster_offset = cluster_lba * CLKS_DISK_SECTOR_SIZE;
        to_copy = size - written;
        if (to_copy > (u64)clks_disk_fat32.cluster_size_bytes) {
            to_copy = (u64)clks_disk_fat32.cluster_size_bytes;
        }

        if (clks_disk_bytes_range_valid(cluster_offset, (u64)clks_disk_fat32.cluster_size_bytes) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        clks_memset(clks_disk_bytes + (usize)cluster_offset, 0, (usize)clks_disk_fat32.cluster_size_bytes);
        clks_memcpy(clks_disk_bytes + (usize)cluster_offset, (const u8 *)data + (usize)written, (usize)to_copy);

        if (clks_disk_sync_sector_range(cluster_lba, (u64)clks_disk_fat32.sectors_per_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        written += to_copy;
        if (written >= size) {
            return CLKS_TRUE;
        }

        if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (is_eoc == CLKS_TRUE) {
            return CLKS_FALSE;
        }

        if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        cluster = next_cluster;
        guard++;
    }

    return (written == size) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_disk_format_choose_spc(u32 total_sectors, u8 *out_spc) {
    if (out_spc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (total_sectors <= 532480U) {
        *out_spc = 1U;
    } else if (total_sectors <= 16777216U) {
        *out_spc = 8U;
    } else if (total_sectors <= 33554432U) {
        *out_spc = 16U;
    } else if (total_sectors <= 67108864U) {
        *out_spc = 32U;
    } else {
        *out_spc = 64U;
    }

    return CLKS_TRUE;
}

static void clks_disk_label_to_boot_field(char *out_label, const char *label) {
    usize i = 0U;

    for (i = 0U; i < 11U; i++) {
        out_label[i] = ' ';
    }

    if (label == CLKS_NULL || label[0] == '\0') {
        const char default_label[11] = {'C', 'L', 'E', 'O', 'N', 'O', 'S', 'D', 'I', 'S', 'K'};
        for (i = 0U; i < 11U; i++) {
            out_label[i] = default_label[i];
        }
        return;
    }

    i = 0U;
    while (label[i] != '\0' && i < 11U) {
        out_label[i] = clks_disk_ascii_upper(label[i]);
        i++;
    }
}

static clks_bool clks_disk_write_fat32_boot_sector(const char *label) {
    u8 *boot = clks_disk_bytes;
    u8 *fsinfo;
    u8 *backup_boot;
    u8 *backup_fsinfo;
    u32 total_sectors;
    u8 sectors_per_cluster;
    u16 reserved = 32U;
    u8 fats = 2U;
    u32 fat_sectors = 1U;
    u32 iter;
    u64 cluster_count = 0ULL;
    char label_field[11];
    u64 fat_region_lba;
    u32 fat_index;

    if (clks_disk_bytes == CLKS_NULL || clks_disk_sector_total == 0ULL || clks_disk_sector_total > 0xFFFFFFFFULL) {
        return CLKS_FALSE;
    }

    total_sectors = (u32)clks_disk_sector_total;
    if (clks_disk_format_choose_spc(total_sectors, &sectors_per_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (iter = 0U; iter < 16U; iter++) {
        u64 data_sectors;
        u64 next_fat;

        if ((u64)total_sectors <= (u64)reserved + ((u64)fats * (u64)fat_sectors)) {
            return CLKS_FALSE;
        }

        data_sectors = (u64)total_sectors - (u64)reserved - ((u64)fats * (u64)fat_sectors);
        cluster_count = data_sectors / (u64)sectors_per_cluster;
        next_fat = ((cluster_count + 2ULL) * 4ULL + (CLKS_DISK_SECTOR_SIZE - 1ULL)) / CLKS_DISK_SECTOR_SIZE;

        if (next_fat > 0xFFFFFFFFULL) {
            return CLKS_FALSE;
        }

        if ((u32)next_fat == fat_sectors) {
            break;
        }

        fat_sectors = (u32)next_fat;
    }

    if (cluster_count < (u64)CLKS_DISK_FAT32_MIN_CLUSTER_COUNT || cluster_count > 0x0FFFFFF5ULL) {
        return CLKS_FALSE;
    }

    if ((u64)reserved + ((u64)fats * (u64)fat_sectors) >= (u64)total_sectors) {
        return CLKS_FALSE;
    }

    clks_memset(boot, 0, CLKS_DISK_SECTOR_SIZE);
    boot[0U] = 0xEBU;
    boot[1U] = 0x58U;
    boot[2U] = 0x90U;
    clks_memcpy(boot + 3U, "MSDOS5.0", 8U);
    clks_disk_write_u16(boot + 11U, (u16)CLKS_DISK_SECTOR_SIZE);
    boot[13U] = sectors_per_cluster;
    clks_disk_write_u16(boot + 14U, reserved);
    boot[16U] = fats;
    clks_disk_write_u16(boot + 17U, 0U);
    clks_disk_write_u16(boot + 19U, 0U);
    boot[21U] = 0xF8U;
    clks_disk_write_u16(boot + 22U, 0U);
    clks_disk_write_u16(boot + 24U, 63U);
    clks_disk_write_u16(boot + 26U, 255U);
    clks_disk_write_u32(boot + 28U, 0U);
    clks_disk_write_u32(boot + 32U, total_sectors);
    clks_disk_write_u32(boot + 36U, fat_sectors);
    clks_disk_write_u16(boot + 40U, 0U);
    clks_disk_write_u16(boot + 42U, 0U);
    clks_disk_write_u32(boot + 44U, 2U);
    clks_disk_write_u16(boot + 48U, 1U);
    clks_disk_write_u16(boot + 50U, 6U);
    boot[64U] = 0x80U;
    boot[66U] = 0x29U;
    clks_disk_write_u32(boot + 67U, 0x434C4B53U);
    clks_disk_label_to_boot_field(label_field, label);
    clks_memcpy(boot + 71U, label_field, 11U);
    clks_memcpy(boot + 82U, "FAT32   ", 8U);
    boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET] = 0x55U;
    boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET + 1U] = 0xAAU;

    fsinfo = clks_disk_bytes + (usize)CLKS_DISK_SECTOR_SIZE;
    clks_memset(fsinfo, 0, CLKS_DISK_SECTOR_SIZE);
    clks_disk_write_u32(fsinfo + 0U, 0x41615252U);
    clks_disk_write_u32(fsinfo + 484U, 0x61417272U);
    clks_disk_write_u32(fsinfo + 488U, 0xFFFFFFFFU);
    clks_disk_write_u32(fsinfo + 492U, 0xFFFFFFFFU);
    fsinfo[CLKS_DISK_FAT32_BOOT_SIG_OFFSET] = 0x55U;
    fsinfo[CLKS_DISK_FAT32_BOOT_SIG_OFFSET + 1U] = 0xAAU;

    backup_boot = clks_disk_bytes + (usize)(6ULL * CLKS_DISK_SECTOR_SIZE);
    backup_fsinfo = clks_disk_bytes + (usize)(7ULL * CLKS_DISK_SECTOR_SIZE);
    clks_memcpy(backup_boot, boot, CLKS_DISK_SECTOR_SIZE);
    clks_memcpy(backup_fsinfo, fsinfo, CLKS_DISK_SECTOR_SIZE);

    fat_region_lba = (u64)reserved;
    for (fat_index = 0U; fat_index < fats; fat_index++) {
        u64 fat_lba = fat_region_lba + ((u64)fat_index * (u64)fat_sectors);
        u64 fat_offset = fat_lba * CLKS_DISK_SECTOR_SIZE;
        u64 fat_bytes = (u64)fat_sectors * CLKS_DISK_SECTOR_SIZE;
        u8 *fat_ptr;

        if (clks_disk_bytes_range_valid(fat_offset, fat_bytes) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        fat_ptr = clks_disk_bytes + (usize)fat_offset;
        clks_memset(fat_ptr, 0, (usize)fat_bytes);
        clks_disk_write_u32(fat_ptr + 0U, 0x0FFFFFF8U);
        clks_disk_write_u32(fat_ptr + 4U, 0x0FFFFFFFU);
        clks_disk_write_u32(fat_ptr + 8U, CLKS_DISK_FAT32_EOC);
    }

    if (clks_disk_parse_fat32_layout() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_init_directory_cluster(clks_disk_fat32.root_cluster, clks_disk_fat32.root_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return clks_disk_sync_bytes_to_hw((u64)clks_disk_fat32.total_sectors * CLKS_DISK_SECTOR_SIZE);
}

void clks_disk_init(void) {
    u64 detected_sectors = 0ULL;
    u64 cache_bytes;
    u64 alloc_bytes;
    u64 heap_usable_bytes = 0ULL;
    u64 retry_step;
    u64 lba;
    struct clks_heap_stats heap_stats;

    clks_disk_ready = CLKS_FALSE;
    clks_disk_formatted = CLKS_FALSE;
    clks_disk_mounted = CLKS_FALSE;
    clks_disk_hw_backed = CLKS_FALSE;
    clks_disk_bytes = CLKS_NULL;
    clks_disk_bytes_len = 0ULL;
    clks_disk_sector_total = 0ULL;
    clks_disk_mount_path_buf[0] = '\0';
    clks_memset(&clks_disk_fat32, 0, sizeof(clks_disk_fat32));
    clks_disk_alloc_hint = 2U;

    if (clks_disk_read_cache != CLKS_NULL) {
        clks_kfree(clks_disk_read_cache);
        clks_disk_read_cache = CLKS_NULL;
        clks_disk_read_cache_size = 0ULL;
    }

    if (clks_disk_ata_probe(&detected_sectors) == CLKS_FALSE || detected_sectors == 0ULL) {
        clks_log(CLKS_LOG_WARN, "DISK", "NO ATA DISK DETECTED (CHECK QEMU -DRIVE)");
        return;
    }

    cache_bytes = detected_sectors * CLKS_DISK_SECTOR_SIZE;
    if (cache_bytes / CLKS_DISK_SECTOR_SIZE != detected_sectors) {
        clks_log(CLKS_LOG_WARN, "DISK", "DISK SIZE OVERFLOW");
        return;
    }

    if (cache_bytes < CLKS_DISK_MIN_BYTES) {
        clks_log(CLKS_LOG_WARN, "DISK", "ATA DISK TOO SMALL FOR FAT32");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "BYTES", cache_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "MIN_BYTES", CLKS_DISK_MIN_BYTES);
        return;
    }

    if (cache_bytes > CLKS_DISK_CACHE_MAX_BYTES) {
        cache_bytes = CLKS_DISK_CACHE_MAX_BYTES;
        clks_log(CLKS_LOG_WARN, "DISK", "DISK CACHE TRUNCATED");
    }

    heap_stats = clks_heap_get_stats();
    if (heap_stats.free_bytes > CLKS_DISK_HEAP_RESERVE_BYTES) {
        heap_usable_bytes = heap_stats.free_bytes - CLKS_DISK_HEAP_RESERVE_BYTES;
    }

    if (heap_usable_bytes < CLKS_DISK_MIN_BYTES) {
        clks_log(CLKS_LOG_WARN, "DISK", "INSUFFICIENT HEAP FOR DISK CACHE");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_FREE_BYTES", heap_stats.free_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_RESERVE_BYTES", CLKS_DISK_HEAP_RESERVE_BYTES);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "MIN_BYTES", CLKS_DISK_MIN_BYTES);
        return;
    }

    alloc_bytes = cache_bytes;
    if (alloc_bytes > heap_usable_bytes) {
        alloc_bytes = heap_usable_bytes;
    }
    alloc_bytes -= (alloc_bytes % CLKS_DISK_SECTOR_SIZE);
    if (alloc_bytes < CLKS_DISK_MIN_BYTES) {
        clks_log(CLKS_LOG_WARN, "DISK", "DISK CACHE HEAP BUDGET TOO SMALL");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_FREE_BYTES", heap_stats.free_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_USABLE_BYTES", heap_usable_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "MIN_BYTES", CLKS_DISK_MIN_BYTES);
        return;
    }
    retry_step = CLKS_DISK_ALLOC_RETRY_STEP_BYTES;
    if (retry_step < CLKS_DISK_SECTOR_SIZE) {
        retry_step = CLKS_DISK_SECTOR_SIZE;
    }
    retry_step -= (retry_step % CLKS_DISK_SECTOR_SIZE);

    while (alloc_bytes >= CLKS_DISK_MIN_BYTES) {
        clks_disk_bytes = (u8 *)clks_kmalloc((usize)alloc_bytes);
        if (clks_disk_bytes != CLKS_NULL) {
            cache_bytes = alloc_bytes;
            break;
        }

        if (alloc_bytes == CLKS_DISK_MIN_BYTES) {
            break;
        }

        if (alloc_bytes > (CLKS_DISK_MIN_BYTES + retry_step)) {
            alloc_bytes -= retry_step;
        } else {
            alloc_bytes = CLKS_DISK_MIN_BYTES;
        }

        alloc_bytes -= (alloc_bytes % CLKS_DISK_SECTOR_SIZE);
    }

    if (clks_disk_bytes == CLKS_NULL) {
        clks_log(CLKS_LOG_WARN, "DISK", "DISK BACKEND ALLOCATION FAILED");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "CACHE_BYTES", cache_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_FREE_BYTES", heap_stats.free_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_RESERVE_BYTES", CLKS_DISK_HEAP_RESERVE_BYTES);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_USABLE_BYTES", heap_usable_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "MIN_BYTES", CLKS_DISK_MIN_BYTES);
        return;
    }

    detected_sectors = cache_bytes / CLKS_DISK_SECTOR_SIZE;

    for (lba = 0ULL; lba < detected_sectors; lba++) {
        u8 *sector_ptr = clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE);
        if (clks_disk_ata_read_sector_hw(lba, sector_ptr) == CLKS_FALSE) {
            clks_log(CLKS_LOG_WARN, "DISK", "ATA READ FAILED DURING CACHE LOAD");
            clks_log_hex(CLKS_LOG_WARN, "DISK", "LBA", lba);
            clks_kfree(clks_disk_bytes);
            clks_disk_bytes = CLKS_NULL;
            return;
        }
    }

    clks_disk_bytes_len = cache_bytes;
    clks_disk_sector_total = detected_sectors;
    clks_disk_hw_backed = CLKS_TRUE;
    clks_disk_ready = CLKS_TRUE;
    clks_disk_formatted = clks_disk_parse_fat32_layout();

    if (clks_disk_formatted == CLKS_TRUE) {
        (void)clks_disk_mount("/temp/disk");
    }

    clks_log(CLKS_LOG_INFO, "DISK", "DISK BACKEND ONLINE");
    clks_log_hex(CLKS_LOG_INFO, "DISK", "BYTES", clks_disk_bytes_len);
    clks_log_hex(CLKS_LOG_INFO, "DISK", "SECTORS", clks_disk_sector_total);
    clks_log_hex(CLKS_LOG_INFO, "DISK", "FAT32", (clks_disk_formatted == CLKS_TRUE) ? 1ULL : 0ULL);
    clks_log_hex(CLKS_LOG_INFO, "DISK", "HW_BACKED", (clks_disk_hw_backed == CLKS_TRUE) ? 1ULL : 0ULL);
}

clks_bool clks_disk_present(void) {
    return clks_disk_ready;
}

u64 clks_disk_size_bytes(void) {
    return (clks_disk_ready == CLKS_TRUE) ? clks_disk_bytes_len : 0ULL;
}

u64 clks_disk_sector_count(void) {
    return (clks_disk_ready == CLKS_TRUE) ? clks_disk_sector_total : 0ULL;
}

clks_bool clks_disk_read_sector(u64 lba, void *out_sector) {
    if (clks_disk_ready == CLKS_FALSE || out_sector == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (lba >= clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    if (clks_disk_hw_backed == CLKS_TRUE) {
        if (clks_disk_ata_read_sector_hw(lba, out_sector) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        clks_memcpy(clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE), out_sector, (usize)CLKS_DISK_SECTOR_SIZE);
        return CLKS_TRUE;
    }

    clks_memcpy(out_sector, clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE), (usize)CLKS_DISK_SECTOR_SIZE);
    return CLKS_TRUE;
}

clks_bool clks_disk_write_sector(u64 lba, const void *sector_data) {
    if (clks_disk_ready == CLKS_FALSE || sector_data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (lba >= clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    clks_memcpy(clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE), sector_data, (usize)CLKS_DISK_SECTOR_SIZE);

    if (clks_disk_hw_backed == CLKS_TRUE) {
        if (clks_disk_ata_write_sector_hw(lba, sector_data) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (clks_disk_ata_cache_flush_hw() == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

clks_bool clks_disk_is_formatted_fat32(void) {
    return (clks_disk_ready == CLKS_TRUE && clks_disk_formatted == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;
}

clks_bool clks_disk_format_fat32(const char *label) {
    if (clks_disk_ready == CLKS_FALSE || clks_disk_bytes == CLKS_NULL) {
        clks_log(CLKS_LOG_WARN, "DISK", "FAT32 FORMAT FAILED: DISK NOT READY");
        return CLKS_FALSE;
    }

    clks_memset(clks_disk_bytes, 0, (usize)clks_disk_bytes_len);

    if (clks_disk_write_fat32_boot_sector(label) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "DISK", "FAT32 FORMAT FAILED: BOOT/FAT INIT");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "BYTES", clks_disk_bytes_len);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "SECTORS", clks_disk_sector_total);
        clks_disk_formatted = CLKS_FALSE;
        clks_memset(&clks_disk_fat32, 0, sizeof(clks_disk_fat32));
        return CLKS_FALSE;
    }

    clks_disk_formatted = clks_disk_parse_fat32_layout();
    if (clks_disk_formatted == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "DISK", "FAT32 FORMAT FAILED: LAYOUT PARSE");
        return CLKS_FALSE;
    }

    clks_log(CLKS_LOG_INFO, "DISK", "FAT32 FORMAT COMPLETE");
    return CLKS_TRUE;
}

clks_bool clks_disk_mount(const char *mount_path) {
    char normalized[CLKS_DISK_PATH_MAX];

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_normalize_absolute_path(mount_path, normalized, sizeof(normalized)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_strcmp(normalized, "/") == 0) {
        return CLKS_FALSE;
    }

    clks_disk_copy_text(clks_disk_mount_path_buf, sizeof(clks_disk_mount_path_buf), normalized);
    clks_disk_mounted = CLKS_TRUE;
    return CLKS_TRUE;
}

clks_bool clks_disk_is_mounted(void) {
    return (clks_disk_ready == CLKS_TRUE && clks_disk_mounted == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;
}

const char *clks_disk_mount_path(void) {
    if (clks_disk_mounted == CLKS_FALSE) {
        return "";
    }

    return clks_disk_mount_path_buf;
}

clks_bool clks_disk_path_in_mount(const char *path) {
    char relative[CLKS_DISK_PATH_MAX];
    return (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;
}

clks_bool clks_disk_stat(const char *path, u64 *out_type, u64 *out_size) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;

    if (out_type == CLKS_NULL || out_size == CLKS_NULL ||
        clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (relative[0] == '\0') {
        *out_type = CLKS_DISK_NODE_DIR;
        *out_size = 0ULL;
        return CLKS_TRUE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_type = ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) ? CLKS_DISK_NODE_DIR : CLKS_DISK_NODE_FILE;
    *out_size = (u64)lookup.info.size;
    return CLKS_TRUE;
}

const void *clks_disk_read_all(const char *path, u64 *out_size) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE || relative[0] == '\0') {
        return CLKS_NULL;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
        return CLKS_NULL;
    }

    if (out_size != CLKS_NULL) {
        *out_size = (u64)lookup.info.size;
    }

    if (lookup.info.size == 0U) {
        return (const void *)clks_disk_empty_file_data;
    }

    if (clks_disk_prepare_read_cache((u64)lookup.info.size) == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if (clks_disk_read_file_chain(lookup.info.first_cluster, clks_disk_read_cache, (u64)lookup.info.size) == CLKS_FALSE) {
        return CLKS_NULL;
    }

    return (const void *)clks_disk_read_cache;
}

u64 clks_disk_count_children(const char *dir_path) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    u32 dir_cluster;
    u32 cluster;
    u32 guard = 0U;
    u64 count = 0ULL;

    if (clks_disk_path_to_relative(dir_path, relative, sizeof(relative)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return 0ULL;
    }

    if (relative[0] == '\0') {
        dir_cluster = clks_disk_fat32.root_cluster;
    } else {
        if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
            return 0ULL;
        }
        if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) == 0U) {
            return 0ULL;
        }
        dir_cluster = lookup.info.first_cluster;
    }

    cluster = dir_cluster;
    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);

            if (entry == CLKS_NULL) {
                return 0ULL;
            }

            if (entry[0] == 0x00U) {
                return count;
            }

            if (clks_disk_dirent_is_visible(entry) == CLKS_FALSE) {
                continue;
            }

            if (clks_disk_dirent_is_dot_like(entry) == CLKS_TRUE) {
                continue;
            }

            count++;
        }

        {
            u32 next_cluster;
            clks_bool is_eoc;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return 0ULL;
            }
            if (is_eoc == CLKS_TRUE) {
                return count;
            }
            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                return 0ULL;
            }
            cluster = next_cluster;
            guard++;
        }
    }

    return count;
}

clks_bool clks_disk_get_child_name(const char *dir_path, u64 index, char *out_name, usize out_name_size) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    u32 dir_cluster;
    u32 cluster;
    u32 guard = 0U;
    u64 current = 0ULL;

    if (out_name == CLKS_NULL || out_name_size == 0U) {
        return CLKS_FALSE;
    }

    out_name[0] = '\0';

    if (clks_disk_path_to_relative(dir_path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (relative[0] == '\0') {
        dir_cluster = clks_disk_fat32.root_cluster;
    } else {
        if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) == 0U) {
            return CLKS_FALSE;
        }
        dir_cluster = lookup.info.first_cluster;
    }

    cluster = dir_cluster;
    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            if (entry[0] == 0x00U) {
                return CLKS_FALSE;
            }

            if (clks_disk_dirent_is_visible(entry) == CLKS_FALSE) {
                continue;
            }

            if (clks_disk_dirent_is_dot_like(entry) == CLKS_TRUE) {
                continue;
            }

            if (current != index) {
                current++;
                continue;
            }

            clks_disk_sfn_to_text(entry, out_name, out_name_size);
            return (out_name[0] != '\0') ? CLKS_TRUE : CLKS_FALSE;
        }

        {
            u32 next_cluster;
            clks_bool is_eoc;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            if (is_eoc == CLKS_TRUE) {
                return CLKS_FALSE;
            }
            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            cluster = next_cluster;
            guard++;
        }
    }

    return CLKS_FALSE;
}

clks_bool clks_disk_mkdir(const char *path) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    struct clks_disk_dir_slot free_slot;
    u8 entry[CLKS_DISK_DIRENT_SIZE];
    u32 new_cluster;

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (relative[0] == '\0') {
        return CLKS_TRUE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (lookup.found == CLKS_TRUE) {
        return ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) ? CLKS_TRUE : CLKS_FALSE;
    }

    if (clks_disk_cluster_valid(lookup.parent_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_dir_find_free_slot(lookup.parent_cluster, &free_slot) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_alloc_cluster(&new_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_init_directory_cluster(new_cluster, lookup.parent_cluster) == CLKS_FALSE) {
        clks_disk_free_chain(new_cluster);
        return CLKS_FALSE;
    }

    clks_disk_build_dir_entry(entry, lookup.sfn, CLKS_DISK_ATTR_DIRECTORY, new_cluster, 0U);

    if (clks_disk_write_dir_entry(&free_slot, entry) == CLKS_FALSE) {
        clks_disk_free_chain(new_cluster);
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

clks_bool clks_disk_write_all(const char *path, const void *data, u64 size) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    struct clks_disk_dir_slot target_slot;
    u32 old_cluster = CLKS_DISK_INVALID_CLUSTER;
    u32 new_cluster = CLKS_DISK_INVALID_CLUSTER;
    u32 needed_clusters = 0U;
    u8 entry[CLKS_DISK_DIRENT_SIZE];

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE || relative[0] == '\0') {
        return CLKS_FALSE;
    }

    if (size > 0ULL && data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (lookup.found == CLKS_TRUE && (lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
        return CLKS_FALSE;
    }

    if (lookup.found == CLKS_TRUE) {
        target_slot = lookup.slot;
        old_cluster = lookup.info.first_cluster;
    } else {
        if (clks_disk_cluster_valid(lookup.parent_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (clks_disk_dir_find_free_slot(lookup.parent_cluster, &target_slot) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    if (size > 0ULL) {
        u64 cluster_size = (u64)clks_disk_fat32.cluster_size_bytes;
        u64 need64 = (size + cluster_size - 1ULL) / cluster_size;
        if (need64 == 0ULL || need64 > (u64)clks_disk_fat32.cluster_count) {
            return CLKS_FALSE;
        }
        needed_clusters = (u32)need64;
    }

    if (old_cluster != CLKS_DISK_INVALID_CLUSTER) {
        clks_disk_free_chain(old_cluster);
    }

    if (needed_clusters > 0U) {
        if (clks_disk_alloc_chain(needed_clusters, &new_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (clks_disk_write_file_chain(new_cluster, data, size) == CLKS_FALSE) {
            clks_disk_free_chain(new_cluster);
            return CLKS_FALSE;
        }
    }

    clks_disk_build_dir_entry(entry, lookup.sfn, CLKS_DISK_ATTR_ARCHIVE, new_cluster, (u32)size);

    if (clks_disk_write_dir_entry(&target_slot, entry) == CLKS_FALSE) {
        if (new_cluster != CLKS_DISK_INVALID_CLUSTER) {
            clks_disk_free_chain(new_cluster);
        }
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

clks_bool clks_disk_append(const char *path, const void *data, u64 size) {
    char relative[CLKS_DISK_PATH_MAX];
    const void *old_data;
    u64 old_size = 0ULL;
    u64 new_size;
    void *merged;
    struct clks_disk_path_lookup lookup;

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE || relative[0] == '\0') {
        return CLKS_FALSE;
    }

    if (size > 0ULL && data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (lookup.found == CLKS_FALSE) {
        return clks_disk_write_all(path, data, size);
    }

    if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
        return CLKS_FALSE;
    }

    old_data = clks_disk_read_all(path, &old_size);
    if (old_data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (old_size > (0xFFFFFFFFFFFFFFFFULL - size)) {
        return CLKS_FALSE;
    }

    new_size = old_size + size;
    if (new_size == 0ULL) {
        return clks_disk_write_all(path, clks_disk_empty_file_data, 0ULL);
    }

    merged = clks_kmalloc((usize)new_size);
    if (merged == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (old_size > 0ULL) {
        clks_memcpy(merged, old_data, (usize)old_size);
    }
    if (size > 0ULL) {
        clks_memcpy((u8 *)merged + (usize)old_size, data, (usize)size);
    }

    if (clks_disk_write_all(path, merged, new_size) == CLKS_FALSE) {
        clks_kfree(merged);
        return CLKS_FALSE;
    }

    clks_kfree(merged);
    return CLKS_TRUE;
}

clks_bool clks_disk_remove(const char *path) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    u64 lba = 0ULL;
    u8 *entry;

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE || relative[0] == '\0') {
        return CLKS_FALSE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
        if (clks_disk_cluster_valid(lookup.info.first_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (clks_disk_dir_is_empty(lookup.info.first_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    entry = clks_disk_dir_entry_ptr(lookup.slot.cluster, lookup.slot.entry_index, &lba);
    if (entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    entry[0] = 0xE5U;
    if (clks_disk_sync_sector_range(lba, 1ULL) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_cluster_valid(lookup.info.first_cluster) == CLKS_TRUE) {
        clks_disk_free_chain(lookup.info.first_cluster);
    }

    return CLKS_TRUE;
}

static u64 clks_disk_count_nodes_recursive(u32 dir_cluster, u32 depth) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;
    u64 count = 0ULL;

    if (depth > 64U) {
        return 0ULL;
    }

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);
            u8 attr;
            u32 sub_cluster;

            if (entry == CLKS_NULL) {
                return count;
            }

            if (entry[0] == 0x00U) {
                return count;
            }

            if (clks_disk_dirent_is_visible(entry) == CLKS_FALSE) {
                continue;
            }

            if (clks_disk_dirent_is_dot_like(entry) == CLKS_TRUE) {
                continue;
            }

            count++;
            attr = entry[11U];
            if ((attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
                sub_cluster = clks_disk_dirent_first_cluster(entry);
                if (clks_disk_cluster_valid(sub_cluster) == CLKS_TRUE) {
                    count += clks_disk_count_nodes_recursive(sub_cluster, depth + 1U);
                }
            }
        }

        {
            u32 next_cluster;
            clks_bool is_eoc;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return count;
            }
            if (is_eoc == CLKS_TRUE) {
                return count;
            }
            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                return count;
            }
            cluster = next_cluster;
            guard++;
        }
    }

    return count;
}

u64 clks_disk_node_count(void) {
    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return 0ULL;
    }

    return 1ULL + clks_disk_count_nodes_recursive(clks_disk_fat32.root_cluster, 0U);
}
