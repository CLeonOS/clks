#ifndef CLKS_INTERRUPTS_H
#define CLKS_INTERRUPTS_H

#include <clks/types.h>

void clks_interrupts_init(void);
void clks_interrupts_drain_ps2_input(void);
u64 clks_interrupts_timer_ticks(void);
u32 clks_interrupts_timer_hz(void);

#endif
