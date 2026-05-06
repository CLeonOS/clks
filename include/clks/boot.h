#ifndef CLKS_BOOT_H
#define CLKS_BOOT_H

#include <clks/limine.h>
#include <clks/types.h>

clks_bool clks_boot_base_revision_supported(void);
const struct limine_framebuffer *clks_boot_get_framebuffer(void);
const struct limine_memmap_response *clks_boot_get_memmap(void);
const struct limine_file *clks_boot_get_executable_file(void);
const char *clks_boot_get_cmdline(void);
clks_bool clks_boot_cmdline_flag_enabled(const char *name);
clks_bool clks_boot_rescue_mode(void);
u64 clks_boot_get_module_count(void);
const struct limine_file *clks_boot_get_module(u64 index);
u64 clks_boot_get_hhdm_offset(void);
void *clks_boot_phys_to_virt(u64 phys_addr);

#endif
