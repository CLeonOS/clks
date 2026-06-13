static void clks_exec_proc_release_runtime(struct clks_exec_proc_record *proc) {
    if (proc == CLKS_NULL) {
        return;
    }

    clks_exec_proc_release_user_vm(proc);

    if (proc->loaded_active == CLKS_TRUE) {
        clks_elf64_unload(&proc->loaded);
        proc->loaded_active = CLKS_FALSE;
    } else {
        clks_memset(&proc->loaded, 0, sizeof(proc->loaded));
    }

    if (proc->run_stack_base != CLKS_NULL) {
        clks_kfree(proc->run_stack_base);
    }

    proc->run_stack_base = CLKS_NULL;
    proc->run_stack_bytes = 0ULL;
    proc->saved_frame_valid = CLKS_FALSE;
    clks_memset(&proc->saved_frame, 0, sizeof(proc->saved_frame));
}

static void clks_exec_proc_release_address_space(struct clks_exec_proc_record *proc) {
    u64 kernel_cr3;

    if (proc == CLKS_NULL || proc->address_space_cr3 == 0ULL) {
        return;
    }

    kernel_cr3 = clks_vm_kernel_cr3();
    if (kernel_cr3 != 0ULL && proc->address_space_cr3 != kernel_cr3) {
        clks_vm_destroy_address_space(proc->address_space_cr3);
    }

    proc->address_space_cr3 = 0ULL;
}

static struct clks_exec_proc_record *clks_exec_prepare_proc_record(i32 slot, u64 pid, const char *path,
                                                                   const char *argv_line, const char *env_line,
                                                                   enum clks_exec_proc_state state) {
    const struct clks_exec_proc_record *parent_proc = clks_exec_current_proc();
    struct clks_exec_proc_record *proc;

    if (slot < 0 || path == CLKS_NULL) {
        return CLKS_NULL;
    }

    proc = &clks_exec_proc_table[(u32)slot];
    clks_exec_proc_release_runtime(proc);
    clks_exec_proc_release_address_space(proc);
    clks_exec_fd_release_all(proc);
    clks_memset(proc, 0, sizeof(*proc));

    proc->used = CLKS_TRUE;
    proc->pid = pid;
    proc->ppid = clks_exec_current_pid();
    proc->main_thread_id = pid;
    proc->scheduler_task_id = 0ULL;
    proc->parent_waiting = CLKS_FALSE;
    proc->started_tick = 0ULL;
    proc->exited_tick = 0ULL;
    proc->run_started_tick = 0ULL;
    proc->runtime_ticks_accum = 0ULL;
    proc->exit_status = (u64)-1;
    proc->tty_index = clks_tty_active();
    proc->image_mem_bytes = 0ULL;
    proc->address_space_cr3 = clks_vm_create_address_space();
    if (proc->address_space_cr3 == 0ULL) {
        proc->address_space_cr3 = clks_vm_kernel_cr3();
    }
    clks_exec_copy_path(proc->path, sizeof(proc->path), path);
    clks_exec_copy_line(proc->argv_line, sizeof(proc->argv_line), argv_line);
    clks_exec_copy_line(proc->env_line, sizeof(proc->env_line), env_line);
    clks_exec_clear_items(proc->argv_items, CLKS_EXEC_MAX_ARGS);
    clks_exec_clear_items(proc->env_items, CLKS_EXEC_MAX_ENVS);
    proc->argc = 0U;
    proc->envc = 0U;
    proc->last_signal = 0ULL;
    proc->last_fault_vector = 0ULL;
    proc->last_fault_error = 0ULL;
    proc->last_fault_rip = 0ULL;
    clks_exec_proc_set_state(proc, state,
                             (state == CLKS_EXEC_PROC_PENDING) ? CLKS_EXEC_THREAD_READY
                                                               : CLKS_EXEC_THREAD_RUNNING,
                             CLKS_EXEC_BLOCK_REASON_NONE);
    if (clks_exec_proc_bind_scheduler_task(proc) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "EXEC", "SCHEDULER TASK BIND FAILED");
        clks_log(CLKS_LOG_WARN, "EXEC", path);
    }
    clks_exec_proc_inherit_user(proc, parent_proc);
    clks_exec_fd_init_defaults(proc);

    if (proc->argc < CLKS_EXEC_MAX_ARGS) {
        clks_exec_copy_path(proc->argv_items[proc->argc], CLKS_EXEC_ITEM_MAX, path);
        proc->argc++;
    }

    if (proc->argv_line[0] != '\0' && proc->argc < CLKS_EXEC_MAX_ARGS) {
        proc->argc += clks_exec_parse_whitespace_items(proc->argv_line, &proc->argv_items[proc->argc],
                                                       (u32)(CLKS_EXEC_MAX_ARGS - proc->argc));
    }

    if (proc->env_line[0] != '\0') {
        proc->envc = clks_exec_parse_env_items(proc->env_line, proc->env_items, CLKS_EXEC_MAX_ENVS);
    }

    return proc;
}

