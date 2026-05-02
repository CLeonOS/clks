static u64 clks_syscall_exec_path(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];
    u64 status = (u64)-1;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_exec_run_path(path, &status) == CLKS_FALSE) {
        return (u64)-1;
    }

    return status;
}

static u64 clks_syscall_exec_pathv(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];
    char argv_line[CLKS_SYSCALL_ARG_LINE_MAX];
    char env_line[CLKS_SYSCALL_ENV_LINE_MAX];
    u64 status = (u64)-1;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_syscall_copy_user_optional_string(arg1, argv_line, sizeof(argv_line)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_syscall_copy_user_optional_string(arg2, env_line, sizeof(env_line)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_exec_run_pathv(path, argv_line, env_line, &status) == CLKS_FALSE) {
        return (u64)-1;
    }

    return status;
}

static u64 clks_syscall_exec_pathv_io(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];
    char argv_line[CLKS_SYSCALL_ARG_LINE_MAX];
    char env_line[CLKS_SYSCALL_ENV_LINE_MAX];
    struct clks_syscall_exec_io_req req;
    u64 status = (u64)-1;

    if (arg2 == 0ULL) {
        return (u64)-1;
    }

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_syscall_copy_user_optional_string(arg1, argv_line, sizeof(argv_line)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_syscall_user_ptr_readable(arg2, (u64)sizeof(req)) == CLKS_FALSE) {
        return (u64)-1;
    }

    clks_memcpy(&req, (const void *)arg2, sizeof(req));

    if (clks_syscall_copy_user_optional_string(req.env_line_ptr, env_line, sizeof(env_line)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_exec_run_pathv_io(path, argv_line, env_line, req.stdin_fd, req.stdout_fd, req.stderr_fd, &status) ==
        CLKS_FALSE) {
        return (u64)-1;
    }

    return status;
}

static u64 clks_syscall_getpid(void) {
    return clks_exec_current_pid();
}

static u64 clks_syscall_spawn_path(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];
    u64 pid = (u64)-1;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_exec_spawn_path(path, &pid) == CLKS_FALSE) {
        return (u64)-1;
    }

    return pid;
}

static u64 clks_syscall_spawn_pathv(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];
    char argv_line[CLKS_SYSCALL_ARG_LINE_MAX];
    char env_line[CLKS_SYSCALL_ENV_LINE_MAX];
    u64 pid = (u64)-1;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_syscall_copy_user_optional_string(arg1, argv_line, sizeof(argv_line)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_syscall_copy_user_optional_string(arg2, env_line, sizeof(env_line)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_exec_spawn_pathv(path, argv_line, env_line, &pid) == CLKS_FALSE) {
        return (u64)-1;
    }

    return pid;
}

static u64 clks_syscall_waitpid(u64 arg0, u64 arg1) {
    u64 status = (u64)-1;
    u64 wait_ret = clks_exec_wait_pid(arg0, &status);

    if (wait_ret == 1ULL && arg1 != 0ULL) {
        if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(status)) == CLKS_FALSE) {
            return (u64)-1;
        }
        clks_memcpy((void *)arg1, &status, sizeof(status));
    }

    return wait_ret;
}

static u64 clks_syscall_proc_argc(void) {
    return clks_exec_current_argc();
}

