#ifndef CLKS_TASK_H
#define CLKS_TASK_H

#include <clks/types.h>

#define CLKS_TASK_NAME_MAX 32U

typedef void (*clks_task_entry_fn)(u64 tick);

enum clks_task_state {
    CLKS_TASK_UNUSED = 0,
    CLKS_TASK_READY = 1,
    CLKS_TASK_RUNNING = 2,
    CLKS_TASK_BLOCKED = 3,
    CLKS_TASK_SLEEPING = 4,
    CLKS_TASK_WAITING_CHILD = 5,
    CLKS_TASK_BLOCKED_IO = 6,
    CLKS_TASK_STOPPED = 7,
    CLKS_TASK_ZOMBIE = 8,
};

struct clks_task_descriptor {
    u32 id;
    char name[CLKS_TASK_NAME_MAX];
    enum clks_task_state state;
    u32 time_slice_ticks;
    u32 remaining_ticks;
    u64 total_ticks;
    u64 switch_count;
    u64 run_count;
    u64 last_run_tick;
    u64 wake_tick;
    u64 sleep_count;
    u64 block_count;
    u64 yield_count;
    clks_task_entry_fn entry;
};

#endif