static void clks_exec_proc_mark_running(struct clks_exec_proc_record *proc, u64 now_tick) {
    if (proc == CLKS_NULL) {
        return;
    }

    if (proc->started_tick == 0ULL) {
        proc->started_tick = now_tick;
    }

    proc->run_started_tick = now_tick;
    clks_exec_proc_set_state(proc, CLKS_EXEC_PROC_RUNNING, CLKS_EXEC_THREAD_RUNNING, CLKS_EXEC_BLOCK_REASON_NONE);
}

static void clks_exec_proc_pause_runtime(struct clks_exec_proc_record *proc, u64 now_tick) {
    if (proc == CLKS_NULL) {
        return;
    }

    if (proc->run_started_tick != 0ULL && now_tick > proc->run_started_tick) {
        proc->runtime_ticks_accum += (now_tick - proc->run_started_tick);
    }

    proc->run_started_tick = 0ULL;
}

static void clks_exec_proc_mark_exited(struct clks_exec_proc_record *proc, u64 now_tick, u64 status) {
    if (proc == CLKS_NULL) {
        return;
    }

    clks_exec_proc_pause_runtime(proc, now_tick);
    clks_exec_proc_release_runtime(proc);
    clks_exec_proc_release_address_space(proc);
    clks_exec_fd_release_all(proc);
    clks_exec_proc_set_state(proc, CLKS_EXEC_PROC_EXITED, CLKS_EXEC_THREAD_ZOMBIE, CLKS_EXEC_BLOCK_REASON_NONE);
    proc->exit_status = status;
    proc->exited_tick = now_tick;
    proc->parent_waiting = CLKS_FALSE;
}

#if defined(CLKS_ARCH_X86_64)
static u64 clks_exec_read_rflags(void) {
    u64 flags = 0ULL;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) : : "memory");
    return flags;
}

static void clks_exec_enable_interrupt_window(void) {
    if ((clks_exec_read_rflags() & (1ULL << 9)) != 0ULL) {
        return;
    }

    __asm__ volatile("sti" : : : "memory");
}
#endif

static clks_bool clks_exec_invoke_entry(struct clks_exec_proc_record *proc, void *entry_ptr, u32 depth_index,
                                        u64 *out_ret) {
    if (proc == CLKS_NULL || out_ret == CLKS_NULL) {
        return CLKS_FALSE;
    }

#if defined(CLKS_ARCH_X86_64)
    {
        static const u64 clks_exec_stack_candidates[] = {
            CLKS_EXEC_RUN_STACK_BYTES,  CLKS_EXEC_RUN_STACK_TIER_1, CLKS_EXEC_RUN_STACK_TIER_2,
            CLKS_EXEC_RUN_STACK_TIER_3, CLKS_EXEC_RUN_STACK_TIER_4, CLKS_EXEC_RUN_STACK_TIER_5,
        };
        void *stack_top;
        u64 stack_bytes = 0ULL;
        u64 unwind_slot;
        u64 call_ret = (u64)-1;
        u32 candidate_index;

        if (proc->saved_frame_valid == CLKS_FALSE) {
            if (entry_ptr == CLKS_NULL) {
                return CLKS_FALSE;
            }

            for (candidate_index = 0U;
                 candidate_index < (u32)(sizeof(clks_exec_stack_candidates) / sizeof(clks_exec_stack_candidates[0]));
                 candidate_index++) {
                stack_bytes = clks_exec_stack_candidates[candidate_index];
                proc->run_stack_base = clks_kmalloc((usize)stack_bytes);
                if (proc->run_stack_base != CLKS_NULL) {
                    proc->run_stack_bytes = stack_bytes;
                    break;
                }
            }

            if (proc->run_stack_base == CLKS_NULL) {
                clks_log(CLKS_LOG_WARN, "EXEC", "RUN STACK ALLOC FAILED");
                return CLKS_FALSE;
            }

            if (stack_bytes != CLKS_EXEC_RUN_STACK_BYTES) {
                clks_log(CLKS_LOG_WARN, "EXEC", "RUN STACK FALLBACK");
                clks_exec_log_hex_serial("STACK_BYTES", stack_bytes);
            }
        }

        if (proc->run_stack_base == CLKS_NULL || proc->run_stack_bytes == 0ULL) {
            return CLKS_FALSE;
        }

        stack_top = (void *)((u8 *)proc->run_stack_base + (usize)proc->run_stack_bytes);
        clks_exec_stack_begin_stack[depth_index] = (u64)proc->run_stack_base;
        clks_exec_stack_end_stack[depth_index] = (u64)stack_top;
        unwind_slot = (((u64)stack_top) & ~0xFULL) - CLKS_EXEC_UNWIND_CTX_BYTES;
        clks_exec_unwind_slot_stack[depth_index] = unwind_slot;
        clks_exec_unwind_slot_valid_stack[depth_index] = CLKS_TRUE;

        clks_exec_enable_interrupt_window();

        if (proc->saved_frame_valid == CLKS_TRUE) {
            call_ret = clks_exec_resume_frame_x86_64(&proc->saved_frame, (void *)unwind_slot);
        } else {
            call_ret = clks_exec_call_on_stack_x86_64(entry_ptr, stack_top);
        }

        /* Close unwind window immediately after call returns to avoid IRQ race. */
        clks_exec_unwind_slot_valid_stack[depth_index] = CLKS_FALSE;
        clks_exec_unwind_slot_stack[depth_index] = 0ULL;
        clks_exec_stack_begin_stack[depth_index] = 0ULL;
        clks_exec_stack_end_stack[depth_index] = 0ULL;

        *out_ret = call_ret;
        if (call_ret != CLKS_EXEC_INTERNAL_SUSPEND_STATUS && proc->run_stack_base != CLKS_NULL) {
            clks_kfree(proc->run_stack_base);
            proc->run_stack_base = CLKS_NULL;
            proc->run_stack_bytes = 0ULL;
            proc->saved_frame_valid = CLKS_FALSE;
            clks_memset(&proc->saved_frame, 0, sizeof(proc->saved_frame));
        }
        return CLKS_TRUE;
    }
#else
    (void)depth_index;
    if (entry_ptr == CLKS_NULL) {
        return CLKS_FALSE;
    }
    *out_ret = ((clks_exec_entry_fn)entry_ptr)();
    return CLKS_TRUE;
#endif
}

