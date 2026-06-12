static u64 clks_exec_proc_runtime_ticks(const struct clks_exec_proc_record *proc, u64 now_tick) {
    u64 runtime;

    if (proc == CLKS_NULL || proc->started_tick == 0ULL) {
        return 0ULL;
    }

    runtime = proc->runtime_ticks_accum;

    if (proc->state == CLKS_EXEC_PROC_RUNNING && proc->run_started_tick != 0ULL && now_tick > proc->run_started_tick) {
        runtime += (now_tick - proc->run_started_tick);
    }

    return runtime;
}

static u64 clks_exec_proc_memory_bytes(const struct clks_exec_proc_record *proc) {
    u64 mem = 0ULL;

    if (proc == CLKS_NULL) {
        return 0ULL;
    }

    mem += proc->image_mem_bytes;

    if (proc->run_stack_base != CLKS_NULL && proc->run_stack_bytes != 0ULL) {
        mem += proc->run_stack_bytes;
    } else if (proc->state == CLKS_EXEC_PROC_RUNNING) {
        mem += CLKS_EXEC_RUN_STACK_BYTES;
    }

    mem += proc->user_vm_bytes;

    return mem;
}

u64 clks_exec_proc_count(void) {
    u64 count = 0ULL;
    u32 i;

    for (i = 0U; i < CLKS_EXEC_MAX_PROCS; i++) {
        if (clks_exec_proc_table[i].used == CLKS_TRUE) {
            count++;
        }
    }

    return count;
}

clks_bool clks_exec_proc_pid_at(u64 index, u64 *out_pid) {
    u64 pos = 0ULL;
    u32 i;

    if (out_pid != CLKS_NULL) {
        *out_pid = 0ULL;
    }

    if (out_pid == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < CLKS_EXEC_MAX_PROCS; i++) {
        if (clks_exec_proc_table[i].used != CLKS_TRUE) {
            continue;
        }

        if (pos == index) {
            *out_pid = clks_exec_proc_table[i].pid;
            return CLKS_TRUE;
        }

        pos++;
    }

    return CLKS_FALSE;
}

clks_bool clks_exec_proc_snapshot(u64 pid, struct clks_exec_proc_snapshot *out_snapshot) {
    i32 slot;
    const struct clks_exec_proc_record *proc;
    u64 now_tick;

    if (out_snapshot == CLKS_NULL) {
        return CLKS_FALSE;
    }

    slot = clks_exec_proc_find_slot_by_pid(pid);

    if (slot < 0) {
        return CLKS_FALSE;
    }

    proc = &clks_exec_proc_table[(u32)slot];

    if (proc->used != CLKS_TRUE) {
        return CLKS_FALSE;
    }

    now_tick = clks_interrupts_timer_ticks();
    clks_memset(out_snapshot, 0, sizeof(*out_snapshot));

    out_snapshot->pid = proc->pid;
    out_snapshot->ppid = proc->ppid;
    out_snapshot->state = (u64)proc->state;
    out_snapshot->started_tick = proc->started_tick;
    out_snapshot->exited_tick = proc->exited_tick;
    out_snapshot->exit_status = proc->exit_status;
    out_snapshot->runtime_ticks = clks_exec_proc_runtime_ticks(proc, now_tick);
    out_snapshot->mem_bytes = clks_exec_proc_memory_bytes(proc);
    out_snapshot->tty_index = (u64)proc->tty_index;
    out_snapshot->last_signal = proc->last_signal;
    out_snapshot->last_fault_vector = proc->last_fault_vector;
    out_snapshot->last_fault_error = proc->last_fault_error;
    out_snapshot->last_fault_rip = proc->last_fault_rip;
    out_snapshot->uid = proc->uid;
    out_snapshot->role = proc->user_role;
    clks_exec_copy_path(out_snapshot->path, sizeof(out_snapshot->path), proc->path);
    out_snapshot->main_thread_id = proc->main_thread_id;
    out_snapshot->thread_state = (u64)proc->thread_state;
    out_snapshot->scheduler_task_id = proc->scheduler_task_id;
    out_snapshot->blocked_reason = (u64)proc->blocked_reason;
    out_snapshot->wake_tick = proc->wake_tick;
    out_snapshot->wait_target_pid = proc->wait_target_pid;
    out_snapshot->parent_waiting = (proc->parent_waiting == CLKS_TRUE) ? 1ULL : 0ULL;

    return CLKS_TRUE;
}

