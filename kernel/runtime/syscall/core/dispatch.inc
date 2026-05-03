static void clks_syscall_trace_user_program(u64 id) {
    clks_bool user_program_running =
        (clks_exec_is_running() == CLKS_TRUE && clks_exec_current_path_is_user() == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;

    if (CLKS_CFG_SYSCALL_USERID_SERIAL_LOG == 0) {
        (void)id;
        clks_syscall_user_trace_active = CLKS_FALSE;
        clks_syscall_user_trace_budget = 0ULL;
        return;
    }

    if (user_program_running == CLKS_FALSE) {
        if (clks_syscall_user_trace_active == CLKS_TRUE) {
            clks_serial_write("[DEBUG][SYSCALL] USER_TRACE_END\n");
        }

        clks_syscall_user_trace_active = CLKS_FALSE;
        clks_syscall_user_trace_budget = 0ULL;
        return;
    }

    if (clks_syscall_user_trace_active == CLKS_FALSE) {
        clks_syscall_user_trace_active = CLKS_TRUE;
        clks_syscall_user_trace_budget = CLKS_SYSCALL_USER_TRACE_BUDGET;
        clks_serial_write("[DEBUG][SYSCALL] USER_TRACE_BEGIN\n");
        clks_serial_write("[DEBUG][SYSCALL] PID: 0X");
        clks_syscall_serial_write_hex64(clks_exec_current_pid());
        clks_serial_write("\n");
    }

    if (clks_syscall_user_trace_budget > 0ULL) {
        if (clks_syscall_trace_suppressed(id) == CLKS_TRUE) {
            return;
        }

        clks_serial_write("[DEBUG][SYSCALL] USER_ID: 0X");
        clks_syscall_serial_write_hex64(id);
        clks_serial_write("\n");
        clks_syscall_user_trace_budget--;

        if (clks_syscall_user_trace_budget == 0ULL) {
            clks_serial_write("[DEBUG][SYSCALL] USER_TRACE_BUDGET_EXHAUSTED\n");
        }
    }
}

void clks_syscall_init(void) {
    clks_syscall_ready = CLKS_TRUE;
    clks_syscall_user_trace_active = CLKS_FALSE;
    clks_syscall_user_trace_budget = 0ULL;
    clks_memset(&clks_syscall_last_frame, 0, sizeof(clks_syscall_last_frame));
    clks_syscall_last_frame_valid = CLKS_FALSE;
    clks_syscall_symbols_checked = CLKS_FALSE;
    clks_syscall_symbols_data = CLKS_NULL;
    clks_syscall_symbols_size = 0ULL;
#if CLKS_CFG_USC != 0
    clks_memset(clks_syscall_usc_session_allowed_used, 0, sizeof(clks_syscall_usc_session_allowed_used));
    clks_memset(clks_syscall_usc_session_allowed_path, 0, sizeof(clks_syscall_usc_session_allowed_path));
    clks_memset(clks_syscall_usc_permanent_allowed_used, 0, sizeof(clks_syscall_usc_permanent_allowed_used));
    clks_memset(clks_syscall_usc_permanent_allowed_path, 0, sizeof(clks_syscall_usc_permanent_allowed_path));
    clks_syscall_usc_permanent_loaded = CLKS_FALSE;
#endif
    clks_syscall_stats_reset();
    clks_log(CLKS_LOG_INFO, "SYSCALL", "INT80 FRAMEWORK ONLINE");
}

u64 clks_syscall_dispatch(void *frame_ptr) {
    struct clks_syscall_frame *frame = (struct clks_syscall_frame *)frame_ptr;
    u64 id;
    u64 ret = (u64)-1;
    clks_bool user_trace_enabled = CLKS_FALSE;
    u64 user_pid = 0ULL;

#define CLKS_SYSCALL_DISPATCH_RETURN(value)                                                                            \
    do {                                                                                                               \
        ret = (value);                                                                                                 \
        goto clks_syscall_dispatch_done;                                                                               \
    } while (0)

    if (clks_syscall_ready == CLKS_FALSE || frame == CLKS_NULL) {
        return (u64)-1;
    }

    clks_memcpy(&clks_syscall_last_frame, frame, sizeof(clks_syscall_last_frame));
    clks_syscall_last_frame_valid = CLKS_TRUE;

    id = frame->rax;
    clks_syscall_stats_record(id);
    clks_syscall_trace_user_program(id);
    user_trace_enabled = clks_syscall_in_user_exec_context();

    if (user_trace_enabled == CLKS_TRUE) {
        user_pid = clks_exec_current_pid();
    }

    clks_syscall_trace_user_call(user_trace_enabled, user_pid, id, frame->rbx, frame->rcx, frame->rdx);

    if (clks_syscall_usc_check(id, frame->rbx, frame->rcx, frame->rdx) == CLKS_FALSE) {
        CLKS_SYSCALL_DISPATCH_RETURN((u64)-1);
    }

    /* Giant switch, yeah. Ugly, explicit, and easy to grep at 3 AM. */
    switch (id) {
    case CLKS_SYSCALL_LOG_WRITE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_log_write(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_TIMER_TICKS:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_interrupts_timer_ticks());
    case CLKS_SYSCALL_TIMER_HZ:
        CLKS_SYSCALL_DISPATCH_RETURN((u64)clks_interrupts_timer_hz());
    case CLKS_SYSCALL_TIME_MS: {
        u64 hz = (u64)clks_interrupts_timer_hz();
        if (hz == 0ULL) {
            CLKS_SYSCALL_DISPATCH_RETURN(0ULL);
        }
        CLKS_SYSCALL_DISPATCH_RETURN((clks_interrupts_timer_ticks() * 1000ULL) / hz);
    }
    case CLKS_SYSCALL_TASK_COUNT: {
        struct clks_scheduler_stats stats = clks_scheduler_get_stats();
        CLKS_SYSCALL_DISPATCH_RETURN(stats.task_count);
    }
    case CLKS_SYSCALL_CURRENT_TASK_ID: {
        struct clks_scheduler_stats stats = clks_scheduler_get_stats();
        CLKS_SYSCALL_DISPATCH_RETURN(stats.current_task_id);
    }
    case CLKS_SYSCALL_SERVICE_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_service_count());
    case CLKS_SYSCALL_SERVICE_READY_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_service_ready_count());
    case CLKS_SYSCALL_CONTEXT_SWITCHES: {
        struct clks_scheduler_stats stats = clks_scheduler_get_stats();
        CLKS_SYSCALL_DISPATCH_RETURN(stats.context_switch_count);
    }
    case CLKS_SYSCALL_KELF_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_kelf_count());
    case CLKS_SYSCALL_KELF_RUNS:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_kelf_total_runs());
    case CLKS_SYSCALL_FS_NODE_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_fs_node_count());
    case CLKS_SYSCALL_FS_CHILD_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fs_child_count(frame->rbx));
    case CLKS_SYSCALL_FS_GET_CHILD_NAME:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fs_get_child_name(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_FS_READ:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fs_read(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_EXEC_PATH:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_exec_path(frame->rbx));
    case CLKS_SYSCALL_EXEC_PATHV:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_exec_pathv(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_EXEC_PATHV_IO:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_exec_pathv_io(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_EXEC_REQUESTS:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_exec_request_count());
    case CLKS_SYSCALL_EXEC_SUCCESS:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_exec_success_count());
    case CLKS_SYSCALL_USER_SHELL_READY:
        CLKS_SYSCALL_DISPATCH_RETURN((clks_userland_shell_ready() == CLKS_TRUE) ? 1ULL : 0ULL);
    case CLKS_SYSCALL_USER_EXEC_REQUESTED:
        CLKS_SYSCALL_DISPATCH_RETURN((clks_userland_shell_exec_requested() == CLKS_TRUE) ? 1ULL : 0ULL);
    case CLKS_SYSCALL_USER_LAUNCH_TRIES:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_userland_launch_attempts());
    case CLKS_SYSCALL_USER_LAUNCH_OK:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_userland_launch_success());
    case CLKS_SYSCALL_USER_LAUNCH_FAIL:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_userland_launch_failures());
    case CLKS_SYSCALL_TTY_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN((u64)clks_tty_count());
    case CLKS_SYSCALL_TTY_ACTIVE:
        CLKS_SYSCALL_DISPATCH_RETURN((u64)clks_tty_active());
    case CLKS_SYSCALL_TTY_SWITCH:
        clks_tty_switch((u32)frame->rbx);
        CLKS_SYSCALL_DISPATCH_RETURN((u64)clks_tty_active());
    case CLKS_SYSCALL_TTY_WRITE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_tty_write(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_TTY_WRITE_CHAR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_tty_write_char(frame->rbx));
    case CLKS_SYSCALL_KBD_GET_CHAR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_kbd_get_char());
    case CLKS_SYSCALL_FS_STAT_TYPE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fs_stat_type(frame->rbx));
    case CLKS_SYSCALL_FS_STAT_SIZE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fs_stat_size(frame->rbx));
    case CLKS_SYSCALL_FS_MKDIR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fs_mkdir(frame->rbx));
    case CLKS_SYSCALL_FS_WRITE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fs_write(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_FS_APPEND:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fs_append(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_FS_REMOVE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fs_remove(frame->rbx));
    case CLKS_SYSCALL_LOG_JOURNAL_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_log_journal_count());
    case CLKS_SYSCALL_LOG_JOURNAL_READ:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_log_journal_read(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_KBD_BUFFERED:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_keyboard_buffered_count());
    case CLKS_SYSCALL_KBD_PUSHED:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_keyboard_push_count());
    case CLKS_SYSCALL_KBD_POPPED:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_keyboard_pop_count());
    case CLKS_SYSCALL_KBD_DROPPED:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_keyboard_drop_count());
    case CLKS_SYSCALL_KBD_HOTKEY_SWITCHES:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_keyboard_hotkey_switch_count());
    case CLKS_SYSCALL_GETPID:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_getpid());
    case CLKS_SYSCALL_SPAWN_PATH:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_spawn_path(frame->rbx));
    case CLKS_SYSCALL_SPAWN_PATHV:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_spawn_pathv(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_WAITPID:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_waitpid(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_PROC_ARGC:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_argc());
    case CLKS_SYSCALL_PROC_ARGV:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_argv(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_PROC_ENVC:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_envc());
    case CLKS_SYSCALL_PROC_ENV:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_env(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_PROC_LAST_SIGNAL:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_last_signal());
    case CLKS_SYSCALL_PROC_FAULT_VECTOR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_fault_vector());
    case CLKS_SYSCALL_PROC_FAULT_ERROR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_fault_error());
    case CLKS_SYSCALL_PROC_FAULT_RIP:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_fault_rip());
    case CLKS_SYSCALL_PROC_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_count());
    case CLKS_SYSCALL_PROC_PID_AT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_pid_at(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_PROC_SNAPSHOT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_snapshot(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_PROC_KILL:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_proc_kill(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_EXIT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_exit(frame->rbx));
    case CLKS_SYSCALL_SLEEP_TICKS:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_sleep_ticks(frame, frame->rbx));
    case CLKS_SYSCALL_SLEEP_MS:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_sleep_ms(frame, frame->rbx));
    case CLKS_SYSCALL_YIELD:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_yield(frame));
    case CLKS_SYSCALL_SHUTDOWN:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_shutdown());
    case CLKS_SYSCALL_RESTART:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_restart());
    case CLKS_SYSCALL_AUDIO_AVAILABLE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_audio_available());
    case CLKS_SYSCALL_AUDIO_PLAY_TONE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_audio_play_tone(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_AUDIO_STOP:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_audio_stop());
    case CLKS_SYSCALL_KDBG_SYM:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_kdbg_sym(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_KDBG_BT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_kdbg_bt(frame->rbx));
    case CLKS_SYSCALL_KDBG_REGS:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_kdbg_regs(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_STATS_TOTAL:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_stats_total_count());
    case CLKS_SYSCALL_STATS_ID_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_stats_id(frame->rbx));
    case CLKS_SYSCALL_STATS_RECENT_WINDOW:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_stats_recent_window());
    case CLKS_SYSCALL_STATS_RECENT_ID:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_stats_recent_id(frame->rbx));
    case CLKS_SYSCALL_FD_OPEN:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fd_open(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_FD_READ:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fd_read(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_FD_WRITE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fd_write(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_FD_CLOSE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fd_close(frame->rbx));
    case CLKS_SYSCALL_FD_DUP:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fd_dup(frame->rbx));
    case CLKS_SYSCALL_DL_OPEN:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_dl_open(frame->rbx));
    case CLKS_SYSCALL_DL_CLOSE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_dl_close(frame->rbx));
    case CLKS_SYSCALL_DL_SYM:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_dl_sym(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_FB_INFO:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fb_info(frame->rbx));
    case CLKS_SYSCALL_FB_BLIT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fb_blit(frame->rbx));
    case CLKS_SYSCALL_FB_CLEAR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_fb_clear(frame->rbx));
    case CLKS_SYSCALL_KERNEL_VERSION:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_kernel_version(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_DISK_PRESENT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_present());
    case CLKS_SYSCALL_DISK_SIZE_BYTES:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_size_bytes());
    case CLKS_SYSCALL_DISK_SECTOR_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_sector_count());
    case CLKS_SYSCALL_DISK_FORMATTED:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_formatted());
    case CLKS_SYSCALL_DISK_FORMAT_FAT32:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_format_fat32(frame->rbx));
    case CLKS_SYSCALL_DISK_MOUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_mount(frame->rbx));
    case CLKS_SYSCALL_DISK_MOUNTED:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_mounted());
    case CLKS_SYSCALL_DISK_MOUNT_PATH:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_mount_path(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_DISK_READ_SECTOR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_read_sector(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_DISK_WRITE_SECTOR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_disk_write_sector(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_NET_AVAILABLE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_available());
    case CLKS_SYSCALL_NET_IPV4_ADDR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_ipv4_addr());
    case CLKS_SYSCALL_NET_PING:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_ping(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_NET_UDP_SEND:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_udp_send(frame->rbx));
    case CLKS_SYSCALL_NET_UDP_RECV:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_udp_recv(frame->rbx));
    case CLKS_SYSCALL_NET_NETMASK:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_netmask());
    case CLKS_SYSCALL_NET_GATEWAY:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_gateway());
    case CLKS_SYSCALL_NET_DNS_SERVER:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_dns_server());
    case CLKS_SYSCALL_NET_TCP_CONNECT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_tcp_connect(frame->rbx));
    case CLKS_SYSCALL_NET_TCP_SEND:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_tcp_send(frame->rbx));
    case CLKS_SYSCALL_NET_TCP_RECV:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_tcp_recv(frame->rbx));
    case CLKS_SYSCALL_NET_TCP_CLOSE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_tcp_close(frame->rbx));
    case CLKS_SYSCALL_NET_TCP_LAST_ERROR:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_net_tcp_last_error());
    case CLKS_SYSCALL_VM_ALLOC:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_exec_vm_alloc(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_VM_FREE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_exec_vm_free(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_MOUSE_STATE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_mouse_state(frame->rbx));
    case CLKS_SYSCALL_WM_CREATE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_create(frame->rbx));
    case CLKS_SYSCALL_WM_DESTROY:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_destroy(frame->rbx));
    case CLKS_SYSCALL_WM_PRESENT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_present(frame->rbx));
    case CLKS_SYSCALL_WM_POLL_EVENT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_poll_event(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_WM_MOVE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_move(frame->rbx));
    case CLKS_SYSCALL_WM_SET_FOCUS:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_set_focus(frame->rbx));
    case CLKS_SYSCALL_WM_SET_FLAGS:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_set_flags(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_WM_RESIZE:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_resize(frame->rbx));
    case CLKS_SYSCALL_PTY_OPEN:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_pty_open());
    case CLKS_SYSCALL_WM_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_count());
    case CLKS_SYSCALL_WM_ID_AT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_id_at(frame->rbx, frame->rcx));
    case CLKS_SYSCALL_WM_SNAPSHOT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_wm_snapshot(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_USER_HEAP_ALLOC:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_exec_user_heap_alloc(frame->rbx));
    case CLKS_SYSCALL_DRIVER_COUNT:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_driver_count());
    case CLKS_SYSCALL_DRIVER_INFO:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_driver_info(frame->rbx, frame->rcx, frame->rdx));
    case CLKS_SYSCALL_DRIVER_LOAD:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_driver_load(frame->rbx));
    case CLKS_SYSCALL_DRIVER_UNLOAD:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_driver_unload(frame->rbx));
    case CLKS_SYSCALL_DRIVER_RELOAD:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_driver_reload());
    default:
        CLKS_SYSCALL_DISPATCH_RETURN((u64)-1);
    }

clks_syscall_dispatch_done:
    clks_syscall_trace_user_return(user_trace_enabled, user_pid, id, ret);
#undef CLKS_SYSCALL_DISPATCH_RETURN
    return ret;
}

u64 clks_syscall_invoke_kernel(u64 id, u64 arg0, u64 arg1, u64 arg2) {
    u64 ret;

    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(id), "b"(arg0), "c"(arg1), "d"(arg2) : "memory");

    return ret;
}