static u64 clks_syscall_proc_argv(u64 arg0, u64 arg1, u64 arg2) {
    if (arg1 == 0ULL || arg2 == 0ULL) {
        return 0ULL;
    }

    if (arg2 > CLKS_SYSCALL_ITEM_MAX) {
        arg2 = CLKS_SYSCALL_ITEM_MAX;
    }

    if (clks_syscall_user_ptr_writable(arg1, arg2) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_exec_copy_current_argv(arg0, (char *)arg1, (usize)arg2) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_proc_envc(void) {
    return clks_exec_current_envc();
}

static u64 clks_syscall_proc_env(u64 arg0, u64 arg1, u64 arg2) {
    if (arg1 == 0ULL || arg2 == 0ULL) {
        return 0ULL;
    }

    if (arg2 > CLKS_SYSCALL_ITEM_MAX) {
        arg2 = CLKS_SYSCALL_ITEM_MAX;
    }

    if (clks_syscall_user_ptr_writable(arg1, arg2) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_exec_copy_current_env(arg0, (char *)arg1, (usize)arg2) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_proc_last_signal(void) {
    return clks_exec_current_signal();
}

static u64 clks_syscall_proc_fault_vector(void) {
    return clks_exec_current_fault_vector();
}

static u64 clks_syscall_proc_fault_error(void) {
    return clks_exec_current_fault_error();
}

static u64 clks_syscall_proc_fault_rip(void) {
    return clks_exec_current_fault_rip();
}

static u64 clks_syscall_proc_count(void) {
    return clks_exec_proc_count();
}

static u64 clks_syscall_proc_pid_at(u64 arg0, u64 arg1) {
    u64 pid = 0ULL;

    if (arg1 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(pid)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_exec_proc_pid_at(arg0, &pid) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy((void *)arg1, &pid, sizeof(pid));
    return 1ULL;
}

static u64 clks_syscall_proc_snapshot(u64 arg0, u64 arg1, u64 arg2) {
    struct clks_exec_proc_snapshot snap;

    if (arg1 == 0ULL || arg2 < (u64)sizeof(snap)) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(snap)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_exec_proc_snapshot(arg0, &snap) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy((void *)arg1, &snap, sizeof(snap));
    return 1ULL;
}

static u64 clks_syscall_proc_kill(u64 arg0, u64 arg1) {
    return clks_exec_proc_kill(arg0, arg1);
}

static u64 clks_syscall_exit(u64 arg0) {
    return (clks_exec_request_exit(arg0) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_sleep_ticks(struct clks_syscall_frame *frame, u64 arg0) {
    u64 before = clks_interrupts_timer_ticks();
    u64 slept;

    if (clks_wm_ready() == CLKS_TRUE) {
        clks_wm_tick(before);
    }

    if (clks_exec_suspend_current_from_syscall(frame, arg0) == CLKS_TRUE) {
        return arg0;
    }

    slept = clks_exec_sleep_ticks(arg0);

    if (clks_wm_ready() == CLKS_TRUE) {
        clks_wm_tick(clks_interrupts_timer_ticks());
    }

    return slept;
}

static u64 clks_syscall_ms_to_ticks(u64 ms) {
    u64 hz;
    u64 whole;
    u64 rem;
    u64 ticks;

    if (ms == 0ULL) {
        return 0ULL;
    }

    hz = (u64)clks_interrupts_timer_hz();
    if (hz == 0ULL) {
        hz = 100ULL;
    }

    whole = ms / 1000ULL;
    rem = ms % 1000ULL;
    if (whole > ((~0ULL) / hz)) {
        return ~0ULL;
    }

    ticks = whole * hz;
    if (rem != 0ULL) {
        u64 partial = ((rem * hz) + 999ULL) / 1000ULL;
        if (ticks > ((~0ULL) - partial)) {
            return ~0ULL;
        }
        ticks += partial;
    }

    return (ticks == 0ULL) ? 1ULL : ticks;
}

static u64 clks_syscall_ticks_to_ms(u64 ticks) {
    u64 hz = (u64)clks_interrupts_timer_hz();

    if (ticks == 0ULL) {
        return 0ULL;
    }
    if (hz == 0ULL) {
        hz = 100ULL;
    }
    return (ticks / hz) * 1000ULL + (((ticks % hz) * 1000ULL) / hz);
}

static u64 clks_syscall_sleep_ms(struct clks_syscall_frame *frame, u64 ms) {
    u64 slept_ticks = clks_syscall_sleep_ticks(frame, clks_syscall_ms_to_ticks(ms));
    return clks_syscall_ticks_to_ms(slept_ticks);
}

static u64 clks_syscall_yield(struct clks_syscall_frame *frame) {
    u64 tick;

    if (clks_wm_ready() == CLKS_TRUE) {
        clks_wm_tick(clks_interrupts_timer_ticks());
    }

    tick = clks_interrupts_timer_ticks();
    if (clks_exec_suspend_current_from_syscall(frame, tick) == CLKS_TRUE) {
        return tick;
    }

    tick = clks_exec_yield();

    if (clks_wm_ready() == CLKS_TRUE) {
        clks_wm_tick(tick);
    }

    return tick;
}

static u64 clks_syscall_shutdown(void) {
    clks_log(CLKS_LOG_WARN, "SYSCALL", "SHUTDOWN REQUESTED BY USERLAND");
    clks_serial_write("[WARN][SYSCALL] SHUTDOWN REQUESTED\n");
#if defined(CLKS_ARCH_X86_64)
    clks_syscall_outw(0x604U, 0x2000U);
#endif
    clks_cpu_halt_forever();
    return 1ULL;
}

static u64 clks_syscall_restart(void) {
    clks_log(CLKS_LOG_WARN, "SYSCALL", "RESTART REQUESTED BY USERLAND");
    clks_serial_write("[WARN][SYSCALL] RESTART REQUESTED\n");
#if defined(CLKS_ARCH_X86_64)
    clks_syscall_outb(0x64U, 0xFEU);
#endif
    clks_cpu_halt_forever();
    return 1ULL;
}