static clks_bool clks_exec_run_proc_slot(i32 slot, u64 *out_status) {
    struct clks_exec_proc_record *proc;
    const void *image = CLKS_NULL;
    u64 image_size = 0ULL;
    struct clks_elf64_info info;
    clks_bool resume_existing = CLKS_FALSE;
    clks_bool depth_pushed = CLKS_FALSE;
    clks_bool depth_counted = CLKS_FALSE;
    void *entry_ptr = CLKS_NULL;
    u64 run_ret = (u64)-1;
    i32 depth_index;
    u64 prev_cr3 = 0ULL;
    clks_bool switched_cr3 = CLKS_FALSE;

    clks_memset(&info, 0, sizeof(info));

    if (out_status != CLKS_NULL) {
        *out_status = (u64)-1;
    }

    if (slot < 0 || (u32)slot >= CLKS_EXEC_MAX_PROCS) {
        return CLKS_FALSE;
    }

    proc = &clks_exec_proc_table[(u32)slot];

    if (proc->used == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (proc->path[0] != '/') {
        clks_exec_proc_mark_exited(proc, clks_interrupts_timer_ticks(), (u64)-1);
        return CLKS_FALSE;
    }

    if (clks_exec_pid_stack_depth >= CLKS_EXEC_MAX_DEPTH) {
        clks_log(CLKS_LOG_WARN, "EXEC", "PROCESS STACK DEPTH EXCEEDED");
        clks_exec_proc_mark_exited(proc, clks_interrupts_timer_ticks(), (u64)-1);
        return CLKS_FALSE;
    }

    clks_exec_switch_address_space(clks_exec_proc_address_space(proc), &prev_cr3, &switched_cr3);

    clks_exec_proc_mark_running(proc, clks_interrupts_timer_ticks());

    depth_index = (i32)clks_exec_pid_stack_depth;
    clks_exec_pid_stack[(u32)depth_index] = proc->pid;
    clks_exec_exit_requested_stack[(u32)depth_index] = CLKS_FALSE;
    clks_exec_exit_status_stack[(u32)depth_index] = 0ULL;
    clks_exec_stop_requested_stack[(u32)depth_index] = CLKS_FALSE;
    clks_exec_image_begin_stack[(u32)depth_index] = 0ULL;
    clks_exec_image_end_stack[(u32)depth_index] = 0ULL;
    clks_exec_image_vaddr_begin_stack[(u32)depth_index] = 0ULL;
    clks_exec_image_vaddr_end_stack[(u32)depth_index] = 0ULL;
    clks_exec_stack_begin_stack[(u32)depth_index] = 0ULL;
    clks_exec_stack_end_stack[(u32)depth_index] = 0ULL;
    clks_exec_pid_stack_depth++;
    depth_pushed = CLKS_TRUE;

    if (proc->loaded_active == CLKS_TRUE && proc->saved_frame_valid == CLKS_TRUE) {
        resume_existing = CLKS_TRUE;
    } else {
        clks_exec_proc_release_runtime(proc);
        image = clks_fs_read_all(proc->path, &image_size);

        if (image == CLKS_NULL || image_size == 0ULL) {
            clks_log(CLKS_LOG_WARN, "EXEC", "EXEC FILE MISSING");
            clks_log(CLKS_LOG_WARN, "EXEC", proc->path);
            goto fail;
        }

        if (clks_elf64_inspect(image, image_size, &info) == CLKS_FALSE) {
            clks_log(CLKS_LOG_WARN, "EXEC", "EXEC ELF INVALID");
            clks_log(CLKS_LOG_WARN, "EXEC", proc->path);
            goto fail;
        }

        proc->image_mem_bytes = info.total_load_memsz;

        if (clks_elf64_load(image, image_size, &proc->loaded) == CLKS_FALSE) {
            clks_log(CLKS_LOG_WARN, "EXEC", "EXEC ELF LOAD FAILED");
            clks_log(CLKS_LOG_WARN, "EXEC", proc->path);
            goto fail;
        }

        proc->loaded_active = CLKS_TRUE;
        entry_ptr = clks_elf64_entry_pointer(&proc->loaded, info.entry);
        if (entry_ptr == CLKS_NULL) {
            clks_log(CLKS_LOG_WARN, "EXEC", "ENTRY POINTER RESOLVE FAILED");
            clks_log(CLKS_LOG_WARN, "EXEC", proc->path);
            goto fail;
        }
    }

    {
        u64 image_begin = (u64)proc->loaded.image_base;
        u64 image_end = image_begin + proc->loaded.image_size;

        if (proc->loaded.image_base == CLKS_NULL || proc->loaded.image_size == 0ULL || image_end <= image_begin) {
            clks_log(CLKS_LOG_WARN, "EXEC", "EXEC IMAGE WINDOW INVALID");
            clks_log(CLKS_LOG_WARN, "EXEC", proc->path);
            goto fail;
        }

        clks_exec_image_begin_stack[(u32)depth_index] = image_begin;
        clks_exec_image_end_stack[(u32)depth_index] = image_end;
        clks_exec_image_vaddr_begin_stack[(u32)depth_index] = proc->loaded.image_vaddr_base;
        clks_exec_image_vaddr_end_stack[(u32)depth_index] = proc->loaded.image_vaddr_base + proc->loaded.image_size;
    }

    if (resume_existing == CLKS_TRUE) {
        clks_exec_log_context_switch_serial("EXEC RUN RESUME");
        clks_exec_log_context_switch_serial(proc->path);
        clks_exec_log_context_switch_hex_serial("IMAGE_BASE", (u64)proc->loaded.image_base);
        clks_exec_log_context_switch_hex_serial("IMAGE_SIZE", proc->loaded.image_size);
        clks_exec_log_context_switch_hex_serial("VADDR_BASE", proc->loaded.image_vaddr_base);
    } else {
        clks_exec_log_info_serial("EXEC RUN START");
        clks_exec_log_info_serial(proc->path);
        clks_exec_log_hex_serial("ENTRY", info.entry);
        clks_exec_log_hex_serial("PHNUM", (u64)info.phnum);
        clks_exec_log_hex_serial("IMAGE_BASE", (u64)proc->loaded.image_base);
        clks_exec_log_hex_serial("IMAGE_SIZE", proc->loaded.image_size);
        clks_exec_log_hex_serial("VADDR_BASE", proc->loaded.image_vaddr_base);
    }

    clks_exec_running_depth++;
    depth_counted = CLKS_TRUE;

    if (clks_exec_invoke_entry(proc, entry_ptr, (u32)depth_index, &run_ret) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "EXEC", "EXEC RUN INVOKE FAILED");
        clks_log(CLKS_LOG_WARN, "EXEC", proc->path);
        goto fail;
    }

    if (depth_counted == CLKS_TRUE && clks_exec_running_depth > 0U) {
        clks_exec_running_depth--;
        depth_counted = CLKS_FALSE;
    }

    if (clks_exec_exit_requested_stack[(u32)depth_index] == CLKS_TRUE) {
        run_ret = clks_exec_exit_status_stack[(u32)depth_index];
    }

    if (run_ret == CLKS_EXEC_INTERNAL_SUSPEND_STATUS && proc->saved_frame_valid == CLKS_TRUE) {
        clks_exec_proc_pause_runtime(proc, clks_interrupts_timer_ticks());
        if (proc->blocked_reason == CLKS_EXEC_BLOCK_REASON_NONE) {
            clks_exec_proc_set_state(proc, CLKS_EXEC_PROC_PENDING, CLKS_EXEC_THREAD_READY,
                                     CLKS_EXEC_BLOCK_REASON_NONE);
        } else {
            clks_exec_proc_set_state(proc, CLKS_EXEC_PROC_PENDING, proc->thread_state, proc->blocked_reason);
        }
        proc->exit_status = (u64)-1;
        proc->exited_tick = 0ULL;
        clks_exec_log_context_switch_serial("RUN SUSPENDED");
        clks_exec_log_context_switch_serial(proc->path);

        if (depth_pushed == CLKS_TRUE && clks_exec_pid_stack_depth > 0U) {
            clks_exec_stop_requested_stack[(u32)depth_index] = CLKS_FALSE;
            clks_exec_image_begin_stack[(u32)depth_index] = 0ULL;
            clks_exec_image_end_stack[(u32)depth_index] = 0ULL;
            clks_exec_image_vaddr_begin_stack[(u32)depth_index] = 0ULL;
            clks_exec_image_vaddr_end_stack[(u32)depth_index] = 0ULL;
            clks_exec_stack_begin_stack[(u32)depth_index] = 0ULL;
            clks_exec_stack_end_stack[(u32)depth_index] = 0ULL;
            clks_exec_pid_stack_depth--;
            depth_pushed = CLKS_FALSE;
        }

        if (out_status != CLKS_NULL) {
            *out_status = run_ret;
        }

        clks_exec_restore_address_space(prev_cr3, switched_cr3);
        return CLKS_TRUE;
    }

    clks_exec_log_info_serial("RUN RETURNED");
    clks_exec_log_info_serial(proc->path);
    clks_exec_log_hex_serial("RET", run_ret);
    if (clks_exec_status_is_user_terminated(run_ret) == CLKS_TRUE) {
        clks_exec_log_info_serial("TERMINATED BY USER");
        clks_exec_log_info_serial(proc->path);
        clks_exec_log_hex_serial("PID", proc->pid);
        clks_exec_log_hex_serial("SIGNAL", clks_exec_status_signal(run_ret));
    }

    clks_exec_success++;
    if (clks_exec_stop_requested_stack[(u32)depth_index] == CLKS_TRUE) {
        u64 now_tick = clks_interrupts_timer_ticks();
        clks_exec_proc_pause_runtime(proc, now_tick);
        clks_exec_proc_release_runtime(proc);
        clks_exec_proc_release_address_space(proc);
        clks_exec_proc_set_state(proc, CLKS_EXEC_PROC_STOPPED, CLKS_EXEC_THREAD_STOPPED,
                                 CLKS_EXEC_BLOCK_REASON_NONE);
        proc->exit_status = run_ret;
        proc->exited_tick = 0ULL;
        clks_exec_log_info_serial("RUN STOPPED");
        clks_exec_log_info_serial(proc->path);
    } else {
        clks_exec_proc_mark_exited(proc, clks_interrupts_timer_ticks(), run_ret);
    }

    clks_exec_dynlib_release_owner(proc->pid);

    if (depth_pushed == CLKS_TRUE && clks_exec_pid_stack_depth > 0U) {
        clks_exec_stop_requested_stack[(u32)depth_index] = CLKS_FALSE;
        clks_exec_image_begin_stack[(u32)depth_index] = 0ULL;
        clks_exec_image_end_stack[(u32)depth_index] = 0ULL;
        clks_exec_image_vaddr_begin_stack[(u32)depth_index] = 0ULL;
        clks_exec_image_vaddr_end_stack[(u32)depth_index] = 0ULL;
        clks_exec_stack_begin_stack[(u32)depth_index] = 0ULL;
        clks_exec_stack_end_stack[(u32)depth_index] = 0ULL;
        clks_exec_pid_stack_depth--;
        depth_pushed = CLKS_FALSE;
    }

    if (out_status != CLKS_NULL) {
        *out_status = run_ret;
    }

    clks_exec_restore_address_space(prev_cr3, switched_cr3);
    return CLKS_TRUE;

fail:
    if (depth_counted == CLKS_TRUE && clks_exec_running_depth > 0U) {
        clks_exec_running_depth--;
    }

    clks_exec_proc_mark_exited(proc, clks_interrupts_timer_ticks(), (u64)-1);
    clks_exec_dynlib_release_owner(proc->pid);

    if (depth_pushed == CLKS_TRUE && clks_exec_pid_stack_depth > 0U) {
        clks_exec_stop_requested_stack[(u32)depth_index] = CLKS_FALSE;
        clks_exec_image_begin_stack[(u32)depth_index] = 0ULL;
        clks_exec_image_end_stack[(u32)depth_index] = 0ULL;
        clks_exec_image_vaddr_begin_stack[(u32)depth_index] = 0ULL;
        clks_exec_image_vaddr_end_stack[(u32)depth_index] = 0ULL;
        clks_exec_stack_begin_stack[(u32)depth_index] = 0ULL;
        clks_exec_stack_end_stack[(u32)depth_index] = 0ULL;
        clks_exec_pid_stack_depth--;
    }

    if (out_status != CLKS_NULL) {
        *out_status = (u64)-1;
    }

    clks_exec_restore_address_space(prev_cr3, switched_cr3);
    return CLKS_FALSE;
}

