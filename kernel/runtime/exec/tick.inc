u64 clks_exec_sleep_ticks(u64 ticks) {
    u64 start = clks_interrupts_timer_ticks();
    struct clks_exec_proc_record *proc = clks_exec_current_proc();

    if (ticks == 0ULL) {
        if (proc != CLKS_NULL) {
            clks_exec_proc_set_block(proc, CLKS_EXEC_BLOCK_REASON_YIELD, 0ULL, 0ULL);
        }
        clks_scheduler_yield_current();
        if (proc != CLKS_NULL) {
            clks_exec_proc_clear_block(proc);
        }
        return 0ULL;
    }

    if (proc != CLKS_NULL) {
        clks_exec_proc_set_block(proc, CLKS_EXEC_BLOCK_REASON_SLEEP, start + ticks, 0ULL);
    }
    (void)clks_scheduler_sleep_current_ticks(ticks, start);

    while ((clks_interrupts_timer_ticks() - start) < ticks) {
#if defined(CLKS_ARCH_X86_64)
        __asm__ volatile("sti; hlt; cli" : : : "memory");
#elif defined(CLKS_ARCH_AARCH64)
        clks_cpu_pause();
#endif
        (void)clks_exec_dispatch_pending_once();
    }

    if (proc != CLKS_NULL) {
        clks_exec_proc_clear_block(proc);
    }
    return clks_interrupts_timer_ticks() - start;
}

u64 clks_exec_yield(void) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();

    if (proc != CLKS_NULL) {
        clks_exec_proc_set_block(proc, CLKS_EXEC_BLOCK_REASON_YIELD, 0ULL, 0ULL);
    }
    clks_scheduler_yield_current();

#if defined(CLKS_ARCH_X86_64)
    __asm__ volatile("sti; hlt; cli" : : : "memory");
#elif defined(CLKS_ARCH_AARCH64)
    clks_cpu_pause();
#endif

    (void)clks_exec_dispatch_pending_once();
    if (proc != CLKS_NULL) {
        clks_exec_proc_clear_block(proc);
    }
    return clks_interrupts_timer_ticks();
}

void clks_exec_tick(u64 tick) {
    (void)tick;
    /* Background pump: boring, repetitive, absolutely necessary. */
    (void)clks_exec_dispatch_pending_once();
}

u64 clks_exec_request_count(void) {
    return clks_exec_requests;
}

u64 clks_exec_success_count(void) {
    return clks_exec_success;
}

clks_bool clks_exec_is_running(void) {
    return (clks_exec_running_depth > 0U) ? CLKS_TRUE : CLKS_FALSE;
}

clks_bool clks_exec_has_user_process(void) {
    u32 i;

    for (i = 0U; i < CLKS_EXEC_MAX_PROCS; i++) {
        const struct clks_exec_proc_record *proc = &clks_exec_proc_table[i];

        if (proc->used != CLKS_TRUE) {
            continue;
        }

        if (proc->state != CLKS_EXEC_PROC_PENDING && proc->state != CLKS_EXEC_PROC_RUNNING &&
            proc->state != CLKS_EXEC_PROC_STOPPED) {
            continue;
        }

        if (clks_exec_path_is_user_program(proc->path) == CLKS_TRUE) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

clks_bool clks_exec_tty_has_user_process(u32 tty_index) {
    u32 tty_count = clks_tty_count();
    u32 i;

    if (tty_count == 0U) {
        tty_count = 1U;
    }

    if (tty_index >= tty_count) {
        tty_index = 0U;
    }

    for (i = 0U; i < CLKS_EXEC_MAX_PROCS; i++) {
        const struct clks_exec_proc_record *proc = &clks_exec_proc_table[i];

        if (proc->used != CLKS_TRUE) {
            continue;
        }

        if (proc->tty_index != tty_index) {
            continue;
        }

        if (proc->state != CLKS_EXEC_PROC_PENDING && proc->state != CLKS_EXEC_PROC_RUNNING) {
            continue;
        }

        if (clks_exec_path_is_user_program(proc->path) == CLKS_TRUE) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

clks_bool clks_exec_current_path_is_user(void) {
    i32 depth_index;
    i32 slot;
    const struct clks_exec_proc_record *proc;

    depth_index = clks_exec_current_depth_index();

    if (depth_index < 0) {
        return CLKS_FALSE;
    }

    slot = clks_exec_proc_find_slot_by_pid(clks_exec_pid_stack[(u32)depth_index]);

    if (slot < 0) {
        return CLKS_FALSE;
    }

    proc = &clks_exec_proc_table[(u32)slot];
    return clks_exec_path_is_user_program(proc->path);
}

clks_bool clks_exec_current_user_ptr_readable(u64 addr, u64 size) {
    i32 depth_index;
    u64 image_begin;
    u64 image_end;
    u64 stack_begin;
    u64 stack_end;

    if (clks_exec_is_running() == CLKS_FALSE || clks_exec_current_path_is_user() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (size == 0ULL || clks_exec_pid_stack_depth == 0U) {
        return CLKS_FALSE;
    }

    depth_index = (i32)(clks_exec_pid_stack_depth - 1U);
    image_begin = clks_exec_image_begin_stack[(u32)depth_index];
    image_end = clks_exec_image_end_stack[(u32)depth_index];
    stack_begin = clks_exec_stack_begin_stack[(u32)depth_index];
    stack_end = clks_exec_stack_end_stack[(u32)depth_index];

    if (clks_exec_addr_range_in_window(addr, size, image_begin, image_end) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    if (clks_exec_addr_range_in_window(addr, size, stack_begin, stack_end) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    if (clks_exec_current_user_vm_ptr_range(addr, size, CLKS_VM_FLAG_READ) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

clks_bool clks_exec_current_user_ptr_writable(u64 addr, u64 size) {
    i32 depth_index;
    u64 image_begin;
    u64 image_end;
    u64 stack_begin;
    u64 stack_end;

    if (clks_exec_is_running() == CLKS_FALSE || clks_exec_current_path_is_user() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (size == 0ULL || clks_exec_pid_stack_depth == 0U) {
        return CLKS_FALSE;
    }

    depth_index = (i32)(clks_exec_pid_stack_depth - 1U);
    image_begin = clks_exec_image_begin_stack[(u32)depth_index];
    image_end = clks_exec_image_end_stack[(u32)depth_index];
    stack_begin = clks_exec_stack_begin_stack[(u32)depth_index];
    stack_end = clks_exec_stack_end_stack[(u32)depth_index];

    /*
     * The legacy exec loader still keeps writable data inside one kmalloc image.
     * Keep that compatible until ELF segments are mapped with per-page permissions.
     */
    if (clks_exec_addr_range_in_window(addr, size, image_begin, image_end) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    if (clks_exec_addr_range_in_window(addr, size, stack_begin, stack_end) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    if (clks_exec_current_user_vm_ptr_range(addr, size, CLKS_VM_FLAG_READ | CLKS_VM_FLAG_WRITE) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}
