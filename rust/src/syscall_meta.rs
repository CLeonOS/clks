use core::ptr;

#[repr(C)]
pub struct ClksRustSyscallMeta {
    pub id: u64,
    pub name: *const u8,
    pub category: u32,
    pub argc: u8,
    pub flags: u32,
    pub usc_gate: u32,
}

struct SyscallMetaEntry {
    name: &'static [u8],
    category: u32,
    argc: u8,
    flags: u32,
    usc_gate: u32,
}

const SYSCALL_META_MAX_ID: u64 = 159;

static SYSCALL_META: [SyscallMetaEntry; 160] = [
    SyscallMetaEntry { name: b"LOG_WRITE\0", category: 0, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 0
    SyscallMetaEntry { name: b"TIMER_TICKS\0", category: 0, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 1
    SyscallMetaEntry { name: b"TASK_COUNT\0", category: 0, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 2
    SyscallMetaEntry { name: b"CURRENT_TASK_ID\0", category: 0, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 3
    SyscallMetaEntry { name: b"SERVICE_COUNT\0", category: 0, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 4
    SyscallMetaEntry { name: b"SERVICE_READY_COUNT\0", category: 0, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 5
    SyscallMetaEntry { name: b"CONTEXT_SWITCHES\0", category: 0, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 6
    SyscallMetaEntry { name: b"RESERVED_7\0", category: 20, argc: 0u8, flags: 0x00000001u32, usc_gate: 0 }, // 7
    SyscallMetaEntry { name: b"RESERVED_8\0", category: 20, argc: 0u8, flags: 0x00000001u32, usc_gate: 0 }, // 8
    SyscallMetaEntry { name: b"FS_NODE_COUNT\0", category: 1, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 9
    SyscallMetaEntry { name: b"FS_CHILD_COUNT\0", category: 1, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 10
    SyscallMetaEntry { name: b"FS_GET_CHILD_NAME\0", category: 1, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 11
    SyscallMetaEntry { name: b"FS_READ\0", category: 1, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 12
    SyscallMetaEntry { name: b"EXEC_PATH\0", category: 2, argc: 1u8, flags: 0x00000050u32, usc_gate: 6 }, // 13
    SyscallMetaEntry { name: b"EXEC_REQUESTS\0", category: 2, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 14
    SyscallMetaEntry { name: b"EXEC_SUCCESS\0", category: 2, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 15
    SyscallMetaEntry { name: b"USER_SHELL_READY\0", category: 2, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 16
    SyscallMetaEntry { name: b"USER_EXEC_REQUESTED\0", category: 2, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 17
    SyscallMetaEntry { name: b"USER_LAUNCH_TRIES\0", category: 2, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 18
    SyscallMetaEntry { name: b"USER_LAUNCH_OK\0", category: 2, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 19
    SyscallMetaEntry { name: b"USER_LAUNCH_FAIL\0", category: 2, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 20
    SyscallMetaEntry { name: b"TTY_COUNT\0", category: 3, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 21
    SyscallMetaEntry { name: b"TTY_ACTIVE\0", category: 3, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 22
    SyscallMetaEntry { name: b"TTY_SWITCH\0", category: 3, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 23
    SyscallMetaEntry { name: b"TTY_WRITE\0", category: 3, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 24
    SyscallMetaEntry { name: b"TTY_WRITE_CHAR\0", category: 3, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 25
    SyscallMetaEntry { name: b"KBD_GET_CHAR\0", category: 4, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 26
    SyscallMetaEntry { name: b"FS_STAT_TYPE\0", category: 1, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 27
    SyscallMetaEntry { name: b"FS_STAT_SIZE\0", category: 1, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 28
    SyscallMetaEntry { name: b"FS_MKDIR\0", category: 1, argc: 1u8, flags: 0x00000030u32, usc_gate: 2 }, // 29
    SyscallMetaEntry { name: b"FS_WRITE\0", category: 1, argc: 3u8, flags: 0x00000030u32, usc_gate: 3 }, // 30
    SyscallMetaEntry { name: b"FS_APPEND\0", category: 1, argc: 3u8, flags: 0x00000030u32, usc_gate: 4 }, // 31
    SyscallMetaEntry { name: b"FS_REMOVE\0", category: 1, argc: 1u8, flags: 0x00000030u32, usc_gate: 5 }, // 32
    SyscallMetaEntry { name: b"LOG_JOURNAL_COUNT\0", category: 0, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 33
    SyscallMetaEntry { name: b"LOG_JOURNAL_READ\0", category: 0, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 34
    SyscallMetaEntry { name: b"KBD_BUFFERED\0", category: 4, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 35
    SyscallMetaEntry { name: b"KBD_PUSHED\0", category: 4, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 36
    SyscallMetaEntry { name: b"KBD_POPPED\0", category: 4, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 37
    SyscallMetaEntry { name: b"KBD_DROPPED\0", category: 4, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 38
    SyscallMetaEntry { name: b"KBD_HOTKEY_SWITCHES\0", category: 4, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 39
    SyscallMetaEntry { name: b"GETPID\0", category: 5, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 40
    SyscallMetaEntry { name: b"SPAWN_PATH\0", category: 5, argc: 1u8, flags: 0x00000050u32, usc_gate: 9 }, // 41
    SyscallMetaEntry { name: b"WAITPID\0", category: 5, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 42
    SyscallMetaEntry { name: b"EXIT\0", category: 5, argc: 1u8, flags: 0x00000040u32, usc_gate: 0 }, // 43
    SyscallMetaEntry { name: b"SLEEP_TICKS\0", category: 5, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 44
    SyscallMetaEntry { name: b"YIELD\0", category: 5, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 45
    SyscallMetaEntry { name: b"SHUTDOWN\0", category: 0, argc: 0u8, flags: 0x00000050u32, usc_gate: 12 }, // 46
    SyscallMetaEntry { name: b"RESTART\0", category: 0, argc: 0u8, flags: 0x00000050u32, usc_gate: 13 }, // 47
    SyscallMetaEntry { name: b"AUDIO_AVAILABLE\0", category: 19, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 48
    SyscallMetaEntry { name: b"AUDIO_PLAY_TONE\0", category: 19, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 49
    SyscallMetaEntry { name: b"AUDIO_STOP\0", category: 19, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 50
    SyscallMetaEntry { name: b"EXEC_PATHV\0", category: 2, argc: 3u8, flags: 0x00000050u32, usc_gate: 7 }, // 51
    SyscallMetaEntry { name: b"SPAWN_PATHV\0", category: 5, argc: 3u8, flags: 0x00000050u32, usc_gate: 10 }, // 52
    SyscallMetaEntry { name: b"PROC_ARGC\0", category: 5, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 53
    SyscallMetaEntry { name: b"PROC_ARGV\0", category: 5, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 54
    SyscallMetaEntry { name: b"PROC_ENVC\0", category: 5, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 55
    SyscallMetaEntry { name: b"PROC_ENV\0", category: 5, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 56
    SyscallMetaEntry { name: b"PROC_LAST_SIGNAL\0", category: 5, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 57
    SyscallMetaEntry { name: b"PROC_FAULT_VECTOR\0", category: 5, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 58
    SyscallMetaEntry { name: b"PROC_FAULT_ERROR\0", category: 5, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 59
    SyscallMetaEntry { name: b"PROC_FAULT_RIP\0", category: 5, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 60
    SyscallMetaEntry { name: b"PROC_COUNT\0", category: 5, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 61
    SyscallMetaEntry { name: b"PROC_PID_AT\0", category: 5, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 62
    SyscallMetaEntry { name: b"PROC_SNAPSHOT\0", category: 5, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 63
    SyscallMetaEntry { name: b"PROC_KILL\0", category: 5, argc: 2u8, flags: 0x00000050u32, usc_gate: 11 }, // 64
    SyscallMetaEntry { name: b"KDBG_SYM\0", category: 6, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 65
    SyscallMetaEntry { name: b"KDBG_BT\0", category: 6, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 66
    SyscallMetaEntry { name: b"KDBG_REGS\0", category: 6, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 67
    SyscallMetaEntry { name: b"STATS_TOTAL\0", category: 7, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 68
    SyscallMetaEntry { name: b"STATS_ID_COUNT\0", category: 7, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 69
    SyscallMetaEntry { name: b"STATS_RECENT_WINDOW\0", category: 7, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 70
    SyscallMetaEntry { name: b"STATS_RECENT_ID\0", category: 7, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 71
    SyscallMetaEntry { name: b"FD_OPEN\0", category: 8, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 72
    SyscallMetaEntry { name: b"FD_READ\0", category: 8, argc: 3u8, flags: 0x0000000Cu32, usc_gate: 0 }, // 73
    SyscallMetaEntry { name: b"FD_WRITE\0", category: 8, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 74
    SyscallMetaEntry { name: b"FD_CLOSE\0", category: 8, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 75
    SyscallMetaEntry { name: b"FD_DUP\0", category: 8, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 76
    SyscallMetaEntry { name: b"DL_OPEN\0", category: 9, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 77
    SyscallMetaEntry { name: b"DL_CLOSE\0", category: 9, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 78
    SyscallMetaEntry { name: b"DL_SYM\0", category: 9, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 79
    SyscallMetaEntry { name: b"EXEC_PATHV_IO\0", category: 2, argc: 3u8, flags: 0x00000050u32, usc_gate: 8 }, // 80
    SyscallMetaEntry { name: b"FB_INFO\0", category: 10, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 81
    SyscallMetaEntry { name: b"FB_BLIT\0", category: 10, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 82
    SyscallMetaEntry { name: b"FB_CLEAR\0", category: 10, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 83
    SyscallMetaEntry { name: b"KERNEL_VERSION\0", category: 0, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 84
    SyscallMetaEntry { name: b"DISK_PRESENT\0", category: 11, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 85
    SyscallMetaEntry { name: b"DISK_SIZE_BYTES\0", category: 11, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 86
    SyscallMetaEntry { name: b"DISK_SECTOR_COUNT\0", category: 11, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 87
    SyscallMetaEntry { name: b"DISK_FORMATTED\0", category: 11, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 88
    SyscallMetaEntry { name: b"DISK_FORMAT_FAT32\0", category: 11, argc: 1u8, flags: 0x00000090u32, usc_gate: 1 }, // 89
    SyscallMetaEntry { name: b"DISK_MOUNT\0", category: 11, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 90
    SyscallMetaEntry { name: b"DISK_MOUNTED\0", category: 11, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 91
    SyscallMetaEntry { name: b"DISK_MOUNT_PATH\0", category: 11, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 92
    SyscallMetaEntry { name: b"DISK_READ_SECTOR\0", category: 11, argc: 2u8, flags: 0x00000080u32, usc_gate: 0 }, // 93
    SyscallMetaEntry { name: b"DISK_WRITE_SECTOR\0", category: 11, argc: 2u8, flags: 0x00000090u32, usc_gate: 1 }, // 94
    SyscallMetaEntry { name: b"NET_AVAILABLE\0", category: 12, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 95
    SyscallMetaEntry { name: b"NET_IPV4_ADDR\0", category: 12, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 96
    SyscallMetaEntry { name: b"NET_PING\0", category: 12, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 97
    SyscallMetaEntry { name: b"NET_UDP_SEND\0", category: 12, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 98
    SyscallMetaEntry { name: b"NET_UDP_RECV\0", category: 12, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 99
    SyscallMetaEntry { name: b"NET_NETMASK\0", category: 12, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 100
    SyscallMetaEntry { name: b"NET_GATEWAY\0", category: 12, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 101
    SyscallMetaEntry { name: b"NET_DNS_SERVER\0", category: 12, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 102
    SyscallMetaEntry { name: b"NET_TCP_CONNECT\0", category: 12, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 103
    SyscallMetaEntry { name: b"NET_TCP_SEND\0", category: 12, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 104
    SyscallMetaEntry { name: b"NET_TCP_RECV\0", category: 12, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 105
    SyscallMetaEntry { name: b"NET_TCP_CLOSE\0", category: 12, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 106
    SyscallMetaEntry { name: b"MOUSE_STATE\0", category: 0, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 107
    SyscallMetaEntry { name: b"WM_CREATE\0", category: 17, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 108
    SyscallMetaEntry { name: b"WM_DESTROY\0", category: 17, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 109
    SyscallMetaEntry { name: b"WM_PRESENT\0", category: 17, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 110
    SyscallMetaEntry { name: b"WM_POLL_EVENT\0", category: 17, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 111
    SyscallMetaEntry { name: b"WM_MOVE\0", category: 17, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 112
    SyscallMetaEntry { name: b"WM_SET_FOCUS\0", category: 17, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 113
    SyscallMetaEntry { name: b"WM_SET_FLAGS\0", category: 17, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 114
    SyscallMetaEntry { name: b"WM_RESIZE\0", category: 17, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 115
    SyscallMetaEntry { name: b"PTY_OPEN\0", category: 8, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 116
    SyscallMetaEntry { name: b"WM_COUNT\0", category: 17, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 117
    SyscallMetaEntry { name: b"WM_ID_AT\0", category: 17, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 118
    SyscallMetaEntry { name: b"WM_SNAPSHOT\0", category: 17, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 119
    SyscallMetaEntry { name: b"USER_HEAP_ALLOC\0", category: 13, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 120
    SyscallMetaEntry { name: b"DRIVER_COUNT\0", category: 18, argc: 0u8, flags: 0x00000002u32, usc_gate: 0 }, // 121
    SyscallMetaEntry { name: b"DRIVER_INFO\0", category: 18, argc: 3u8, flags: 0x00000002u32, usc_gate: 0 }, // 122
    SyscallMetaEntry { name: b"DRIVER_LOAD\0", category: 18, argc: 1u8, flags: 0x00000002u32, usc_gate: 0 }, // 123
    SyscallMetaEntry { name: b"DRIVER_UNLOAD\0", category: 18, argc: 1u8, flags: 0x00000002u32, usc_gate: 0 }, // 124
    SyscallMetaEntry { name: b"DRIVER_RELOAD\0", category: 18, argc: 0u8, flags: 0x00000002u32, usc_gate: 0 }, // 125
    SyscallMetaEntry { name: b"TIMER_HZ\0", category: 0, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 126
    SyscallMetaEntry { name: b"TIME_MS\0", category: 0, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 127
    SyscallMetaEntry { name: b"SLEEP_MS\0", category: 5, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 128
    SyscallMetaEntry { name: b"NET_TCP_LAST_ERROR\0", category: 12, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 129
    SyscallMetaEntry { name: b"VM_ALLOC\0", category: 13, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 130
    SyscallMetaEntry { name: b"VM_FREE\0", category: 13, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 131
    SyscallMetaEntry { name: b"USER_CURRENT\0", category: 14, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 132
    SyscallMetaEntry { name: b"USER_LOGIN\0", category: 14, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 133
    SyscallMetaEntry { name: b"USER_LOGOUT\0", category: 14, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 134
    SyscallMetaEntry { name: b"USER_COUNT\0", category: 14, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 135
    SyscallMetaEntry { name: b"USER_AT\0", category: 14, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 136
    SyscallMetaEntry { name: b"USER_ADD\0", category: 14, argc: 1u8, flags: 0x00000100u32, usc_gate: 0 }, // 137
    SyscallMetaEntry { name: b"USER_PASSWD\0", category: 14, argc: 1u8, flags: 0x00000100u32, usc_gate: 0 }, // 138
    SyscallMetaEntry { name: b"USER_SET_ROLE\0", category: 14, argc: 2u8, flags: 0x00000100u32, usc_gate: 0 }, // 139
    SyscallMetaEntry { name: b"USER_REMOVE\0", category: 14, argc: 1u8, flags: 0x00000100u32, usc_gate: 0 }, // 140
    SyscallMetaEntry { name: b"USER_IS_ADMIN\0", category: 14, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 141
    SyscallMetaEntry { name: b"DISK_FSCK_FAT32\0", category: 11, argc: 2u8, flags: 0x00000090u32, usc_gate: 1 }, // 142
    SyscallMetaEntry { name: b"SYSINFO\0", category: 0, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 143
    SyscallMetaEntry { name: b"LOCALE_GET\0", category: 0, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 144
    SyscallMetaEntry { name: b"LOCALE_SET\0", category: 0, argc: 1u8, flags: 0x00000100u32, usc_gate: 0 }, // 145
    SyscallMetaEntry { name: b"MMAP\0", category: 13, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 146
    SyscallMetaEntry { name: b"DISPLAY_INFO\0", category: 15, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 147
    SyscallMetaEntry { name: b"DISPLAY_SET_MODE\0", category: 15, argc: 1u8, flags: 0x00000100u32, usc_gate: 0 }, // 148
    SyscallMetaEntry { name: b"TTY_GRID_INFO\0", category: 3, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 149
    SyscallMetaEntry { name: b"INPUTM_COUNT\0", category: 16, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 150
    SyscallMetaEntry { name: b"INPUTM_INFO\0", category: 16, argc: 3u8, flags: 0x00000000u32, usc_gate: 0 }, // 151
    SyscallMetaEntry { name: b"INPUTM_CURRENT\0", category: 16, argc: 0u8, flags: 0x00000000u32, usc_gate: 0 }, // 152
    SyscallMetaEntry { name: b"INPUTM_SELECT\0", category: 16, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 153
    SyscallMetaEntry { name: b"INPUTM_REGISTER\0", category: 16, argc: 1u8, flags: 0x00000100u32, usc_gate: 0 }, // 154
    SyscallMetaEntry { name: b"TTY_STATUS_SET\0", category: 3, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 155
    SyscallMetaEntry { name: b"BOOT_CMDLINE\0", category: 0, argc: 2u8, flags: 0x00000000u32, usc_gate: 0 }, // 156
    SyscallMetaEntry { name: b"INPUTM_REGISTER_RULE\0", category: 16, argc: 1u8, flags: 0x00000100u32, usc_gate: 0 }, // 157
    SyscallMetaEntry { name: b"NET_TCP_LISTEN\0", category: 12, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 158
    SyscallMetaEntry { name: b"NET_TCP_ACCEPT\0", category: 12, argc: 1u8, flags: 0x00000000u32, usc_gate: 0 }, // 159
];

#[no_mangle]
pub extern "C" fn clks_rust_syscall_meta_max_id() -> u64 {
    SYSCALL_META_MAX_ID
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_syscall_meta_fill(id: u64, out: *mut ClksRustSyscallMeta) -> u32 {
    if out.is_null() || id > SYSCALL_META_MAX_ID {
        return 0;
    }

    let entry = &SYSCALL_META[id as usize];
    ptr::write(
        out,
        ClksRustSyscallMeta {
            id,
            name: entry.name.as_ptr(),
            category: entry.category,
            argc: entry.argc,
            flags: entry.flags,
            usc_gate: entry.usc_gate,
        },
    );
    1
}
