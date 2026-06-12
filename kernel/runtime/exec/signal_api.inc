clks_bool clks_exec_try_unwind_signaled_process(u64 interrupted_rip, u64 *io_rip, u64 *io_rdi, u64 *io_rsi) {
    i32 depth_index;

    if (io_rip == CLKS_NULL || io_rdi == CLKS_NULL || io_rsi == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_exec_is_running() == CLKS_FALSE || clks_exec_current_path_is_user() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_exec_rip_is_current_user_context(interrupted_rip) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    depth_index = clks_exec_current_depth_index();

    if (depth_index < 0) {
        return CLKS_FALSE;
    }

    if (clks_exec_stop_requested_stack[(u32)depth_index] == CLKS_FALSE) {
        return CLKS_FALSE;
    }

#if defined(CLKS_ARCH_X86_64)
    if (clks_exec_unwind_slot_valid_stack[(u32)depth_index] == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *io_rip = (u64)clks_exec_abort_to_caller_x86_64;
    *io_rdi = clks_exec_unwind_slot_stack[(u32)depth_index];
    *io_rsi = clks_exec_exit_status_stack[(u32)depth_index];
    return CLKS_TRUE;
#else
    (void)depth_index;
    return CLKS_FALSE;
#endif
}

clks_bool clks_exec_handle_exception(u64 vector, u64 error_code, u64 rip, u64 *io_rip, u64 *io_rdi, u64 *io_rsi) {
    i32 depth_index;
    struct clks_exec_proc_record *proc;
    u64 signal;
    u64 status;

    if (clks_exec_is_running() == CLKS_FALSE || clks_exec_current_path_is_user() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_exec_rip_is_current_user_context(rip) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    depth_index = clks_exec_current_depth_index();
    proc = clks_exec_current_proc();

    if (depth_index < 0 || proc == CLKS_NULL) {
        return CLKS_FALSE;
    }

#if defined(CLKS_ARCH_X86_64)
    if (vector == 14ULL && (error_code & 0x10ULL) != 0ULL && io_rip != CLKS_NULL) {
        u64 translated_rip = 0ULL;

        if (clks_exec_translate_legacy_user_rip(rip, &translated_rip) == CLKS_TRUE) {
            *io_rip = translated_rip;
            clks_exec_log_info_serial("USER RIP LEGACY REBASE");
            clks_exec_log_hex_serial("RIP_OLD", rip);
            clks_exec_log_hex_serial("RIP_NEW", translated_rip);
            return CLKS_TRUE;
        }
    }
#endif

    signal = clks_exec_signal_from_vector(vector);
    status = clks_exec_encode_signal_status(signal, vector, error_code);

    proc->last_signal = signal;
    proc->last_fault_vector = vector;
    proc->last_fault_error = error_code;
    proc->last_fault_rip = rip;

    clks_exec_exit_requested_stack[(u32)depth_index] = CLKS_TRUE;
    clks_exec_exit_status_stack[(u32)depth_index] = status;

    clks_exec_log_info_serial("USER EXCEPTION CAPTURED");
    clks_exec_log_info_serial(proc->path);
    clks_exec_log_hex_serial("PID", proc->pid);
    clks_exec_log_hex_serial("SIGNAL", signal);
    clks_exec_log_hex_serial("VECTOR", vector);
    clks_exec_log_hex_serial("ERROR", error_code);
    clks_exec_log_hex_serial("RIP", rip);
    {
        u64 rip_vaddr = 0ULL;
        if (clks_exec_translate_loaded_user_rip_to_vaddr(rip, &rip_vaddr) == CLKS_TRUE) {
            clks_exec_log_hex_serial("RIP_VADDR", rip_vaddr);
        }
    }
#if defined(CLKS_ARCH_X86_64)
    if (vector == 14ULL) {
        clks_exec_log_hex_serial("CR2", clks_exec_read_cr2());
    }
#endif

#if defined(CLKS_ARCH_X86_64)
    if (io_rip == CLKS_NULL || io_rdi == CLKS_NULL || io_rsi == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_exec_unwind_slot_valid_stack[(u32)depth_index] == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *io_rip = (u64)clks_exec_abort_to_caller_x86_64;
    *io_rdi = clks_exec_unwind_slot_stack[(u32)depth_index];
    *io_rsi = status;
    return CLKS_TRUE;
#else
    (void)io_rip;
    (void)io_rdi;
    (void)io_rsi;
    return CLKS_FALSE;
#endif
}

clks_bool clks_exec_suspend_current_from_syscall(void *frame_ptr, u64 syscall_ret) {
#if defined(CLKS_ARCH_X86_64)
    struct clks_exec_saved_interrupt_frame *frame = (struct clks_exec_saved_interrupt_frame *)frame_ptr;
    struct clks_exec_proc_record *proc;
    i32 depth_index;

    if (frame == CLKS_NULL || clks_exec_pending_dispatch_active == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    depth_index = clks_exec_current_depth_index();
    proc = clks_exec_current_proc();
    if (depth_index < 0 || proc == CLKS_NULL || proc->state != CLKS_EXEC_PROC_RUNNING) {
        return CLKS_FALSE;
    }

    if (proc->loaded_active == CLKS_FALSE || proc->run_stack_base == CLKS_NULL || proc->run_stack_bytes == 0ULL ||
        clks_exec_unwind_slot_valid_stack[(u32)depth_index] == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    proc->saved_frame = *frame;
    proc->saved_frame.rax = syscall_ret;
    proc->saved_frame_valid = CLKS_TRUE;
    if (proc->blocked_reason == CLKS_EXEC_BLOCK_REASON_NONE) {
        clks_exec_proc_set_block(proc, CLKS_EXEC_BLOCK_REASON_YIELD, 0ULL, 0ULL);
    }

    frame->rip = (u64)clks_exec_abort_to_caller_x86_64;
    frame->rdi = clks_exec_unwind_slot_stack[(u32)depth_index];
    frame->rsi = CLKS_EXEC_INTERNAL_SUSPEND_STATUS;
    return CLKS_TRUE;
#else
    (void)frame_ptr;
    (void)syscall_ret;
    return CLKS_FALSE;
#endif
}

