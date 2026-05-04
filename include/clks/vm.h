#ifndef CLKS_VM_H
#define CLKS_VM_H

#include <clks/types.h>

#define CLKS_VM_PAGE_SIZE 4096ULL

#define CLKS_VM_FLAG_READ 0x1ULL
#define CLKS_VM_FLAG_WRITE 0x2ULL
#define CLKS_VM_FLAG_EXEC 0x4ULL
#define CLKS_VM_FLAG_USER 0x8ULL
#define CLKS_VM_FLAG_MMIO 0x10ULL

void clks_vm_init(void);
u64 clks_vm_current_cr3(void);
u64 clks_vm_kernel_cr3(void);
void clks_vm_switch_cr3(u64 cr3);
u64 clks_vm_create_address_space(void);
void clks_vm_destroy_address_space(u64 cr3);
clks_bool clks_vm_map_page_current(u64 virt_addr, u64 phys_addr, u64 flags);
clks_bool clks_vm_protect_page_current(u64 virt_addr, u64 flags);
clks_bool clks_vm_unmap_page_current(u64 virt_addr, u64 *out_phys_addr);
clks_bool clks_vm_translate_current(u64 virt_addr, u64 *out_phys_addr, u64 *out_flags);

#endif
