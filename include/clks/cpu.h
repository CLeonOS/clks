#ifndef CLKS_CPU_H
#define CLKS_CPU_H

#include <clks/compiler.h>
#include <clks/types.h>

static inline void clks_cpu_pause(void) {
#if defined(CLKS_ARCH_X86_64)
    __asm__ volatile("pause");
#elif defined(CLKS_ARCH_AARCH64)
    __asm__ volatile("yield");
#endif
}

static inline void clks_cpu_init_fpu(void) {
#if defined(CLKS_ARCH_X86_64)
    u64 cr0;
    u64 cr4;
    u32 mxcsr = 0x1F80U;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2U); /* EM=0: allow x87/MMX/SSE instead of trapping as #UD. */
    cr0 &= ~(1ULL << 3U); /* TS=0: no lazy-FPU trap until we implement full context switching. */
    cr0 |= (1ULL << 1U);  /* MP=1: sane WAIT/FWAIT behavior. */
    cr0 |= (1ULL << 5U);  /* NE=1: native x87 exceptions. */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9U);  /* OSFXSR: OS supports FXSAVE/FXRSTOR and SSE state. */
    cr4 |= (1ULL << 10U); /* OSXMMEXCPT: OS supports unmasked SIMD FP exceptions. */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    __asm__ volatile("fninit" : : : "memory");
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr) : "memory");
#endif
}

static inline CLKS_NORETURN void clks_cpu_halt_forever(void) {
    for (;;) {
#if defined(CLKS_ARCH_X86_64)
        __asm__ volatile("hlt");
#elif defined(CLKS_ARCH_AARCH64)
        __asm__ volatile("wfe");
#endif
    }
}

#endif
