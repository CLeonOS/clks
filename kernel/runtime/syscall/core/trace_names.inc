static void clks_syscall_serial_write_hex64(u64 value) {
    i32 nibble;

    for (nibble = 15; nibble >= 0; nibble--) {
        u64 current = (value >> (u64)(nibble * 4)) & 0x0FULL;
        char ch = (current < 10ULL) ? (char)('0' + current) : (char)('A' + (current - 10ULL));
        clks_serial_write_char(ch);
    }
}

static const char *clks_syscall_name(u64 id) {
    switch (id) {
    case CLKS_SYSCALL_LOG_WRITE:
        return "LOG_WRITE";
    case CLKS_SYSCALL_TIMER_TICKS:
        return "TIMER_TICKS";
    case CLKS_SYSCALL_TIMER_HZ:
        return "TIMER_HZ";
    case CLKS_SYSCALL_TIME_MS:
        return "TIME_MS";
    case CLKS_SYSCALL_SLEEP_MS:
        return "SLEEP_MS";
    case CLKS_SYSCALL_TASK_COUNT:
        return "TASK_COUNT";
    case CLKS_SYSCALL_CURRENT_TASK_ID:
        return "CURRENT_TASK_ID";
    case CLKS_SYSCALL_SERVICE_COUNT:
        return "SERVICE_COUNT";
    case CLKS_SYSCALL_SERVICE_READY_COUNT:
        return "SERVICE_READY_COUNT";
    case CLKS_SYSCALL_CONTEXT_SWITCHES:
        return "CONTEXT_SWITCHES";
    case CLKS_SYSCALL_KELF_COUNT:
        return "KELF_COUNT";
    case CLKS_SYSCALL_KELF_RUNS:
        return "KELF_RUNS";
    case CLKS_SYSCALL_FS_NODE_COUNT:
        return "FS_NODE_COUNT";
    case CLKS_SYSCALL_FS_CHILD_COUNT:
        return "FS_CHILD_COUNT";
    case CLKS_SYSCALL_FS_GET_CHILD_NAME:
        return "FS_GET_CHILD_NAME";
    case CLKS_SYSCALL_FS_READ:
        return "FS_READ";
    case CLKS_SYSCALL_EXEC_PATH:
        return "EXEC_PATH";
    case CLKS_SYSCALL_EXEC_REQUESTS:
        return "EXEC_REQUESTS";
    case CLKS_SYSCALL_EXEC_SUCCESS:
        return "EXEC_SUCCESS";
    case CLKS_SYSCALL_USER_SHELL_READY:
        return "USER_SHELL_READY";
    case CLKS_SYSCALL_USER_EXEC_REQUESTED:
        return "USER_EXEC_REQUESTED";
    case CLKS_SYSCALL_USER_LAUNCH_TRIES:
        return "USER_LAUNCH_TRIES";
    case CLKS_SYSCALL_USER_LAUNCH_OK:
        return "USER_LAUNCH_OK";
    case CLKS_SYSCALL_USER_LAUNCH_FAIL:
        return "USER_LAUNCH_FAIL";
    case CLKS_SYSCALL_TTY_COUNT:
        return "TTY_COUNT";
    case CLKS_SYSCALL_TTY_ACTIVE:
        return "TTY_ACTIVE";
    case CLKS_SYSCALL_TTY_SWITCH:
        return "TTY_SWITCH";
    case CLKS_SYSCALL_TTY_WRITE:
        return "TTY_WRITE";
    case CLKS_SYSCALL_TTY_WRITE_CHAR:
        return "TTY_WRITE_CHAR";
    case CLKS_SYSCALL_KBD_GET_CHAR:
        return "KBD_GET_CHAR";
    case CLKS_SYSCALL_FS_STAT_TYPE:
        return "FS_STAT_TYPE";
    case CLKS_SYSCALL_FS_STAT_SIZE:
        return "FS_STAT_SIZE";
    case CLKS_SYSCALL_FS_MKDIR:
        return "FS_MKDIR";
    case CLKS_SYSCALL_FS_WRITE:
        return "FS_WRITE";
    case CLKS_SYSCALL_FS_APPEND:
        return "FS_APPEND";
    case CLKS_SYSCALL_FS_REMOVE:
        return "FS_REMOVE";
    case CLKS_SYSCALL_LOG_JOURNAL_COUNT:
        return "LOG_JOURNAL_COUNT";
    case CLKS_SYSCALL_LOG_JOURNAL_READ:
        return "LOG_JOURNAL_READ";
    case CLKS_SYSCALL_KBD_BUFFERED:
        return "KBD_BUFFERED";
    case CLKS_SYSCALL_KBD_PUSHED:
        return "KBD_PUSHED";
    case CLKS_SYSCALL_KBD_POPPED:
        return "KBD_POPPED";
    case CLKS_SYSCALL_KBD_DROPPED:
        return "KBD_DROPPED";
    case CLKS_SYSCALL_KBD_HOTKEY_SWITCHES:
        return "KBD_HOTKEY_SWITCHES";
    case CLKS_SYSCALL_GETPID:
        return "GETPID";
    case CLKS_SYSCALL_SPAWN_PATH:
        return "SPAWN_PATH";
    case CLKS_SYSCALL_WAITPID:
        return "WAITPID";
    case CLKS_SYSCALL_EXIT:
        return "EXIT";
    case CLKS_SYSCALL_SLEEP_TICKS:
        return "SLEEP_TICKS";
    case CLKS_SYSCALL_YIELD:
        return "YIELD";
    case CLKS_SYSCALL_SHUTDOWN:
        return "SHUTDOWN";
    case CLKS_SYSCALL_RESTART:
        return "RESTART";
    case CLKS_SYSCALL_AUDIO_AVAILABLE:
        return "AUDIO_AVAILABLE";
    case CLKS_SYSCALL_AUDIO_PLAY_TONE:
        return "AUDIO_PLAY_TONE";
    case CLKS_SYSCALL_AUDIO_STOP:
        return "AUDIO_STOP";
    case CLKS_SYSCALL_EXEC_PATHV:
        return "EXEC_PATHV";
    case CLKS_SYSCALL_SPAWN_PATHV:
        return "SPAWN_PATHV";
    case CLKS_SYSCALL_PROC_ARGC:
        return "PROC_ARGC";
    case CLKS_SYSCALL_PROC_ARGV:
        return "PROC_ARGV";
    case CLKS_SYSCALL_PROC_ENVC:
        return "PROC_ENVC";
    case CLKS_SYSCALL_PROC_ENV:
        return "PROC_ENV";
    case CLKS_SYSCALL_PROC_LAST_SIGNAL:
        return "PROC_LAST_SIGNAL";
    case CLKS_SYSCALL_PROC_FAULT_VECTOR:
        return "PROC_FAULT_VECTOR";
    case CLKS_SYSCALL_PROC_FAULT_ERROR:
        return "PROC_FAULT_ERROR";
    case CLKS_SYSCALL_PROC_FAULT_RIP:
        return "PROC_FAULT_RIP";
    case CLKS_SYSCALL_PROC_COUNT:
        return "PROC_COUNT";
    case CLKS_SYSCALL_PROC_PID_AT:
        return "PROC_PID_AT";
    case CLKS_SYSCALL_PROC_SNAPSHOT:
        return "PROC_SNAPSHOT";
    case CLKS_SYSCALL_PROC_KILL:
        return "PROC_KILL";
    case CLKS_SYSCALL_KDBG_SYM:
        return "KDBG_SYM";
    case CLKS_SYSCALL_KDBG_BT:
        return "KDBG_BT";
    case CLKS_SYSCALL_KDBG_REGS:
        return "KDBG_REGS";
    case CLKS_SYSCALL_STATS_TOTAL:
        return "STATS_TOTAL";
    case CLKS_SYSCALL_STATS_ID_COUNT:
        return "STATS_ID_COUNT";
    case CLKS_SYSCALL_STATS_RECENT_WINDOW:
        return "STATS_RECENT_WINDOW";
    case CLKS_SYSCALL_STATS_RECENT_ID:
        return "STATS_RECENT_ID";
    case CLKS_SYSCALL_FD_OPEN:
        return "FD_OPEN";
    case CLKS_SYSCALL_FD_READ:
        return "FD_READ";
    case CLKS_SYSCALL_FD_WRITE:
        return "FD_WRITE";
    case CLKS_SYSCALL_FD_CLOSE:
        return "FD_CLOSE";
    case CLKS_SYSCALL_FD_DUP:
        return "FD_DUP";
    case CLKS_SYSCALL_DL_OPEN:
        return "DL_OPEN";
    case CLKS_SYSCALL_DL_CLOSE:
        return "DL_CLOSE";
    case CLKS_SYSCALL_DL_SYM:
        return "DL_SYM";
    case CLKS_SYSCALL_EXEC_PATHV_IO:
        return "EXEC_PATHV_IO";
    case CLKS_SYSCALL_FB_INFO:
        return "FB_INFO";
    case CLKS_SYSCALL_FB_BLIT:
        return "FB_BLIT";
    case CLKS_SYSCALL_FB_CLEAR:
        return "FB_CLEAR";
    case CLKS_SYSCALL_KERNEL_VERSION:
        return "KERNEL_VERSION";
    case CLKS_SYSCALL_DISK_PRESENT:
        return "DISK_PRESENT";
    case CLKS_SYSCALL_DISK_SIZE_BYTES:
        return "DISK_SIZE_BYTES";
    case CLKS_SYSCALL_DISK_SECTOR_COUNT:
        return "DISK_SECTOR_COUNT";
    case CLKS_SYSCALL_DISK_FORMATTED:
        return "DISK_FORMATTED";
    case CLKS_SYSCALL_DISK_FORMAT_FAT32:
        return "DISK_FORMAT_FAT32";
    case CLKS_SYSCALL_DISK_MOUNT:
        return "DISK_MOUNT";
    case CLKS_SYSCALL_DISK_MOUNTED:
        return "DISK_MOUNTED";
    case CLKS_SYSCALL_DISK_MOUNT_PATH:
        return "DISK_MOUNT_PATH";
    case CLKS_SYSCALL_DISK_READ_SECTOR:
        return "DISK_READ_SECTOR";
    case CLKS_SYSCALL_DISK_WRITE_SECTOR:
        return "DISK_WRITE_SECTOR";
    case CLKS_SYSCALL_NET_AVAILABLE:
        return "NET_AVAILABLE";
    case CLKS_SYSCALL_NET_IPV4_ADDR:
        return "NET_IPV4_ADDR";
    case CLKS_SYSCALL_NET_PING:
        return "NET_PING";
    case CLKS_SYSCALL_NET_UDP_SEND:
        return "NET_UDP_SEND";
    case CLKS_SYSCALL_NET_UDP_RECV:
        return "NET_UDP_RECV";
    case CLKS_SYSCALL_NET_NETMASK:
        return "NET_NETMASK";
    case CLKS_SYSCALL_NET_GATEWAY:
        return "NET_GATEWAY";
    case CLKS_SYSCALL_NET_DNS_SERVER:
        return "NET_DNS_SERVER";
    case CLKS_SYSCALL_NET_TCP_CONNECT:
        return "NET_TCP_CONNECT";
    case CLKS_SYSCALL_NET_TCP_SEND:
        return "NET_TCP_SEND";
    case CLKS_SYSCALL_NET_TCP_RECV:
        return "NET_TCP_RECV";
    case CLKS_SYSCALL_NET_TCP_CLOSE:
        return "NET_TCP_CLOSE";
    case CLKS_SYSCALL_NET_TCP_LAST_ERROR:
        return "NET_TCP_LAST_ERROR";
    case CLKS_SYSCALL_VM_ALLOC:
        return "VM_ALLOC";
    case CLKS_SYSCALL_VM_FREE:
        return "VM_FREE";
    case CLKS_SYSCALL_MOUSE_STATE:
        return "MOUSE_STATE";
    case CLKS_SYSCALL_WM_CREATE:
        return "WM_CREATE";
    case CLKS_SYSCALL_WM_DESTROY:
        return "WM_DESTROY";
    case CLKS_SYSCALL_WM_PRESENT:
        return "WM_PRESENT";
    case CLKS_SYSCALL_WM_POLL_EVENT:
        return "WM_POLL_EVENT";
    case CLKS_SYSCALL_WM_MOVE:
        return "WM_MOVE";
    case CLKS_SYSCALL_WM_SET_FOCUS:
        return "WM_SET_FOCUS";
    case CLKS_SYSCALL_WM_SET_FLAGS:
        return "WM_SET_FLAGS";
    case CLKS_SYSCALL_WM_RESIZE:
        return "WM_RESIZE";
    case CLKS_SYSCALL_PTY_OPEN:
        return "PTY_OPEN";
    case CLKS_SYSCALL_WM_COUNT:
        return "WM_COUNT";
    case CLKS_SYSCALL_WM_ID_AT:
        return "WM_ID_AT";
    case CLKS_SYSCALL_WM_SNAPSHOT:
        return "WM_SNAPSHOT";
    case CLKS_SYSCALL_USER_HEAP_ALLOC:
        return "USER_HEAP_ALLOC";
    case CLKS_SYSCALL_DRIVER_COUNT:
        return "DRIVER_COUNT";
    case CLKS_SYSCALL_DRIVER_INFO:
        return "DRIVER_INFO";
    case CLKS_SYSCALL_DRIVER_LOAD:
        return "DRIVER_LOAD";
    case CLKS_SYSCALL_DRIVER_UNLOAD:
        return "DRIVER_UNLOAD";
    case CLKS_SYSCALL_DRIVER_RELOAD:
        return "DRIVER_RELOAD";
    default:
        return "UNKNOWN";
    }
}