clks_bool clks_exec_proc_snapshot_copy(u64 pid, void *out_snapshot, u64 out_size) {
    struct clks_exec_proc_snapshot snap;
    u64 copy_size;

    if (out_snapshot == CLKS_NULL || out_size == 0ULL) {
        return CLKS_FALSE;
    }

    if (clks_exec_proc_snapshot(pid, &snap) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    copy_size = out_size;
    if (copy_size > (u64)sizeof(snap)) {
        copy_size = (u64)sizeof(snap);
    }

    clks_memcpy(out_snapshot, &snap, (usize)copy_size);
    return CLKS_TRUE;
}

u64 clks_exec_proc_kill(u64 pid, u64 signal) {
    i32 slot;
    struct clks_exec_proc_record *proc;
    u64 effective_signal;
    u64 status;
    u64 now_tick;

    if (pid == 0ULL) {
        return (u64)-1;
    }

    slot = clks_exec_proc_find_slot_by_pid(pid);

    if (slot < 0) {
        return (u64)-1;
    }

    proc = &clks_exec_proc_table[(u32)slot];

    if (proc->used != CLKS_TRUE) {
        return (u64)-1;
    }

    effective_signal = (signal == 0ULL) ? CLKS_EXEC_DEFAULT_KILL_SIGNAL : (signal & 0xFFULL);
    status = clks_exec_encode_signal_status(effective_signal, 0ULL, 0ULL);
    now_tick = clks_interrupts_timer_ticks();

    if (proc->state == CLKS_EXEC_PROC_EXITED) {
        return 1ULL;
    }

    proc->last_signal = effective_signal;
    proc->last_fault_vector = 0ULL;
    proc->last_fault_error = 0ULL;
    proc->last_fault_rip = 0ULL;

    if (effective_signal == CLKS_EXEC_SIGNAL_CONT) {
        if (proc->state == CLKS_EXEC_PROC_STOPPED) {
            clks_exec_proc_set_state(proc, CLKS_EXEC_PROC_PENDING, CLKS_EXEC_THREAD_READY,
                                     CLKS_EXEC_BLOCK_REASON_NONE);
        }
        return 1ULL;
    }

    if (effective_signal == CLKS_EXEC_SIGNAL_STOP) {
        if (proc->state == CLKS_EXEC_PROC_PENDING) {
            clks_exec_proc_set_state(proc, CLKS_EXEC_PROC_STOPPED, CLKS_EXEC_THREAD_STOPPED,
                                     CLKS_EXEC_BLOCK_REASON_NONE);
            return 1ULL;
        }

        if (proc->state == CLKS_EXEC_PROC_STOPPED) {
            return 1ULL;
        }
    }

    if (proc->state == CLKS_EXEC_PROC_PENDING || proc->state == CLKS_EXEC_PROC_STOPPED) {
        clks_exec_proc_mark_exited(proc, now_tick, status);
        if (effective_signal == CLKS_EXEC_SIGNAL_TERM || effective_signal == CLKS_EXEC_SIGNAL_KILL ||
            effective_signal == CLKS_EXEC_SIGNAL_STOP) {
            clks_exec_log_info_serial("TERMINATED BY USER");
            clks_exec_log_info_serial(proc->path);
            clks_exec_log_hex_serial("PID", proc->pid);
            clks_exec_log_hex_serial("SIGNAL", effective_signal);
        }
        return 1ULL;
    }

    if (proc->state == CLKS_EXEC_PROC_RUNNING) {
        i32 depth_index = clks_exec_current_depth_index();

        if (depth_index < 0 || clks_exec_current_pid() != pid) {
            return 0ULL;
        }

        if (effective_signal == CLKS_EXEC_SIGNAL_STOP) {
            clks_exec_exit_requested_stack[(u32)depth_index] = CLKS_TRUE;
            clks_exec_exit_status_stack[(u32)depth_index] = status;
            clks_exec_stop_requested_stack[(u32)depth_index] = CLKS_TRUE;
            return 1ULL;
        }

        clks_exec_exit_requested_stack[(u32)depth_index] = CLKS_TRUE;
        clks_exec_exit_status_stack[(u32)depth_index] = status;
        return 1ULL;
    }

    return 0ULL;
}

u64 clks_exec_force_stop_tty_running_process(u32 tty_index, u64 *out_pid) {
    u32 tty_count = clks_tty_count();
    u64 current_pid;
    i32 slot;
    u32 i;

    if (out_pid != CLKS_NULL) {
        *out_pid = 0ULL;
    }

    if (tty_count == 0U) {
        tty_count = 1U;
    }

    if (tty_index >= tty_count) {
        tty_index = 0U;
    }

    current_pid = clks_exec_current_pid();

    if (current_pid != 0ULL) {
        slot = clks_exec_proc_find_slot_by_pid(current_pid);

        if (slot >= 0) {
            const struct clks_exec_proc_record *proc = &clks_exec_proc_table[(u32)slot];

            if (proc->used == CLKS_TRUE && proc->state == CLKS_EXEC_PROC_RUNNING && proc->tty_index == tty_index &&
                clks_exec_path_is_user_program(proc->path) == CLKS_TRUE) {
                u64 ret = clks_exec_proc_kill(current_pid, CLKS_EXEC_SIGNAL_STOP);

                if (ret == 1ULL && out_pid != CLKS_NULL) {
                    *out_pid = current_pid;
                }

                return ret;
            }
        }
    }

    for (i = 0U; i < CLKS_EXEC_MAX_PROCS; i++) {
        u64 ret;

        if (clks_exec_proc_table[i].used != CLKS_TRUE || clks_exec_proc_table[i].state != CLKS_EXEC_PROC_RUNNING ||
            clks_exec_proc_table[i].tty_index != tty_index ||
            clks_exec_path_is_user_program(clks_exec_proc_table[i].path) == CLKS_FALSE) {
            continue;
        }

        if (out_pid != CLKS_NULL) {
            *out_pid = clks_exec_proc_table[i].pid;
        }

        ret = clks_exec_proc_kill(clks_exec_proc_table[i].pid, CLKS_EXEC_SIGNAL_STOP);

        if (ret == 1ULL) {
            return 1ULL;
        }
    }

    return 0ULL;
}

