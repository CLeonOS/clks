#include <clks/log.h>
#include <clks/scheduler.h>
#include <clks/string.h>
#include <clks/types.h>

#define CLKS_SCHED_MAX_TASKS 32U
#define CLKS_SCHED_MIN_SLICE 1U
#define CLKS_SCHED_DEFAULT_DISPATCH_BUDGET 32U

static struct clks_task_descriptor clks_tasks[CLKS_SCHED_MAX_TASKS];
static u32 clks_task_count = 0U;
static u32 clks_current_task = 0U;
static u64 clks_total_timer_ticks = 0ULL;
static u64 clks_context_switch_count = 0ULL;
static u64 clks_yield_count = 0ULL;
static u64 clks_wakeup_count = 0ULL;
static clks_bool clks_dispatch_active = CLKS_FALSE;
static clks_bool clks_reschedule_requested = CLKS_FALSE;

static void clks_sched_copy_name(char *dst, const char *src) {
    u32 i = 0U;

    if (dst == CLKS_NULL) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (i < (CLKS_TASK_NAME_MAX - 1U) && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static clks_bool clks_sched_state_runnable(enum clks_task_state state) {
    return (state == CLKS_TASK_READY || state == CLKS_TASK_RUNNING) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_sched_task_runnable(const struct clks_task_descriptor *task) {
    if (task == CLKS_NULL || task->entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    return clks_sched_state_runnable(task->state);
}

static void clks_sched_wake_sleepers(u64 tick) {
    u32 i;

    for (i = 0U; i < clks_task_count; i++) {
        struct clks_task_descriptor *task = &clks_tasks[i];

        if (task->state != CLKS_TASK_SLEEPING) {
            continue;
        }

        if (tick < task->wake_tick) {
            continue;
        }

        task->wake_tick = 0ULL;
        task->state = CLKS_TASK_READY;
        task->remaining_ticks = task->time_slice_ticks;
        clks_wakeup_count++;
    }
}

static u32 clks_sched_next_runnable_task(u32 from) {
    u32 i;

    if (clks_task_count == 0U) {
        return 0U;
    }

    for (i = 1U; i <= clks_task_count; i++) {
        u32 idx = (from + i) % clks_task_count;

        if (clks_sched_task_runnable(&clks_tasks[idx]) == CLKS_TRUE) {
            return idx;
        }
    }

    if (clks_sched_task_runnable(&clks_tasks[from]) == CLKS_TRUE) {
        return from;
    }

    return 0U;
}

static void clks_sched_switch_to(u32 next) {
    struct clks_task_descriptor *old_task;
    struct clks_task_descriptor *new_task;

    if (clks_task_count == 0U || next >= clks_task_count) {
        return;
    }

    if (next == clks_current_task) {
        return;
    }

    old_task = &clks_tasks[clks_current_task];
    new_task = &clks_tasks[next];

    if (old_task->state == CLKS_TASK_RUNNING) {
        old_task->state = CLKS_TASK_READY;
    }

    clks_current_task = next;
    new_task->state = CLKS_TASK_RUNNING;
    new_task->switch_count++;
    if (new_task->remaining_ticks == 0U) {
        new_task->remaining_ticks = new_task->time_slice_ticks;
    }
    clks_context_switch_count++;
}

static void clks_sched_account_current_tick(void) {
    struct clks_task_descriptor *current;

    if (clks_task_count == 0U) {
        return;
    }

    current = &clks_tasks[clks_current_task];
    if (clks_sched_state_runnable(current->state) == CLKS_FALSE) {
        return;
    }

    current->total_ticks++;
    if (current->remaining_ticks > 0U) {
        current->remaining_ticks--;
    }

    if (current->remaining_ticks == 0U) {
        clks_reschedule_requested = CLKS_TRUE;
    }
}

static void clks_sched_maybe_reschedule(void) {
    u32 next;

    if (clks_task_count == 0U || clks_reschedule_requested == CLKS_FALSE) {
        return;
    }

    clks_reschedule_requested = CLKS_FALSE;
    next = clks_sched_next_runnable_task(clks_current_task);
    clks_tasks[clks_current_task].remaining_ticks = clks_tasks[clks_current_task].time_slice_ticks;
    clks_sched_switch_to(next);
}

static void clks_sched_run_task(u32 task_id, u64 tick) {
    struct clks_task_descriptor *task;

    if (task_id >= clks_task_count) {
        return;
    }

    task = &clks_tasks[task_id];
    if (clks_sched_task_runnable(task) == CLKS_FALSE) {
        return;
    }

    if (task->state == CLKS_TASK_READY) {
        clks_sched_switch_to(task_id);
    }

    task->state = CLKS_TASK_RUNNING;
    task->run_count++;
    task->last_run_tick = tick;

    clks_dispatch_active = CLKS_TRUE;
    task->entry(tick);
    clks_dispatch_active = CLKS_FALSE;

    if (task->state == CLKS_TASK_RUNNING) {
        task->state = CLKS_TASK_READY;
    }

    clks_sched_maybe_reschedule();
}

void clks_scheduler_init(void) {
    clks_memset(clks_tasks, 0, sizeof(clks_tasks));
    clks_task_count = 0U;
    clks_current_task = 0U;
    clks_total_timer_ticks = 0ULL;
    clks_context_switch_count = 0ULL;
    clks_yield_count = 0ULL;
    clks_wakeup_count = 0ULL;
    clks_dispatch_active = CLKS_FALSE;
    clks_reschedule_requested = CLKS_FALSE;

    (void)clks_scheduler_add_kernel_task_ex("idle", 1U, CLKS_NULL);
    clks_tasks[0].state = CLKS_TASK_RUNNING;
    clks_tasks[0].remaining_ticks = clks_tasks[0].time_slice_ticks;

    clks_log(CLKS_LOG_INFO, "SCHED", "STATEFUL SCHEDULER ONLINE");
}

clks_bool clks_scheduler_add_kernel_task_ex(const char *name, u32 time_slice_ticks, clks_task_entry_fn entry) {
    struct clks_task_descriptor *task;

    if (name == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_task_count >= CLKS_SCHED_MAX_TASKS) {
        return CLKS_FALSE;
    }

    if (time_slice_ticks < CLKS_SCHED_MIN_SLICE) {
        time_slice_ticks = CLKS_SCHED_MIN_SLICE;
    }

    task = &clks_tasks[clks_task_count];
    clks_memset(task, 0, sizeof(*task));
    task->id = clks_task_count;
    clks_sched_copy_name(task->name, name);
    task->state = CLKS_TASK_READY;
    task->time_slice_ticks = time_slice_ticks;
    task->remaining_ticks = time_slice_ticks;
    task->wake_tick = 0ULL;
    task->entry = entry;

    clks_task_count++;
    return CLKS_TRUE;
}

clks_bool clks_scheduler_add_kernel_task(const char *name, u32 time_slice_ticks) {
    return clks_scheduler_add_kernel_task_ex(name, time_slice_ticks, CLKS_NULL);
}

void clks_scheduler_dispatch_current(u64 tick) {
    if (clks_task_count == 0U || clks_dispatch_active == CLKS_TRUE) {
        return;
    }

    clks_sched_wake_sleepers(tick);
    clks_sched_maybe_reschedule();
    clks_sched_run_task(clks_current_task, tick);
}

void clks_scheduler_dispatch_ready(u64 tick) {
    u32 budget = CLKS_SCHED_DEFAULT_DISPATCH_BUDGET;
    u32 i;

    if (clks_task_count == 0U || clks_dispatch_active == CLKS_TRUE) {
        return;
    }

    clks_sched_wake_sleepers(tick);

    for (i = 0U; i < clks_task_count && budget > 0U; i++) {
        u32 idx = (clks_current_task + i) % clks_task_count;

        if (idx == 0U) {
            continue;
        }

        if (clks_sched_task_runnable(&clks_tasks[idx]) == CLKS_FALSE) {
            continue;
        }

        clks_sched_run_task(idx, tick);
        budget--;
    }

    if (budget == CLKS_SCHED_DEFAULT_DISPATCH_BUDGET) {
        clks_scheduler_dispatch_current(tick);
    }
}

void clks_scheduler_on_timer_tick(u64 tick) {
    if (clks_task_count == 0U) {
        return;
    }

    clks_total_timer_ticks = tick;
    clks_sched_wake_sleepers(tick);

    if (clks_dispatch_active == CLKS_TRUE) {
        return;
    }

    clks_sched_account_current_tick();
    clks_sched_maybe_reschedule();
}

void clks_scheduler_yield_current(void) {
    struct clks_task_descriptor *current;

    if (clks_task_count == 0U) {
        return;
    }

    current = &clks_tasks[clks_current_task];
    if (clks_sched_state_runnable(current->state) == CLKS_TRUE) {
        current->yield_count++;
        current->remaining_ticks = 0U;
        clks_yield_count++;
        clks_reschedule_requested = CLKS_TRUE;
    }
}

clks_bool clks_scheduler_sleep_current_until(u64 wake_tick) {
    struct clks_task_descriptor *current;

    if (clks_task_count == 0U) {
        return CLKS_FALSE;
    }

    current = &clks_tasks[clks_current_task];
    if (clks_current_task == 0U || current->entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    current->state = CLKS_TASK_SLEEPING;
    current->wake_tick = wake_tick;
    current->sleep_count++;
    current->remaining_ticks = current->time_slice_ticks;
    clks_reschedule_requested = CLKS_TRUE;
    return CLKS_TRUE;
}

clks_bool clks_scheduler_sleep_current_ticks(u64 ticks, u64 now_tick) {
    if (ticks == 0ULL) {
        clks_scheduler_yield_current();
        return CLKS_TRUE;
    }

    return clks_scheduler_sleep_current_until(now_tick + ticks);
}

clks_bool clks_scheduler_set_task_state(u32 task_id, enum clks_task_state state) {
    struct clks_task_descriptor *task;

    if (task_id >= clks_task_count) {
        return CLKS_FALSE;
    }

    task = &clks_tasks[task_id];
    if (task_id == 0U && state != CLKS_TASK_READY && state != CLKS_TASK_RUNNING) {
        return CLKS_FALSE;
    }

    task->state = state;
    if (state == CLKS_TASK_BLOCKED || state == CLKS_TASK_BLOCKED_IO || state == CLKS_TASK_WAITING_CHILD) {
        task->block_count++;
    }
    if (clks_current_task == task_id && clks_sched_state_runnable(state) == CLKS_FALSE) {
        clks_reschedule_requested = CLKS_TRUE;
    }
    return CLKS_TRUE;
}

clks_bool clks_scheduler_wake_task(u32 task_id) {
    struct clks_task_descriptor *task;

    if (task_id >= clks_task_count) {
        return CLKS_FALSE;
    }

    task = &clks_tasks[task_id];
    if (task->state == CLKS_TASK_UNUSED || task->state == CLKS_TASK_ZOMBIE) {
        return CLKS_FALSE;
    }

    task->state = CLKS_TASK_READY;
    task->wake_tick = 0ULL;
    if (task->remaining_ticks == 0U) {
        task->remaining_ticks = task->time_slice_ticks;
    }
    clks_wakeup_count++;
    return CLKS_TRUE;
}

struct clks_scheduler_stats clks_scheduler_get_stats(void) {
    struct clks_scheduler_stats stats;
    u32 i;

    clks_memset(&stats, 0, sizeof(stats));
    stats.task_count = clks_task_count;
    stats.current_task_id = clks_current_task;
    stats.total_timer_ticks = clks_total_timer_ticks;
    stats.context_switch_count = clks_context_switch_count;
    stats.yield_count = clks_yield_count;
    stats.wakeup_count = clks_wakeup_count;

    for (i = 0U; i < clks_task_count; i++) {
        enum clks_task_state state = clks_tasks[i].state;

        if (clks_sched_state_runnable(state) == CLKS_TRUE) {
            stats.runnable_count++;
        } else if (state == CLKS_TASK_SLEEPING) {
            stats.sleeping_count++;
        } else if (state == CLKS_TASK_BLOCKED || state == CLKS_TASK_BLOCKED_IO ||
                   state == CLKS_TASK_WAITING_CHILD) {
            stats.blocked_count++;
        }
    }

    return stats;
}

const struct clks_task_descriptor *clks_scheduler_get_task(u32 task_id) {
    if (task_id >= clks_task_count) {
        return CLKS_NULL;
    }

    return &clks_tasks[task_id];
}
