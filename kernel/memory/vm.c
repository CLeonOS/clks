#include <clks/boot.h>
#include <clks/log.h>
#include <clks/pmm.h>
#include <clks/string.h>
#include <clks/types.h>
#include <clks/vm.h>

#define CLKS_VM_PTE_PRESENT (1ULL << 0U)
#define CLKS_VM_PTE_WRITE (1ULL << 1U)
#define CLKS_VM_PTE_USER (1ULL << 2U)
#define CLKS_VM_PTE_PWT (1ULL << 3U)
#define CLKS_VM_PTE_PCD (1ULL << 4U)
#define CLKS_VM_PTE_PS (1ULL << 7U)
#define CLKS_VM_PTE_NX (1ULL << 63U)
#define CLKS_VM_PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define CLKS_VM_MSR_EFER 0xC0000080U
#define CLKS_VM_EFER_NXE (1ULL << 11U)
#define CLKS_VM_CPUID_EXT_FEATURES 0x80000001U
#define CLKS_VM_CPUID_EXT_NX (1U << 20U)

static clks_bool clks_vm_ready = CLKS_FALSE;
static clks_bool clks_vm_nxe_ready = CLKS_FALSE;
static u64 clks_vm_kernel_cr3_value = 0ULL;

static inline void clks_vm_invlpg(u64 virt_addr) {
#if defined(CLKS_ARCH_X86_64)
    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
#else
    (void)virt_addr;
#endif
}

#if defined(CLKS_ARCH_X86_64)
static void clks_vm_cpuid(u32 leaf, u32 *out_eax, u32 *out_ebx, u32 *out_ecx, u32 *out_edx) {
    u32 eax = 0U;
    u32 ebx = 0U;
    u32 ecx = 0U;
    u32 edx = 0U;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(0U));

    if (out_eax != CLKS_NULL) {
        *out_eax = eax;
    }
    if (out_ebx != CLKS_NULL) {
        *out_ebx = ebx;
    }
    if (out_ecx != CLKS_NULL) {
        *out_ecx = ecx;
    }
    if (out_edx != CLKS_NULL) {
        *out_edx = edx;
    }
}

static u64 clks_vm_rdmsr(u32 msr) {
    u32 lo = 0U;
    u32 hi = 0U;

    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32U) | (u64)lo;
}

static void clks_vm_wrmsr(u32 msr, u64 value) {
    u32 lo = (u32)(value & 0xFFFFFFFFULL);
    u32 hi = (u32)(value >> 32U);

    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static clks_bool clks_vm_enable_nxe(void) {
    u32 max_ext = 0U;
    u32 edx = 0U;
    u64 efer;

    clks_vm_cpuid(0x80000000U, &max_ext, CLKS_NULL, CLKS_NULL, CLKS_NULL);
    if (max_ext < CLKS_VM_CPUID_EXT_FEATURES) {
        return CLKS_FALSE;
    }

    clks_vm_cpuid(CLKS_VM_CPUID_EXT_FEATURES, CLKS_NULL, CLKS_NULL, CLKS_NULL, &edx);
    if ((edx & CLKS_VM_CPUID_EXT_NX) == 0U) {
        return CLKS_FALSE;
    }

    efer = clks_vm_rdmsr(CLKS_VM_MSR_EFER);
    if ((efer & CLKS_VM_EFER_NXE) == 0ULL) {
        clks_vm_wrmsr(CLKS_VM_MSR_EFER, efer | CLKS_VM_EFER_NXE);
        efer = clks_vm_rdmsr(CLKS_VM_MSR_EFER);
    }

    return ((efer & CLKS_VM_EFER_NXE) != 0ULL) ? CLKS_TRUE : CLKS_FALSE;
}
#endif

u64 clks_vm_current_cr3(void) {
    u64 value = 0ULL;

#if defined(CLKS_ARCH_X86_64)
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
#endif

    return value;
}

u64 clks_vm_kernel_cr3(void) {
    return clks_vm_kernel_cr3_value;
}

void clks_vm_switch_cr3(u64 cr3) {
#if defined(CLKS_ARCH_X86_64)
    if (cr3 != 0ULL && (cr3 & (CLKS_VM_PAGE_SIZE - 1ULL)) == 0ULL) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    }
#else
    (void)cr3;
#endif
}

static u64 *clks_vm_current_pml4(void) {
    u64 cr3 = clks_vm_current_cr3();

    if (cr3 == 0ULL) {
        return CLKS_NULL;
    }

    return (u64 *)clks_boot_phys_to_virt(cr3 & CLKS_VM_PTE_ADDR_MASK);
}