static void clks_syscall_trace_user_call(clks_bool trace_enabled, u64 pid, u64 id, u64 arg0, u64 arg1, u64 arg2) {
    const char *name;

    if (CLKS_CFG_SYSCALL_SERIAL_LOG == 0) {
        return;
    }

    if (trace_enabled == CLKS_FALSE) {
        return;
    }

    /* FD_READ is often polled in tight loops; log it on return path only. */
    if (id == CLKS_SYSCALL_FD_READ) {
        return;
    }

    name = clks_syscall_name(id);
    clks_serial_write("[INFO][SYSCALL] CALL PID: 0X");
    clks_syscall_serial_write_hex64(pid);
    clks_serial_write(" ID: 0X");
    clks_syscall_serial_write_hex64(id);
    clks_serial_write(" NAME: ");
    clks_serial_write(name);
    clks_serial_write(" ARG0: 0X");
    clks_syscall_serial_write_hex64(arg0);
    clks_serial_write(" ARG1: 0X");
    clks_syscall_serial_write_hex64(arg1);
    clks_serial_write(" ARG2: 0X");
    clks_syscall_serial_write_hex64(arg2);
    clks_serial_write("\n");
}

static void clks_syscall_trace_user_return(clks_bool trace_enabled, u64 pid, u64 id, u64 ret) {
    const char *name;

    if (CLKS_CFG_SYSCALL_SERIAL_LOG == 0) {
        return;
    }

    if (trace_enabled == CLKS_FALSE) {
        return;
    }

    /* Suppress empty FD_READ polls, keep real data/error returns visible. */
    if (id == CLKS_SYSCALL_FD_READ && ret == 0ULL) {
        return;
    }

    name = clks_syscall_name(id);
    clks_serial_write("[INFO][SYSCALL] RET  PID: 0X");
    clks_syscall_serial_write_hex64(pid);
    clks_serial_write(" ID: 0X");
    clks_syscall_serial_write_hex64(id);
    clks_serial_write(" NAME: ");
    clks_serial_write(name);
    clks_serial_write(" RET: 0X");
    clks_syscall_serial_write_hex64(ret);
    clks_serial_write("\n");
}
