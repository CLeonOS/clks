#ifndef CLKS_KERNEL_H
#define CLKS_KERNEL_H

#include <clks/types.h>

void clks_kernel_main(void);
void clks_kernel_entry(u64 boot_magic, void *boot_info);

#endif
