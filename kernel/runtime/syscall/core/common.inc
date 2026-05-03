
/* Yes, this file is a syscall kitchen sink and nobody is pretending otherwise. */

#define CLKS_SYSCALL_LOG_MAX_LEN 191U
#define CLKS_SYSCALL_PATH_MAX 192U
#define CLKS_SYSCALL_NAME_MAX 96U
#define CLKS_SYSCALL_TTY_MAX_LEN 2048U
#define CLKS_SYSCALL_FS_IO_CHUNK_LEN 65536U
#define CLKS_SYSCALL_JOURNAL_MAX_LEN 256U
#define CLKS_SYSCALL_ARG_LINE_MAX 256U
#define CLKS_SYSCALL_ENV_LINE_MAX 512U
#define CLKS_SYSCALL_ITEM_MAX 128U
#define CLKS_SYSCALL_PROCFS_TEXT_MAX 2048U
#define CLKS_SYSCALL_USER_TRACE_BUDGET 128ULL
#define CLKS_SYSCALL_KDBG_TEXT_MAX 2048U
#define CLKS_SYSCALL_KDBG_BT_MAX_FRAMES 16U
#define CLKS_SYSCALL_KDBG_STACK_WINDOW_BYTES (128ULL * 1024ULL)
#define CLKS_SYSCALL_KERNEL_SYMBOL_FILE "/system/kernel.sym"
#define CLKS_SYSCALL_KERNEL_ADDR_BASE 0xFFFF800000000000ULL
#define CLKS_SYSCALL_STATS_MAX_ID CLKS_SYSCALL_DISK_FSCK_FAT32
#define CLKS_SYSCALL_DISK_SECTOR_BYTES 512U
#define CLKS_SYSCALL_NET_UDP_PAYLOAD_MAX 1472U
#define CLKS_SYSCALL_NET_TCP_IO_MAX 65536U
#define CLKS_SYSCALL_STATS_RING_SIZE 256U
#define CLKS_SYSCALL_USC_MAX_ALLOWED_APPS 64U
#define CLKS_SYSCALL_USC_PERM_RULE_FILE ".usc_permanent_allowlist"

#ifndef CLKS_CFG_PROCFS
#define CLKS_CFG_PROCFS 1
#endif

#ifndef CLKS_CFG_SYSCALL_SERIAL_LOG
#define CLKS_CFG_SYSCALL_SERIAL_LOG 1
#endif

#ifndef CLKS_CFG_SYSCALL_USERID_SERIAL_LOG
#define CLKS_CFG_SYSCALL_USERID_SERIAL_LOG 1
#endif

#ifndef CLKS_CFG_USC
#define CLKS_CFG_USC 1
#endif

#ifndef CLKS_CFG_USC_SC_FS_MKDIR
#define CLKS_CFG_USC_SC_FS_MKDIR 1
#endif

#ifndef CLKS_CFG_USC_SC_FS_WRITE
#define CLKS_CFG_USC_SC_FS_WRITE 1
#endif

#ifndef CLKS_CFG_USC_SC_FS_APPEND
#define CLKS_CFG_USC_SC_FS_APPEND 1
#endif

#ifndef CLKS_CFG_USC_SC_FS_REMOVE
#define CLKS_CFG_USC_SC_FS_REMOVE 1
#endif

#ifndef CLKS_CFG_USC_SC_EXEC_PATH
#define CLKS_CFG_USC_SC_EXEC_PATH 1
#endif

#ifndef CLKS_CFG_USC_SC_EXEC_PATHV
#define CLKS_CFG_USC_SC_EXEC_PATHV 1
#endif

#ifndef CLKS_CFG_USC_SC_EXEC_PATHV_IO
#define CLKS_CFG_USC_SC_EXEC_PATHV_IO 1
#endif

#ifndef CLKS_CFG_USC_SC_SPAWN_PATH
#define CLKS_CFG_USC_SC_SPAWN_PATH 1
#endif

#ifndef CLKS_CFG_USC_SC_SPAWN_PATHV
#define CLKS_CFG_USC_SC_SPAWN_PATHV 1
#endif

#ifndef CLKS_CFG_USC_SC_PROC_KILL
#define CLKS_CFG_USC_SC_PROC_KILL 1
#endif

#ifndef CLKS_CFG_USC_SC_SHUTDOWN
#define CLKS_CFG_USC_SC_SHUTDOWN 1
#endif

#ifndef CLKS_CFG_USC_SC_RESTART
#define CLKS_CFG_USC_SC_RESTART 1
#endif

