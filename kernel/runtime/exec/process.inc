static i32 clks_exec_proc_find_slot_by_pid(u64 pid) {
    u32 i;

    for (i = 0U; i < CLKS_EXEC_MAX_PROCS; i++) {
        if (clks_exec_proc_table[i].used == CLKS_TRUE && clks_exec_proc_table[i].pid == pid) {
            return (i32)i;
        }
    }

    return -1;
}

static i32 clks_exec_proc_alloc_slot(void) {
    u32 i;

    /* First pass: find free slot. Second pass: recycle corpses. Brutal but effective. */
    for (i = 0U; i < CLKS_EXEC_MAX_PROCS; i++) {
        if (clks_exec_proc_table[i].used == CLKS_FALSE) {
            return (i32)i;
        }
    }

    for (i = 0U; i < CLKS_EXEC_MAX_PROCS; i++) {
        if (clks_exec_proc_table[i].state == CLKS_EXEC_PROC_EXITED) {
            return (i32)i;
        }
    }

    return -1;
}

static i32 clks_exec_current_depth_index(void) {
    if (clks_exec_pid_stack_depth == 0U) {
        return -1;
    }

    return (i32)(clks_exec_pid_stack_depth - 1U);
}

static u64 clks_exec_alloc_pid(void) {
    u64 pid = clks_exec_next_pid;

    clks_exec_next_pid++;

    if (clks_exec_next_pid == 0ULL) {
        clks_exec_next_pid = 1ULL;
    }

    if (pid == 0ULL) {
        pid = clks_exec_next_pid;
        clks_exec_next_pid++;

        if (clks_exec_next_pid == 0ULL) {
            clks_exec_next_pid = 1ULL;
        }
    }

    return pid;
}

static enum clks_task_state clks_exec_proc_to_task_state(enum clks_exec_proc_state proc_state,
                                                         enum clks_exec_thread_state thread_state,
                                                         enum clks_exec_block_reason blocked_reason) {
    if (proc_state == CLKS_EXEC_PROC_EXITED) {
        return CLKS_TASK_ZOMBIE;
    }

    if (proc_state == CLKS_EXEC_PROC_STOPPED || thread_state == CLKS_EXEC_THREAD_STOPPED) {
        return CLKS_TASK_STOPPED;
    }

    if (blocked_reason == CLKS_EXEC_BLOCK_REASON_SLEEP || thread_state == CLKS_EXEC_THREAD_SLEEPING) {
        return CLKS_TASK_SLEEPING;
    }

    if (blocked_reason == CLKS_EXEC_BLOCK_REASON_WAIT_CHILD) {
        return CLKS_TASK_WAITING_CHILD;
    }

    if (blocked_reason == CLKS_EXEC_BLOCK_REASON_TTY_INPUT || blocked_reason == CLKS_EXEC_BLOCK_REASON_PTY_INPUT ||
        blocked_reason == CLKS_EXEC_BLOCK_REASON_IO) {
        return CLKS_TASK_BLOCKED_IO;
    }

    if (thread_state == CLKS_EXEC_THREAD_BLOCKED) {
        return CLKS_TASK_BLOCKED;
    }

    if (proc_state == CLKS_EXEC_PROC_RUNNING || thread_state == CLKS_EXEC_THREAD_RUNNING) {
        return CLKS_TASK_RUNNING;
    }

    return CLKS_TASK_READY;
}

static clks_bool clks_exec_proc_bind_scheduler_task(struct clks_exec_proc_record *proc) {
    u32 task_id = 0xFFFFFFFFU;

    if (proc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (proc->scheduler_task_id != 0ULL && proc->scheduler_task_id != CLKS_EXEC_SCHED_TASK_INVALID) {
        return CLKS_TRUE;
    }

    if (clks_scheduler_is_online() == CLKS_FALSE) {
        proc->scheduler_task_id = CLKS_EXEC_SCHED_TASK_INVALID;
        return CLKS_FALSE;
    }

    if (clks_scheduler_add_kernel_task_ex_id(proc->path, 1U, CLKS_NULL, &task_id) == CLKS_FALSE) {
        proc->scheduler_task_id = CLKS_EXEC_SCHED_TASK_INVALID;
        return CLKS_FALSE;
    }

    proc->scheduler_task_id = (u64)task_id;
    return CLKS_TRUE;
}

static void clks_exec_proc_sync_scheduler_state(struct clks_exec_proc_record *proc) {
    enum clks_task_state task_state;

    if (proc == CLKS_NULL) {
        return;
    }

    if (proc->scheduler_task_id == 0ULL || proc->scheduler_task_id == CLKS_EXEC_SCHED_TASK_INVALID) {
        if (clks_exec_proc_bind_scheduler_task(proc) == CLKS_FALSE) {
            return;
        }
    }

    if (proc->scheduler_task_id > 0xFFFFFFFFULL) {
        return;
    }

    task_state = clks_exec_proc_to_task_state(proc->state, proc->thread_state, proc->blocked_reason);
    (void)clks_scheduler_set_task_state((u32)proc->scheduler_task_id, task_state);

    if (task_state == CLKS_TASK_SLEEPING && proc->wake_tick != 0ULL) {
        (void)clks_scheduler_sleep_task_until((u32)proc->scheduler_task_id, proc->wake_tick);
    }
}

static void clks_exec_proc_set_state(struct clks_exec_proc_record *proc, enum clks_exec_proc_state proc_state,
                                     enum clks_exec_thread_state thread_state,
                                     enum clks_exec_block_reason blocked_reason) {
    if (proc == CLKS_NULL) {
        return;
    }

    proc->state = proc_state;
    proc->thread_state = thread_state;
    proc->blocked_reason = blocked_reason;

    if (blocked_reason == CLKS_EXEC_BLOCK_REASON_NONE) {
        proc->wake_tick = 0ULL;
        proc->wait_target_pid = 0ULL;
    }

    clks_exec_proc_sync_scheduler_state(proc);
}

static void clks_exec_proc_set_block(struct clks_exec_proc_record *proc,
                                     enum clks_exec_block_reason blocked_reason, u64 wake_tick,
                                     u64 wait_target_pid) {
    if (proc == CLKS_NULL) {
        return;
    }

    proc->blocked_reason = blocked_reason;
    proc->wake_tick = wake_tick;
    proc->wait_target_pid = wait_target_pid;

    if (blocked_reason == CLKS_EXEC_BLOCK_REASON_SLEEP) {
        proc->thread_state = CLKS_EXEC_THREAD_SLEEPING;
    } else if (blocked_reason != CLKS_EXEC_BLOCK_REASON_NONE) {
        proc->thread_state = CLKS_EXEC_THREAD_BLOCKED;
    }

    clks_exec_proc_sync_scheduler_state(proc);
}

static void clks_exec_proc_clear_block(struct clks_exec_proc_record *proc) {
    if (proc == CLKS_NULL) {
        return;
    }

    proc->blocked_reason = CLKS_EXEC_BLOCK_REASON_NONE;
    proc->wake_tick = 0ULL;
    proc->wait_target_pid = 0ULL;

    if (proc->state == CLKS_EXEC_PROC_PENDING) {
        proc->thread_state = CLKS_EXEC_THREAD_READY;
    } else if (proc->state == CLKS_EXEC_PROC_RUNNING) {
        proc->thread_state = CLKS_EXEC_THREAD_RUNNING;
    }

    clks_exec_proc_sync_scheduler_state(proc);
}