static clks_bool clks_exec_dispatch_pending_once(void) {
    u32 attempt;

    if (clks_exec_pending_dispatch_active == CLKS_TRUE) {
        return CLKS_FALSE;
    }

    /* One pending process per tick keeps latency sane and avoids scheduler chaos. */
    for (attempt = 0U; attempt < CLKS_EXEC_MAX_PROCS; attempt++) {
        u32 i = (clks_exec_dispatch_cursor + attempt) % CLKS_EXEC_MAX_PROCS;
        if (clks_exec_proc_table[i].used == CLKS_TRUE && clks_exec_proc_table[i].state == CLKS_EXEC_PROC_PENDING) {
            u64 ignored_status = (u64)-1;
            enum clks_exec_block_reason blocked_reason = clks_exec_proc_table[i].blocked_reason;

            if (blocked_reason == CLKS_EXEC_BLOCK_REASON_TTY_INPUT &&
                clks_keyboard_buffered_count_for_tty(clks_exec_proc_table[i].tty_index) == 0ULL) {
                continue;
            }

            if (blocked_reason == CLKS_EXEC_BLOCK_REASON_SLEEP &&
                (clks_exec_proc_table[i].wake_tick == 0ULL ||
                 clks_interrupts_timer_ticks() < clks_exec_proc_table[i].wake_tick)) {
                continue;
            }

            if (blocked_reason == CLKS_EXEC_BLOCK_REASON_PTY_INPUT ||
                blocked_reason == CLKS_EXEC_BLOCK_REASON_WAIT_CHILD ||
                blocked_reason == CLKS_EXEC_BLOCK_REASON_IO) {
                continue;
            }

            clks_exec_dispatch_cursor = (i + 1U) % CLKS_EXEC_MAX_PROCS;
            if (blocked_reason == CLKS_EXEC_BLOCK_REASON_YIELD ||
                blocked_reason == CLKS_EXEC_BLOCK_REASON_TTY_INPUT ||
                blocked_reason == CLKS_EXEC_BLOCK_REASON_SLEEP) {
                clks_exec_proc_clear_block(&clks_exec_proc_table[i]);
            }
            clks_exec_pending_dispatch_active = CLKS_TRUE;
            (void)clks_exec_run_proc_slot((i32)i, &ignored_status);
            clks_exec_pending_dispatch_active = CLKS_FALSE;
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static void clks_exec_sync_wait_idle_once(void) {
#if defined(CLKS_ARCH_X86_64)
    __asm__ volatile("sti; hlt" : : : "memory");
#elif defined(CLKS_ARCH_AARCH64)
    clks_cpu_pause();
#endif
}

static clks_bool clks_exec_proc_has_internal_suspend(i32 slot, u64 status) {
    const struct clks_exec_proc_record *proc;

    if (status != CLKS_EXEC_INTERNAL_SUSPEND_STATUS || slot < 0 || (u32)slot >= CLKS_EXEC_MAX_PROCS) {
        return CLKS_FALSE;
    }

    proc = &clks_exec_proc_table[(u32)slot];
    return (proc->used == CLKS_TRUE && proc->state == CLKS_EXEC_PROC_PENDING &&
            proc->saved_frame_valid == CLKS_TRUE)
               ? CLKS_TRUE
               : CLKS_FALSE;
}

static clks_bool clks_exec_wait_sync_proc_slot(i32 slot, u64 *io_status) {
    u64 status;

    if (io_status == CLKS_NULL) {
        return CLKS_FALSE;
    }

    status = *io_status;
    while (clks_exec_proc_has_internal_suspend(slot, status) == CLKS_TRUE) {
        /*
         * Synchronous exec is used by the user shell for foreground commands.
         * If the child waits for TTY input, cooperative suspend must remain an
         * internal scheduler detail; otherwise the shell receives CLKSSUSP as
         * the command exit code and returns to the prompt too early.
         */
        clks_exec_sync_wait_idle_once();
        status = (u64)-1;
        if (clks_exec_run_proc_slot(slot, &status) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    *io_status = status;
    return CLKS_TRUE;
}

static clks_bool clks_exec_run_path_internal(const char *path, const char *argv_line, const char *env_line,
                                             u64 stdin_fd, u64 stdout_fd, u64 stderr_fd, u64 *out_status,
                                             u64 *out_pid) {
    i32 slot;
    u64 pid;
    const struct clks_exec_proc_record *parent_proc = clks_exec_current_proc();
    struct clks_exec_proc_record *proc;
    u64 status = (u64)-1;

    if (out_status != CLKS_NULL) {
        *out_status = (u64)-1;
    }

    if (out_pid != CLKS_NULL) {
        *out_pid = (u64)-1;
    }

    /* Count every request, even bad ones, so metrics don't lie to our face. */
    clks_exec_requests++;

    if (path == CLKS_NULL || path[0] != '/') {
        clks_log(CLKS_LOG_WARN, "EXEC", "INVALID EXEC PATH");
        return CLKS_FALSE;
    }

    slot = clks_exec_proc_alloc_slot();

    if (slot < 0) {
        clks_log(CLKS_LOG_WARN, "EXEC", "PROCESS TABLE FULL");
        return CLKS_FALSE;
    }

    pid = clks_exec_alloc_pid();
    proc = clks_exec_prepare_proc_record(slot, pid, path, argv_line, env_line, CLKS_EXEC_PROC_RUNNING);

    if (proc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_exec_fd_apply_stdio_overrides(proc, parent_proc, stdin_fd, stdout_fd, stderr_fd) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "EXEC", "INVALID STDIO FD OVERRIDE");
        clks_exec_fd_release_all(proc);
        clks_memset(proc, 0, sizeof(*proc));
        return CLKS_FALSE;
    }

    if (out_pid != CLKS_NULL) {
        *out_pid = pid;
    }

    if (clks_exec_run_proc_slot(slot, &status) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_exec_wait_sync_proc_slot(slot, &status) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (out_status != CLKS_NULL) {
        *out_status = status;
    }

    return CLKS_TRUE;
}

void clks_exec_init(void) {
    clks_exec_requests = 0ULL;
    clks_exec_success = 0ULL;
    clks_exec_running_depth = 0U;
    clks_exec_pending_dispatch_active = CLKS_FALSE;
    clks_exec_dispatch_cursor = 0U;
    clks_exec_random_state = 0xA5A55A5A12345678ULL;
    clks_exec_next_pid = 1ULL;
    clks_exec_pid_stack_depth = 0U;
    clks_memset(clks_exec_pid_stack, 0, sizeof(clks_exec_pid_stack));
    clks_memset(clks_exec_exit_requested_stack, 0, sizeof(clks_exec_exit_requested_stack));
    clks_memset(clks_exec_exit_status_stack, 0, sizeof(clks_exec_exit_status_stack));
    clks_memset(clks_exec_stop_requested_stack, 0, sizeof(clks_exec_stop_requested_stack));
    clks_memset(clks_exec_unwind_slot_stack, 0, sizeof(clks_exec_unwind_slot_stack));
    clks_memset(clks_exec_unwind_slot_valid_stack, 0, sizeof(clks_exec_unwind_slot_valid_stack));
    clks_memset(clks_exec_image_begin_stack, 0, sizeof(clks_exec_image_begin_stack));
    clks_memset(clks_exec_image_end_stack, 0, sizeof(clks_exec_image_end_stack));
    clks_memset(clks_exec_image_vaddr_begin_stack, 0, sizeof(clks_exec_image_vaddr_begin_stack));
    clks_memset(clks_exec_image_vaddr_end_stack, 0, sizeof(clks_exec_image_vaddr_end_stack));
    clks_memset(clks_exec_stack_begin_stack, 0, sizeof(clks_exec_stack_begin_stack));
    clks_memset(clks_exec_stack_end_stack, 0, sizeof(clks_exec_stack_end_stack));
    clks_memset(clks_exec_proc_table, 0, sizeof(clks_exec_proc_table));
    clks_memset(clks_exec_dynlib_table, 0, sizeof(clks_exec_dynlib_table));
    clks_exec_next_dynlib_handle = 1ULL;
    clks_pty_init();
    clks_exec_log_info_serial("PATH EXEC FRAMEWORK ONLINE");
}

clks_bool clks_exec_run_path(const char *path, u64 *out_status) {
    return clks_exec_run_path_internal(path, CLKS_NULL, CLKS_NULL, CLKS_EXEC_FD_INHERIT, CLKS_EXEC_FD_INHERIT,
                                       CLKS_EXEC_FD_INHERIT, out_status, CLKS_NULL);
}

clks_bool clks_exec_run_pathv(const char *path, const char *argv_line, const char *env_line, u64 *out_status) {
    return clks_exec_run_path_internal(path, argv_line, env_line, CLKS_EXEC_FD_INHERIT, CLKS_EXEC_FD_INHERIT,
                                       CLKS_EXEC_FD_INHERIT, out_status, CLKS_NULL);
}

clks_bool clks_exec_run_pathv_io(const char *path, const char *argv_line, const char *env_line, u64 stdin_fd,
                                 u64 stdout_fd, u64 stderr_fd, u64 *out_status) {
    return clks_exec_run_path_internal(path, argv_line, env_line, stdin_fd, stdout_fd, stderr_fd, out_status,
                                       CLKS_NULL);
}

clks_bool clks_exec_spawn_pathv(const char *path, const char *argv_line, const char *env_line, u64 *out_pid) {
    i32 slot;
    u64 pid;
    struct clks_exec_proc_record *proc;

    if (out_pid != CLKS_NULL) {
        *out_pid = (u64)-1;
    }

    clks_exec_requests++;

    if (path == CLKS_NULL || path[0] != '/') {
        clks_log(CLKS_LOG_WARN, "EXEC", "INVALID SPAWN PATH");
        return CLKS_FALSE;
    }

    slot = clks_exec_proc_alloc_slot();

    if (slot < 0) {
        clks_log(CLKS_LOG_WARN, "EXEC", "PROCESS TABLE FULL");
        return CLKS_FALSE;
    }

    pid = clks_exec_alloc_pid();
    proc = clks_exec_prepare_proc_record(slot, pid, path, argv_line, env_line, CLKS_EXEC_PROC_PENDING);

    if (proc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (out_pid != CLKS_NULL) {
        *out_pid = pid;
    }

    clks_exec_log_info_serial("SPAWN QUEUED");
    clks_exec_log_info_serial(path);
    clks_exec_log_hex_serial("PID", pid);

    return CLKS_TRUE;
}

clks_bool clks_exec_spawn_path(const char *path, u64 *out_pid) {
    return clks_exec_spawn_pathv(path, CLKS_NULL, CLKS_NULL, out_pid);
}

u64 clks_exec_wait_pid(u64 pid, u64 *out_status) {
    i32 slot;
    struct clks_exec_proc_record *proc;

    slot = clks_exec_proc_find_slot_by_pid(pid);

    if (slot < 0) {
        return (u64)-1;
    }

    proc = &clks_exec_proc_table[(u32)slot];

    if (proc->used == CLKS_FALSE) {
        return (u64)-1;
    }

    if (proc->state == CLKS_EXEC_PROC_PENDING && clks_exec_pending_dispatch_active == CLKS_FALSE) {
        u64 ignored_status = (u64)-1;
        struct clks_exec_proc_record *parent = clks_exec_current_proc();

        if (parent != CLKS_NULL) {
            parent->parent_waiting = CLKS_TRUE;
            clks_exec_proc_set_block(parent, CLKS_EXEC_BLOCK_REASON_WAIT_CHILD, 0ULL, pid);
        }
        clks_exec_pending_dispatch_active = CLKS_TRUE;
        (void)clks_exec_run_proc_slot(slot, &ignored_status);
        clks_exec_pending_dispatch_active = CLKS_FALSE;
        if (parent != CLKS_NULL) {
            parent->parent_waiting = CLKS_FALSE;
            clks_exec_proc_clear_block(parent);
        }
    }

    if (proc->state == CLKS_EXEC_PROC_PENDING || proc->state == CLKS_EXEC_PROC_RUNNING ||
        proc->state == CLKS_EXEC_PROC_STOPPED) {
        return 0ULL;
    }

    if (proc->state != CLKS_EXEC_PROC_EXITED) {
        return (u64)-1;
    }

    if (out_status != CLKS_NULL) {
        *out_status = proc->exit_status;
    }

    return 1ULL;
}

clks_bool clks_exec_request_exit(u64 status) {
    i32 depth_index = clks_exec_current_depth_index();

    if (depth_index < 0) {
        return CLKS_FALSE;
    }

    clks_exec_exit_requested_stack[(u32)depth_index] = CLKS_TRUE;
    clks_exec_exit_status_stack[(u32)depth_index] = status;
    return CLKS_TRUE;
}