struct clks_syscall_frame {
    u64 rax;
    u64 rbx;
    u64 rcx;
    u64 rdx;
    u64 rsi;
    u64 rdi;
    u64 rbp;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 vector;
    u64 error_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

struct clks_syscall_kdbg_bt_req {
    u64 rbp;
    u64 rip;
    u64 out_ptr;
    u64 out_size;
};

struct clks_syscall_exec_io_req {
    u64 env_line_ptr;
    u64 stdin_fd;
    u64 stdout_fd;
    u64 stderr_fd;
};

struct clks_syscall_fb_info_user {
    u64 width;
    u64 height;
    u64 pitch;
    u64 bpp;
};

struct clks_syscall_fb_blit_req {
    u64 pixels_ptr;
    u64 src_width;
    u64 src_height;
    u64 src_pitch_bytes;
    u64 dst_x;
    u64 dst_y;
    u64 scale;
};

struct clks_syscall_net_udp_send_req {
    u64 dst_ipv4_be;
    u64 dst_port;
    u64 src_port;
    u64 payload_ptr;
    u64 payload_len;
};

struct clks_syscall_net_udp_recv_req {
    u64 out_payload_ptr;
    u64 payload_capacity;
    u64 out_src_ipv4_ptr;
    u64 out_src_port_ptr;
    u64 out_dst_port_ptr;
};

struct clks_syscall_net_tcp_connect_req {
    u64 dst_ipv4_be;
    u64 dst_port;
    u64 src_port;
    u64 poll_budget;
};

struct clks_syscall_net_tcp_send_req {
    u64 payload_ptr;
    u64 payload_len;
    u64 poll_budget;
};

struct clks_syscall_net_tcp_recv_req {
    u64 out_payload_ptr;
    u64 payload_capacity;
    u64 poll_budget;
};

struct clks_syscall_driver_info_user {
    char name[CLKS_DRIVER_NAME_MAX];
    char path[CLKS_DRIVER_PATH_MAX];
    u64 kind;
    u64 state;
    u64 driver_class;
    u64 from_elf;
    u64 image_size;
    u64 elf_entry;
    u64 load_id;
    u64 owner_pid;
};

struct clks_syscall_mouse_state_user {
    u64 x;
    u64 y;
    u64 buttons;
    u64 packet_count;
    u64 ready;
};

struct clks_syscall_user_info_user {
    u64 uid;
    u64 role;
    u64 logged_in;
    u64 disk_login_required;
    char name[CLKS_USER_NAME_MAX];
    char home[CLKS_USER_HOME_MAX];
};

struct clks_syscall_user_login_req {
    u64 name_ptr;
    u64 password_ptr;
    u64 out_info_ptr;
};

struct clks_syscall_user_add_req {
    u64 name_ptr;
    u64 password_ptr;
    u64 role;
};

struct clks_syscall_user_passwd_req {
    u64 name_ptr;
    u64 old_password_ptr;
    u64 new_password_ptr;
};

struct clks_syscall_wm_create_req {
    u64 x;
    u64 y;
    u64 width;
    u64 height;
    u64 flags;
};

struct clks_syscall_wm_present_req {
    u64 window_id;
    u64 pixels_ptr;
    u64 src_width;
    u64 src_height;
    u64 src_pitch_bytes;
};

struct clks_syscall_wm_move_req {
    u64 window_id;
    u64 x;
    u64 y;
};

struct clks_syscall_wm_resize_req {
    u64 window_id;
    u64 width;
    u64 height;
};

static clks_bool clks_syscall_ready = CLKS_FALSE;
static clks_bool clks_syscall_user_trace_active = CLKS_FALSE;
static u64 clks_syscall_user_trace_budget = 0ULL;
static struct clks_syscall_frame clks_syscall_last_frame;
static clks_bool clks_syscall_last_frame_valid = CLKS_FALSE;
static clks_bool clks_syscall_symbols_checked = CLKS_FALSE;
static const char *clks_syscall_symbols_data = CLKS_NULL;
static u64 clks_syscall_symbols_size = 0ULL;
static u64 clks_syscall_stats_total = 0ULL;
static u64 clks_syscall_stats_id_count[CLKS_SYSCALL_STATS_MAX_ID + 1ULL];
static u64 clks_syscall_stats_recent_id_count[CLKS_SYSCALL_STATS_MAX_ID + 1ULL];
static u16 clks_syscall_stats_recent_ring[CLKS_SYSCALL_STATS_RING_SIZE];
static u32 clks_syscall_stats_recent_head = 0U;
static u32 clks_syscall_stats_recent_size = 0U;
#if CLKS_CFG_USC != 0
static clks_bool clks_syscall_usc_session_allowed_used[CLKS_SYSCALL_USC_MAX_ALLOWED_APPS];
static char clks_syscall_usc_session_allowed_path[CLKS_SYSCALL_USC_MAX_ALLOWED_APPS][CLKS_EXEC_PROC_PATH_MAX];
static clks_bool clks_syscall_usc_permanent_allowed_used[CLKS_SYSCALL_USC_MAX_ALLOWED_APPS];
static char clks_syscall_usc_permanent_allowed_path[CLKS_SYSCALL_USC_MAX_ALLOWED_APPS][CLKS_EXEC_PROC_PATH_MAX];
static clks_bool clks_syscall_usc_permanent_loaded = CLKS_FALSE;

enum clks_syscall_usc_decision {
    CLKS_SYSCALL_USC_DECISION_DENY = 0,
    CLKS_SYSCALL_USC_DECISION_ALLOW_ONCE = 1,
    CLKS_SYSCALL_USC_DECISION_ALLOW_SESSION = 2,
    CLKS_SYSCALL_USC_DECISION_ALLOW_PERMANENT = 3,
};
#endif

#if defined(CLKS_ARCH_X86_64)
static inline void clks_syscall_outb(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void clks_syscall_outw(u16 port, u16 value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
#endif

static clks_bool clks_syscall_in_user_exec_context(void) {
    /* If this says "no", we're basically in trust-me-bro kernel mode. */
    return (clks_exec_is_running() == CLKS_TRUE && clks_exec_current_path_is_user() == CLKS_TRUE) ? CLKS_TRUE
                                                                                                  : CLKS_FALSE;
}

static clks_bool clks_syscall_user_ptr_readable(u64 addr, u64 size) {
    if (addr == 0ULL || size == 0ULL) {
        return CLKS_FALSE;
    }

    if (clks_syscall_in_user_exec_context() == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    return clks_exec_current_user_ptr_readable(addr, size);
}

static clks_bool clks_syscall_user_ptr_writable(u64 addr, u64 size) {
    if (addr == 0ULL || size == 0ULL) {
        return CLKS_FALSE;
    }

    if (clks_syscall_in_user_exec_context() == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    return clks_exec_current_user_ptr_writable(addr, size);
}

static clks_bool clks_syscall_copy_user_string(u64 src_addr, char *dst, usize dst_size) {
    usize i = 0U;

    if (src_addr == 0ULL || dst == CLKS_NULL || dst_size == 0U) {
        return CLKS_FALSE;
    }

    /* Byte-by-byte copy is slow as hell, but crashing on bad pointers is worse. */
    while (i + 1U < dst_size) {
        u64 char_addr = src_addr + (u64)i;
        char ch;

        if (char_addr < src_addr) {
            return CLKS_FALSE;
        }

        if (clks_syscall_user_ptr_readable(char_addr, 1ULL) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        ch = *(const char *)(usize)char_addr;
        dst[i] = ch;

        if (ch == '\0') {
            return CLKS_TRUE;
        }

        i++;
    }

    dst[dst_size - 1U] = '\0';
    return CLKS_TRUE;
}

static clks_bool clks_syscall_copy_user_optional_string(u64 src_addr, char *dst, usize dst_size) {
    if (dst == CLKS_NULL || dst_size == 0U) {
        return CLKS_FALSE;
    }

    if (src_addr == 0ULL) {
        dst[0] = '\0';
        return CLKS_TRUE;
    }

    return clks_syscall_copy_user_string(src_addr, dst, dst_size);
}

static u64 clks_syscall_copy_text_to_user(u64 dst_addr, u64 dst_size, const char *src, usize src_len) {
    usize copy_len;

    if (dst_addr == 0ULL || dst_size == 0ULL || src == CLKS_NULL) {
        return 0ULL;
    }

    copy_len = src_len;

    /* Leave room for NUL, because "almost a string" is just pain. */
    if (copy_len + 1U > (usize)dst_size) {
        copy_len = (usize)dst_size - 1U;
    }

    if (clks_syscall_user_ptr_writable(dst_addr, (u64)copy_len + 1ULL) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy((void *)dst_addr, src, copy_len);
    ((char *)dst_addr)[copy_len] = '\0';
    return (u64)copy_len;
}