static u64 clks_vm_entry_flags_from_api(u64 flags) {
    u64 entry_flags = CLKS_VM_PTE_PRESENT;

    if ((flags & CLKS_VM_FLAG_WRITE) != 0ULL) {
        entry_flags |= CLKS_VM_PTE_WRITE;
    }

    if ((flags & CLKS_VM_FLAG_USER) != 0ULL) {
        entry_flags |= CLKS_VM_PTE_USER;
    }

    if (clks_vm_nxe_ready == CLKS_TRUE && (flags & CLKS_VM_FLAG_EXEC) == 0ULL) {
        entry_flags |= CLKS_VM_PTE_NX;
    }

    if ((flags & CLKS_VM_FLAG_MMIO) != 0ULL) {
        entry_flags |= CLKS_VM_PTE_PWT | CLKS_VM_PTE_PCD;
    }

    return entry_flags;
}

static clks_bool clks_vm_next_table(u64 *entry, u64 **out_table, u64 table_flags) {
    u64 phys;
    u64 *table;

    if (entry == CLKS_NULL || out_table == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if ((*entry & CLKS_VM_PTE_PRESENT) == 0ULL) {
        void *virt;
        u64 new_phys = clks_pmm_alloc_page();

        if (new_phys == 0ULL) {
            return CLKS_FALSE;
        }

        virt = clks_boot_phys_to_virt(new_phys);
        if (virt == CLKS_NULL) {
            clks_pmm_free_page(new_phys);
            return CLKS_FALSE;
        }

        clks_memset(virt, 0, (usize)CLKS_VM_PAGE_SIZE);
        *entry = (new_phys & CLKS_VM_PTE_ADDR_MASK) | CLKS_VM_PTE_PRESENT | CLKS_VM_PTE_WRITE | table_flags;
    }

    if ((*entry & CLKS_VM_PTE_PS) != 0ULL) {
        return CLKS_FALSE;
    }

    phys = *entry & CLKS_VM_PTE_ADDR_MASK;
    table = (u64 *)clks_boot_phys_to_virt(phys);
    if (table == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_table = table;
    return CLKS_TRUE;
}

static void clks_vm_destroy_table_recursive(u64 table_phys, u32 level) {
    u64 *table;
    u32 i;

    if (table_phys == 0ULL || (table_phys & (CLKS_VM_PAGE_SIZE - 1ULL)) != 0ULL) {
        return;
    }

    table = (u64 *)clks_boot_phys_to_virt(table_phys);
    if (table == CLKS_NULL) {
        return;
    }

    if (level > 1U) {
        for (i = 0U; i < 512U; i++) {
            u64 entry = table[i];

            if ((entry & CLKS_VM_PTE_PRESENT) == 0ULL || (entry & CLKS_VM_PTE_PS) != 0ULL) {
                continue;
            }

            clks_vm_destroy_table_recursive(entry & CLKS_VM_PTE_ADDR_MASK, level - 1U);
            table[i] = 0ULL;
        }
    }

    clks_pmm_free_page(table_phys);
}

void clks_vm_init(void) {
    clks_vm_kernel_cr3_value = clks_vm_current_cr3() & CLKS_VM_PTE_ADDR_MASK;
    clks_vm_ready = (clks_vm_current_pml4() != CLKS_NULL) ? CLKS_TRUE : CLKS_FALSE;
    clks_vm_nxe_ready = CLKS_FALSE;

#if defined(CLKS_ARCH_X86_64)
    if (clks_vm_ready == CLKS_TRUE) {
        clks_vm_nxe_ready = clks_vm_enable_nxe();
    }
#endif

    if (clks_vm_ready == CLKS_TRUE) {
        clks_log(CLKS_LOG_INFO, "VM", "PAGING MANAGER ONLINE");
        clks_log_hex(CLKS_LOG_INFO, "VM", "NXE", (clks_vm_nxe_ready == CLKS_TRUE) ? 1ULL : 0ULL);
    } else {
        clks_log(CLKS_LOG_WARN, "VM", "PAGING MANAGER UNAVAILABLE");
    }
}

u64 clks_vm_create_address_space(void) {
    u64 phys;
    u64 *dst;
    u64 *kernel_pml4;
    u32 i;

    if (clks_vm_ready == CLKS_FALSE || clks_vm_kernel_cr3_value == 0ULL) {
        return 0ULL;
    }

    kernel_pml4 = (u64 *)clks_boot_phys_to_virt(clks_vm_kernel_cr3_value);
    if (kernel_pml4 == CLKS_NULL) {
        return 0ULL;
    }

    phys = clks_pmm_alloc_page();
    if (phys == 0ULL) {
        return 0ULL;
    }

    dst = (u64 *)clks_boot_phys_to_virt(phys);
    if (dst == CLKS_NULL) {
        clks_pmm_free_page(phys);
        return 0ULL;
    }

    clks_memset(dst, 0, (usize)CLKS_VM_PAGE_SIZE);

    /*
     * Keep the kernel/HHDM half shared. User VM allocations live in low PML4
     * slots and are created per process on demand.
     */
    for (i = 256U; i < 512U; i++) {
        dst[i] = kernel_pml4[i];
    }

    return phys & CLKS_VM_PTE_ADDR_MASK;
}

void clks_vm_destroy_address_space(u64 cr3) {
    u64 *pml4;
    u32 i;

    if (cr3 == 0ULL || (cr3 & (CLKS_VM_PAGE_SIZE - 1ULL)) != 0ULL || cr3 == clks_vm_kernel_cr3_value) {
        return;
    }

    if (clks_vm_current_cr3() == cr3 && clks_vm_kernel_cr3_value != 0ULL) {
        clks_vm_switch_cr3(clks_vm_kernel_cr3_value);
    }

    pml4 = (u64 *)clks_boot_phys_to_virt(cr3);
    if (pml4 != CLKS_NULL) {
        for (i = 0U; i < 256U; i++) {
            u64 entry = pml4[i];

            if ((entry & CLKS_VM_PTE_PRESENT) == 0ULL || (entry & CLKS_VM_PTE_PS) != 0ULL) {
                continue;
            }

            clks_vm_destroy_table_recursive(entry & CLKS_VM_PTE_ADDR_MASK, 3U);
            pml4[i] = 0ULL;
        }
    }

    clks_pmm_free_page(cr3);
}

clks_bool clks_vm_map_page_current(u64 virt_addr, u64 phys_addr, u64 flags) {
    u64 *pml4;
    u64 *pdpt;
    u64 *pd;
    u64 *pt;
    u64 pml4_index;
    u64 pdpt_index;
    u64 pd_index;
    u64 pt_index;
    u64 table_flags;

    if (clks_vm_ready == CLKS_FALSE || (virt_addr & (CLKS_VM_PAGE_SIZE - 1ULL)) != 0ULL ||
        (phys_addr & (CLKS_VM_PAGE_SIZE - 1ULL)) != 0ULL) {
        return CLKS_FALSE;
    }

    pml4 = clks_vm_current_pml4();
    if (pml4 == CLKS_NULL) {
        return CLKS_FALSE;
    }

    table_flags = ((flags & CLKS_VM_FLAG_USER) != 0ULL) ? CLKS_VM_PTE_USER : 0ULL;
    pml4_index = (virt_addr >> 39U) & 0x1FFULL;
    pdpt_index = (virt_addr >> 30U) & 0x1FFULL;
    pd_index = (virt_addr >> 21U) & 0x1FFULL;
    pt_index = (virt_addr >> 12U) & 0x1FFULL;

    if (clks_vm_next_table(&pml4[pml4_index], &pdpt, table_flags) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_vm_next_table(&pdpt[pdpt_index], &pd, table_flags) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_vm_next_table(&pd[pd_index], &pt, table_flags) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    pt[pt_index] = (phys_addr & CLKS_VM_PTE_ADDR_MASK) | clks_vm_entry_flags_from_api(flags);
    clks_vm_invlpg(virt_addr);
    return CLKS_TRUE;
}

clks_bool clks_vm_unmap_page_current(u64 virt_addr, u64 *out_phys_addr) {
    u64 *pml4;
    u64 *pdpt;
    u64 *pd;
    u64 *pt;
    u64 entry;
    u64 pml4_index;
    u64 pdpt_index;
    u64 pd_index;
    u64 pt_index;

    if (clks_vm_ready == CLKS_FALSE || (virt_addr & (CLKS_VM_PAGE_SIZE - 1ULL)) != 0ULL) {
        return CLKS_FALSE;
    }

    pml4 = clks_vm_current_pml4();
    if (pml4 == CLKS_NULL) {
        return CLKS_FALSE;
    }

    pml4_index = (virt_addr >> 39U) & 0x1FFULL;
    pdpt_index = (virt_addr >> 30U) & 0x1FFULL;
    pd_index = (virt_addr >> 21U) & 0x1FFULL;
    pt_index = (virt_addr >> 12U) & 0x1FFULL;

    if ((pml4[pml4_index] & CLKS_VM_PTE_PRESENT) == 0ULL || (pml4[pml4_index] & CLKS_VM_PTE_PS) != 0ULL) {
        return CLKS_FALSE;
    }
    pdpt = (u64 *)clks_boot_phys_to_virt(pml4[pml4_index] & CLKS_VM_PTE_ADDR_MASK);
    if (pdpt == CLKS_NULL || (pdpt[pdpt_index] & CLKS_VM_PTE_PRESENT) == 0ULL ||
        (pdpt[pdpt_index] & CLKS_VM_PTE_PS) != 0ULL) {
        return CLKS_FALSE;
    }
    pd = (u64 *)clks_boot_phys_to_virt(pdpt[pdpt_index] & CLKS_VM_PTE_ADDR_MASK);
    if (pd == CLKS_NULL || (pd[pd_index] & CLKS_VM_PTE_PRESENT) == 0ULL || (pd[pd_index] & CLKS_VM_PTE_PS) != 0ULL) {
        return CLKS_FALSE;
    }
    pt = (u64 *)clks_boot_phys_to_virt(pd[pd_index] & CLKS_VM_PTE_ADDR_MASK);
    if (pt == CLKS_NULL) {
        return CLKS_FALSE;
    }

    entry = pt[pt_index];
    if ((entry & CLKS_VM_PTE_PRESENT) == 0ULL) {
        return CLKS_FALSE;
    }

    if (out_phys_addr != CLKS_NULL) {
        *out_phys_addr = entry & CLKS_VM_PTE_ADDR_MASK;
    }

    pt[pt_index] = 0ULL;
    clks_vm_invlpg(virt_addr);
    return CLKS_TRUE;
}

clks_bool clks_vm_translate_current(u64 virt_addr, u64 *out_phys_addr, u64 *out_flags) {
    u64 *pml4;
    u64 *pdpt;
    u64 *pd;
    u64 *pt;
    u64 entry;
    u64 pml4_index = (virt_addr >> 39U) & 0x1FFULL;
    u64 pdpt_index = (virt_addr >> 30U) & 0x1FFULL;
    u64 pd_index = (virt_addr >> 21U) & 0x1FFULL;
    u64 pt_index = (virt_addr >> 12U) & 0x1FFULL;

    if (clks_vm_ready == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    pml4 = clks_vm_current_pml4();
    if (pml4 == CLKS_NULL || (pml4[pml4_index] & CLKS_VM_PTE_PRESENT) == 0ULL) {
        return CLKS_FALSE;
    }

    pdpt = (u64 *)clks_boot_phys_to_virt(pml4[pml4_index] & CLKS_VM_PTE_ADDR_MASK);
    if (pdpt == CLKS_NULL || (pdpt[pdpt_index] & CLKS_VM_PTE_PRESENT) == 0ULL) {
        return CLKS_FALSE;
    }

    if ((pdpt[pdpt_index] & CLKS_VM_PTE_PS) != 0ULL) {
        u64 base = pdpt[pdpt_index] & 0x000FFFFFC0000000ULL;
        if (out_phys_addr != CLKS_NULL) {
            *out_phys_addr = base + (virt_addr & ((1ULL << 30U) - 1ULL));
        }
        if (out_flags != CLKS_NULL) {
            *out_flags = pdpt[pdpt_index];
        }
        return CLKS_TRUE;
    }

    pd = (u64 *)clks_boot_phys_to_virt(pdpt[pdpt_index] & CLKS_VM_PTE_ADDR_MASK);
    if (pd == CLKS_NULL || (pd[pd_index] & CLKS_VM_PTE_PRESENT) == 0ULL) {
        return CLKS_FALSE;
    }

    if ((pd[pd_index] & CLKS_VM_PTE_PS) != 0ULL) {
        u64 base = pd[pd_index] & 0x000FFFFFFFE00000ULL;
        if (out_phys_addr != CLKS_NULL) {
            *out_phys_addr = base + (virt_addr & ((1ULL << 21U) - 1ULL));
        }
        if (out_flags != CLKS_NULL) {
            *out_flags = pd[pd_index];
        }
        return CLKS_TRUE;
    }

    pt = (u64 *)clks_boot_phys_to_virt(pd[pd_index] & CLKS_VM_PTE_ADDR_MASK);
    if (pt == CLKS_NULL) {
        return CLKS_FALSE;
    }

    entry = pt[pt_index];
    if ((entry & CLKS_VM_PTE_PRESENT) == 0ULL) {
        return CLKS_FALSE;
    }

    if (out_phys_addr != CLKS_NULL) {
        *out_phys_addr = (entry & CLKS_VM_PTE_ADDR_MASK) + (virt_addr & (CLKS_VM_PAGE_SIZE - 1ULL));
    }

    if (out_flags != CLKS_NULL) {
        *out_flags = entry;
    }

    return CLKS_TRUE;
}
