#ifndef CLKS_SCHEDULER_H
#define CLKS_SCHEDULER_H

#include <clks/task.h>
#include <clks/types.h>

struct clks_scheduler_stats {
    u32 task_count;
    u32 current_task_id;
    u32 runnable_count;
    u32 sleeping_count;
    u32 blocked_count;
    u64 total_timer_ticks;
    u64 context_switch_count;
    u64 yield_count;
    u64 wakeup_count;
};

void clks_scheduler_init(void);
clks_bool clks_scheduler_add_kernel_task(const char *name, u32 time_slice_ticks);
clks_bool clks_scheduler_add_kernel_task_ex(const char *name, u32 time_slice_ticks, clks_task_entry_fn entry);
void clks_scheduler_on_timer_tick(u64 tick);
void clks_scheduler_dispatch_current(u64 tick);
void clks_scheduler_dispatch_ready(u64 tick);
void clks_scheduler_yield_current(void);
clks_bool clks_scheduler_sleep_current_until(u64 wake_tick);
clks_bool clks_scheduler_sleep_current_ticks(u64 ticks, u64 now_tick);
clks_bool clks_scheduler_set_task_state(u32 task_id, enum clks_task_state state);
clks_bool clks_scheduler_wake_task(u32 task_id);
struct clks_scheduler_stats clks_scheduler_get_stats(void);
const struct clks_task_descriptor *clks_scheduler_get_task(u32 task_id);

#endif
