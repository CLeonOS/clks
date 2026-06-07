#include <clks/cpu.h>
#include <clks/kernel.h>

void _start(u64 boot_magic, void *boot_info) {
    clks_kernel_entry(boot_magic, boot_info);
    clks_cpu_halt_forever();
}
