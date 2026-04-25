#include <clks/cpu.h>
#include <clks/audio.h>
#include <clks/disk.h>
#include <clks/exec.h>
#include <clks/framebuffer.h>
#include <clks/fs.h>
#include <clks/heap.h>
#include <clks/interrupts.h>
#include <clks/kelf.h>
#include <clks/keyboard.h>
#include <clks/log.h>
#include <clks/mouse.h>
#include <clks/net.h>
#include <clks/serial.h>
#include <clks/scheduler.h>
#include <clks/service.h>
#include <clks/string.h>
#include <clks/syscall.h>
#include <clks/tty.h>
#include <clks/types.h>
#include <clks/userland.h>
#include <clks/version.h>
#include <clks/wm.h>

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
#define CLKS_SYSCALL_STATS_MAX_ID CLKS_SYSCALL_WM_RESIZE
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

struct clks_syscall_mouse_state_user {
    u64 x;
    u64 y;
    u64 buttons;
    u64 packet_count;
    u64 ready;
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

static u64 clks_syscall_log_write(u64 arg0, u64 arg1) {
    const char *src = (const char *)arg0;
    u64 len = arg1;
    char buf[CLKS_SYSCALL_LOG_MAX_LEN + 1U];
    u64 i;

    if (src == CLKS_NULL || len == 0ULL) {
        return 0ULL;
    }

    if (len > CLKS_SYSCALL_LOG_MAX_LEN) {
        len = CLKS_SYSCALL_LOG_MAX_LEN;
    }

    if (clks_syscall_user_ptr_readable((u64)(usize)src, len) == CLKS_FALSE) {
        return 0ULL;
    }

    for (i = 0ULL; i < len; i++) {
        buf[i] = src[i];
    }

    buf[len] = '\0';
    clks_log(CLKS_LOG_INFO, "SYSCALL", buf);

    return len;
}

static u64 clks_syscall_tty_write(u64 arg0, u64 arg1) {
    const char *src = (const char *)arg0;
    u64 len = arg1;
    char buf[CLKS_SYSCALL_TTY_MAX_LEN + 1U];
    u64 i;

    if (src == CLKS_NULL || len == 0ULL) {
        return 0ULL;
    }

    if (len > CLKS_SYSCALL_TTY_MAX_LEN) {
        len = CLKS_SYSCALL_TTY_MAX_LEN;
    }

    if (clks_syscall_user_ptr_readable((u64)(usize)src, len) == CLKS_FALSE) {
        return 0ULL;
    }

    for (i = 0ULL; i < len; i++) {
        buf[i] = src[i];
    }

    buf[len] = '\0';
    clks_tty_write(buf);
    return len;
}

static u64 clks_syscall_tty_write_char(u64 arg0) {
    clks_tty_write_char((char)(arg0 & 0xFFULL));
    return 1ULL;
}

static u64 clks_syscall_kbd_get_char(void) {
    char ch;
    u32 tty_index = clks_exec_current_tty();

    if (clks_keyboard_pop_char_for_tty(tty_index, &ch) == CLKS_FALSE) {
        return (u64)-1;
    }

    return (u64)(u8)ch;
}

static u64 clks_syscall_fb_info(u64 arg0) {
    struct clks_syscall_fb_info_user *out_info = (struct clks_syscall_fb_info_user *)arg0;
    struct clks_framebuffer_info fb_info;

    if (arg0 == 0ULL || clks_fb_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg0, (u64)sizeof(*out_info)) == CLKS_FALSE) {
        return 0ULL;
    }

    fb_info = clks_fb_info();
    out_info->width = (u64)fb_info.width;
    out_info->height = (u64)fb_info.height;
    out_info->pitch = (u64)fb_info.pitch;
    out_info->bpp = (u64)fb_info.bpp;
    return 1ULL;
}

static u64 clks_syscall_fb_clear(u64 arg0) {
    if (clks_fb_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    clks_fb_clear((u32)(arg0 & 0xFFFFFFFFULL));
    return 1ULL;
}

static u64 clks_syscall_fb_blit(u64 arg0) {
    struct clks_syscall_fb_blit_req req;
    const u8 *src_base;
    struct clks_framebuffer_info fb_info;
    u64 src_width;
    u64 src_height;
    u64 src_pitch_bytes;
    u64 dst_x;
    u64 dst_y;
    u64 scale;
    u64 y;
    u64 x;

    if (arg0 == 0ULL || clks_fb_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.pixels_ptr == 0ULL) {
        return 0ULL;
    }

    src_width = req.src_width;
    src_height = req.src_height;
    src_pitch_bytes = req.src_pitch_bytes;
    dst_x = req.dst_x;
    dst_y = req.dst_y;
    scale = req.scale;

    if (src_width == 0ULL || src_height == 0ULL || scale == 0ULL) {
        return 0ULL;
    }

    if (src_width > 4096ULL || src_height > 4096ULL || scale > 8ULL) {
        return 0ULL;
    }

    if (src_pitch_bytes == 0ULL) {
        src_pitch_bytes = src_width * 4ULL;
    }

    if (src_pitch_bytes < (src_width * 4ULL)) {
        return 0ULL;
    }

    if (src_pitch_bytes != 0ULL && src_height > (((u64)-1) / src_pitch_bytes)) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(req.pixels_ptr, src_pitch_bytes * src_height) == CLKS_FALSE) {
        return 0ULL;
    }

    src_base = (const u8 *)(usize)req.pixels_ptr;
    fb_info = clks_fb_info();

    if (dst_x >= (u64)fb_info.width || dst_y >= (u64)fb_info.height) {
        return 0ULL;
    }

    if (scale == 1ULL) {
        clks_fb_blit_rgba((i32)dst_x, (i32)dst_y, src_base, (u32)src_width, (u32)src_height, (u32)src_pitch_bytes);
        return 1ULL;
    }

    for (y = 0ULL; y < src_height; y++) {
        const u32 *src_row = (const u32 *)(const void *)(src_base + (usize)(y * src_pitch_bytes));
        u64 draw_y = dst_y + (y * scale);

        if (draw_y >= (u64)fb_info.height) {
            break;
        }

        for (x = 0ULL; x < src_width; x++) {
            u32 color = src_row[x];
            u64 draw_x = dst_x + (x * scale);

            if (draw_x >= (u64)fb_info.width) {
                break;
            }

            if (scale == 1ULL) {
                clks_fb_draw_pixel((u32)draw_x, (u32)draw_y, color);
            } else {
                clks_fb_fill_rect((u32)draw_x, (u32)draw_y, (u32)scale, (u32)scale, color);
            }
        }
    }

    return 1ULL;
}

static u64 clks_syscall_kernel_version(u64 arg0, u64 arg1) {
    /* Version query: tiny syscall, huge bike-shed potential. */
    usize len = clks_strlen(CLKS_VERSION_STRING);
    return clks_syscall_copy_text_to_user(arg0, arg1, CLKS_VERSION_STRING, len);
}

static u64 clks_syscall_disk_present(void) {
    return (clks_disk_present() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_size_bytes(void) {
    return clks_disk_size_bytes();
}

static u64 clks_syscall_disk_sector_count(void) {
    return clks_disk_sector_count();
}

static u64 clks_syscall_disk_formatted(void) {
    return (clks_disk_is_formatted_fat32() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_format_fat32(u64 arg0) {
    char label[16];

    if (clks_syscall_copy_user_optional_string(arg0, label, sizeof(label)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_disk_format_fat32((label[0] != '\0') ? label : CLKS_NULL) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_mount(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_disk_mount(path) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_mounted(void) {
    return (clks_disk_is_mounted() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_mount_path(u64 arg0, u64 arg1) {
    const char *mount_path = clks_disk_mount_path();

    if (mount_path == CLKS_NULL || mount_path[0] == '\0') {
        return 0ULL;
    }

    return clks_syscall_copy_text_to_user(arg0, arg1, mount_path, clks_strlen(mount_path));
}

static u64 clks_syscall_disk_read_sector(u64 arg0, u64 arg1) {
    u8 sector[CLKS_SYSCALL_DISK_SECTOR_BYTES];

    if (arg1 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)CLKS_SYSCALL_DISK_SECTOR_BYTES) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_disk_read_sector(arg0, (void *)sector) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy((void *)arg1, sector, (usize)CLKS_SYSCALL_DISK_SECTOR_BYTES);
    return 1ULL;
}

static u64 clks_syscall_disk_write_sector(u64 arg0, u64 arg1) {
    u8 sector[CLKS_SYSCALL_DISK_SECTOR_BYTES];

    if (arg1 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg1, (u64)CLKS_SYSCALL_DISK_SECTOR_BYTES) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(sector, (const void *)arg1, (usize)CLKS_SYSCALL_DISK_SECTOR_BYTES);
    return (clks_disk_write_sector(arg0, (const void *)sector) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_net_available(void) {
    return (clks_net_available() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_net_ipv4_addr(void) {
    return (u64)clks_net_ipv4_addr_be();
}

static u64 clks_syscall_net_netmask(void) {
    return (u64)clks_net_ipv4_netmask_be();
}

static u64 clks_syscall_net_gateway(void) {
    return (u64)clks_net_ipv4_gateway_be();
}

static u64 clks_syscall_net_dns_server(void) {
    return (u64)clks_net_ipv4_dns_be();
}

static u64 clks_syscall_net_ping(u64 arg0, u64 arg1) {
    return (clks_net_ping_ipv4((u32)arg0, arg1) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_net_udp_send(u64 arg0) {
    struct clks_syscall_net_udp_send_req req;
    void *payload_copy = CLKS_NULL;
    u64 payload_len;
    u64 sent = 0ULL;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));

    payload_len = req.payload_len;
    if (payload_len > (u64)CLKS_SYSCALL_NET_UDP_PAYLOAD_MAX) {
        return 0ULL;
    }

    if (payload_len > 0ULL) {
        if (req.payload_ptr == 0ULL) {
            return 0ULL;
        }

        if (clks_syscall_user_ptr_readable(req.payload_ptr, payload_len) == CLKS_FALSE) {
            return 0ULL;
        }

        payload_copy = clks_kmalloc((usize)payload_len);
        if (payload_copy == CLKS_NULL) {
            return 0ULL;
        }

        clks_memcpy(payload_copy, (const void *)req.payload_ptr, (usize)payload_len);
    }

    sent = clks_net_udp_send((u32)req.dst_ipv4_be, (u16)req.dst_port, (u16)req.src_port, payload_copy, payload_len);

    if (payload_copy != CLKS_NULL) {
        clks_kfree(payload_copy);
    }

    return sent;
}

static u64 clks_syscall_net_udp_recv(u64 arg0) {
    struct clks_syscall_net_udp_recv_req req;
    u8 packet[CLKS_SYSCALL_NET_UDP_PAYLOAD_MAX];
    u64 capacity;
    u64 got;
    u32 src_ipv4 = 0U;
    u16 src_port = 0U;
    u16 dst_port = 0U;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));

    if (req.out_payload_ptr == 0ULL || req.payload_capacity == 0ULL) {
        return 0ULL;
    }

    capacity = req.payload_capacity;
    if (capacity > (u64)sizeof(packet)) {
        capacity = (u64)sizeof(packet);
    }

    if (clks_syscall_user_ptr_writable(req.out_payload_ptr, capacity) == CLKS_FALSE) {
        return 0ULL;
    }

    if (req.out_src_ipv4_ptr != 0ULL &&
        clks_syscall_user_ptr_writable(req.out_src_ipv4_ptr, (u64)sizeof(u64)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (req.out_src_port_ptr != 0ULL &&
        clks_syscall_user_ptr_writable(req.out_src_port_ptr, (u64)sizeof(u64)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (req.out_dst_port_ptr != 0ULL &&
        clks_syscall_user_ptr_writable(req.out_dst_port_ptr, (u64)sizeof(u64)) == CLKS_FALSE) {
        return 0ULL;
    }

    got = clks_net_udp_recv(packet, capacity, &src_ipv4, &src_port, &dst_port);
    if (got == 0ULL) {
        return 0ULL;
    }

    clks_memcpy((void *)req.out_payload_ptr, packet, (usize)got);

    if (req.out_src_ipv4_ptr != 0ULL) {
        *((u64 *)(usize)req.out_src_ipv4_ptr) = (u64)src_ipv4;
    }

    if (req.out_src_port_ptr != 0ULL) {
        *((u64 *)(usize)req.out_src_port_ptr) = (u64)src_port;
    }

    if (req.out_dst_port_ptr != 0ULL) {
        *((u64 *)(usize)req.out_dst_port_ptr) = (u64)dst_port;
    }

    return got;
}

static u64 clks_syscall_net_tcp_connect(u64 arg0) {
    struct clks_syscall_net_tcp_connect_req req;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));
    return (clks_net_tcp_connect((u32)req.dst_ipv4_be, (u16)req.dst_port, (u16)req.src_port, req.poll_budget) ==
            CLKS_TRUE)
               ? 1ULL
               : 0ULL;
}

static u64 clks_syscall_net_tcp_send(u64 arg0) {
    struct clks_syscall_net_tcp_send_req req;
    void *payload_copy = CLKS_NULL;
    u64 payload_len;
    u64 sent = 0ULL;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));
    payload_len = req.payload_len;
    if (payload_len == 0ULL || payload_len > CLKS_SYSCALL_NET_TCP_IO_MAX) {
        return 0ULL;
    }

    if (req.payload_ptr == 0ULL || clks_syscall_user_ptr_readable(req.payload_ptr, payload_len) == CLKS_FALSE) {
        return 0ULL;
    }

    payload_copy = clks_kmalloc((usize)payload_len);
    if (payload_copy == CLKS_NULL) {
        return 0ULL;
    }

    clks_memcpy(payload_copy, (const void *)req.payload_ptr, (usize)payload_len);
    sent = clks_net_tcp_send(payload_copy, payload_len, req.poll_budget);
    clks_kfree(payload_copy);
    return sent;
}

static u64 clks_syscall_net_tcp_recv(u64 arg0) {
    struct clks_syscall_net_tcp_recv_req req;
    u8 packet[CLKS_SYSCALL_NET_TCP_IO_MAX];
    u64 capacity;
    u64 got;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));
    if (req.out_payload_ptr == 0ULL || req.payload_capacity == 0ULL) {
        return 0ULL;
    }

    capacity = req.payload_capacity;
    if (capacity > (u64)sizeof(packet)) {
        capacity = (u64)sizeof(packet);
    }

    if (clks_syscall_user_ptr_writable(req.out_payload_ptr, capacity) == CLKS_FALSE) {
        return 0ULL;
    }

    got = clks_net_tcp_recv(packet, capacity, req.poll_budget);
    if (got == 0ULL) {
        return 0ULL;
    }

    clks_memcpy((void *)req.out_payload_ptr, packet, (usize)got);
    return got;
}

static u64 clks_syscall_net_tcp_close(u64 arg0) {
    return (clks_net_tcp_close(arg0) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_mouse_state(u64 arg0) {
    struct clks_mouse_state state;
    struct clks_syscall_mouse_state_user *out_state = (struct clks_syscall_mouse_state_user *)(usize)arg0;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg0, (u64)sizeof(*out_state)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_mouse_snapshot(&state);
    out_state->x = (u64)state.x;
    out_state->y = (u64)state.y;
    out_state->buttons = (u64)state.buttons;
    out_state->packet_count = state.packet_count;
    out_state->ready = (state.ready == CLKS_TRUE) ? 1ULL : 0ULL;
    return 1ULL;
}

static clks_bool clks_syscall_u64_to_i32(u64 raw, i32 *out_value) {
    i64 value = (i64)raw;

    if (out_value == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (value < (-2147483647LL - 1LL) || value > 2147483647LL) {
        return CLKS_FALSE;
    }

    *out_value = (i32)value;
    return CLKS_TRUE;
}

static u64 clks_syscall_wm_owner_pid(void) {
    if (clks_syscall_in_user_exec_context() == CLKS_TRUE) {
        return clks_exec_current_pid();
    }

    return 0ULL;
}

static u64 clks_syscall_wm_create(u64 arg0) {
    struct clks_syscall_wm_create_req req;
    i32 x = 0;
    i32 y = 0;

    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.width > 0xFFFFFFFFULL || req.height > 0xFFFFFFFFULL) {
        return 0ULL;
    }

    if (clks_syscall_u64_to_i32(req.x, &x) == CLKS_FALSE || clks_syscall_u64_to_i32(req.y, &y) == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_wm_create(clks_syscall_wm_owner_pid(), x, y, (u32)req.width, (u32)req.height, req.flags);
}

static u64 clks_syscall_wm_destroy(u64 arg0) {
    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_destroy(clks_syscall_wm_owner_pid(), arg0) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_present(u64 arg0) {
    struct clks_syscall_wm_present_req req;
    u64 src_bytes;

    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.window_id == 0ULL || req.pixels_ptr == 0ULL) {
        return 0ULL;
    }

    if (req.src_width > 0xFFFFFFFFULL || req.src_height > 0xFFFFFFFFULL || req.src_pitch_bytes > 0xFFFFFFFFULL) {
        return 0ULL;
    }

    if (req.src_height == 0ULL || req.src_pitch_bytes == 0ULL) {
        return 0ULL;
    }

    if (req.src_height > (((u64)-1) / req.src_pitch_bytes)) {
        return 0ULL;
    }

    src_bytes = req.src_pitch_bytes * req.src_height;
    if (clks_syscall_user_ptr_readable(req.pixels_ptr, src_bytes) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_present(clks_syscall_wm_owner_pid(), req.window_id, (const void *)(usize)req.pixels_ptr,
                            (u32)req.src_width, (u32)req.src_height, (u32)req.src_pitch_bytes) == CLKS_TRUE)
               ? 1ULL
               : 0ULL;
}

static u64 clks_syscall_wm_poll_event(u64 arg0, u64 arg1) {
    struct clks_wm_event *out_event = (struct clks_wm_event *)(usize)arg1;

    if (arg0 == 0ULL || arg1 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(*out_event)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_poll_event(clks_syscall_wm_owner_pid(), arg0, out_event) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_move(u64 arg0) {
    struct clks_syscall_wm_move_req req;
    i32 x = 0;
    i32 y = 0;

    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.window_id == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_u64_to_i32(req.x, &x) == CLKS_FALSE || clks_syscall_u64_to_i32(req.y, &y) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_move(clks_syscall_wm_owner_pid(), req.window_id, x, y) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_set_focus(u64 arg0) {
    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_set_focus(clks_syscall_wm_owner_pid(), arg0) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_set_flags(u64 arg0, u64 arg1) {
    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_set_flags(clks_syscall_wm_owner_pid(), arg0, arg1) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_resize(u64 arg0) {
    struct clks_syscall_wm_resize_req req;

    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.window_id == 0ULL || req.width > 0xFFFFFFFFULL || req.height > 0xFFFFFFFFULL) {
        return 0ULL;
    }

    return (clks_wm_resize(clks_syscall_wm_owner_pid(), req.window_id, (u32)req.width, (u32)req.height) == CLKS_TRUE)
               ? 1ULL
               : 0ULL;
}

static u64 clks_syscall_fd_open(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_exec_fd_open(path, arg1, arg2);
}

static u64 clks_syscall_fd_read(u64 arg0, u64 arg1, u64 arg2) {
    if (arg2 > 0ULL && arg1 == 0ULL) {
        return (u64)-1;
    }

    if (arg2 > 0ULL && clks_syscall_user_ptr_writable(arg1, arg2) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_exec_fd_read(arg0, (void *)arg1, arg2);
}

static u64 clks_syscall_fd_write(u64 arg0, u64 arg1, u64 arg2) {
    if (arg2 > 0ULL && arg1 == 0ULL) {
        return (u64)-1;
    }

    if (arg2 > 0ULL && clks_syscall_user_ptr_readable(arg1, arg2) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_exec_fd_write(arg0, (const void *)arg1, arg2);
}

static u64 clks_syscall_fd_close(u64 arg0) {
    return clks_exec_fd_close(arg0);
}

static u64 clks_syscall_fd_dup(u64 arg0) {
    return clks_exec_fd_dup(arg0);
}

static u64 clks_syscall_dl_open(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_exec_dl_open(path);
}

static u64 clks_syscall_dl_close(u64 arg0) {
    return clks_exec_dl_close(arg0);
}

static u64 clks_syscall_dl_sym(u64 arg0, u64 arg1) {
    char symbol[CLKS_SYSCALL_NAME_MAX];

    if (clks_syscall_copy_user_string(arg1, symbol, sizeof(symbol)) == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_exec_dl_sym(arg0, symbol);
}

static clks_bool clks_syscall_procfs_is_root(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/proc") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_fs_is_root(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_fs_has_real_proc_dir(void) {
    struct clks_fs_node_info info;

    if (clks_fs_stat("/proc", &info) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return (info.type == CLKS_FS_NODE_DIR) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_procfs_is_self(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/proc/self") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_procfs_is_list(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/proc/list") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_parse_u64_dec(const char *text, u64 *out_value) {
    u64 value = 0ULL;
    usize i = 0U;

    if (text == CLKS_NULL || out_value == CLKS_NULL || text[0] == '\0') {
        return CLKS_FALSE;
    }

    while (text[i] != '\0') {
        u64 digit;

        if (text[i] < '0' || text[i] > '9') {
            return CLKS_FALSE;
        }

        digit = (u64)(text[i] - '0');

        if (value > ((0xFFFFFFFFFFFFFFFFULL - digit) / 10ULL)) {
            return CLKS_FALSE;
        }

        value = (value * 10ULL) + digit;
        i++;
    }

    *out_value = value;
    return CLKS_TRUE;
}

static clks_bool clks_syscall_procfs_parse_pid(const char *path, u64 *out_pid) {
    const char *part;
    usize i = 0U;
    char pid_text[32];
    u64 pid;

    if (path == CLKS_NULL || out_pid == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (path[0] != '/' || path[1] != 'p' || path[2] != 'r' || path[3] != 'o' || path[4] != 'c' || path[5] != '/') {
        return CLKS_FALSE;
    }

    part = &path[6];

    if (part[0] == '\0' || clks_strcmp(part, "self") == 0 || clks_strcmp(part, "list") == 0) {
        return CLKS_FALSE;
    }

    while (part[i] != '\0') {
        if (i + 1U >= sizeof(pid_text)) {
            return CLKS_FALSE;
        }

        if (part[i] < '0' || part[i] > '9') {
            return CLKS_FALSE;
        }

        pid_text[i] = part[i];
        i++;
    }

    pid_text[i] = '\0';

    if (clks_syscall_parse_u64_dec(pid_text, &pid) == CLKS_FALSE || pid == 0ULL) {
        return CLKS_FALSE;
    }

    *out_pid = pid;
    return CLKS_TRUE;
}

static const char *clks_syscall_proc_state_name(u64 state) {
    if (state == CLKS_EXEC_PROC_STATE_PENDING) {
        return "PENDING";
    }

    if (state == CLKS_EXEC_PROC_STATE_RUNNING) {
        return "RUNNING";
    }

    if (state == CLKS_EXEC_PROC_STATE_STOPPED) {
        return "STOPPED";
    }

    if (state == CLKS_EXEC_PROC_STATE_EXITED) {
        return "EXITED";
    }

    return "UNUSED";
}

static usize clks_syscall_procfs_append_char(char *out, usize out_size, usize pos, char ch) {
    if (out == CLKS_NULL || out_size == 0U) {
        return pos;
    }

    if (pos + 1U < out_size) {
        out[pos] = ch;
        out[pos + 1U] = '\0';
        return pos + 1U;
    }

    out[out_size - 1U] = '\0';
    return pos;
}

static usize clks_syscall_procfs_append_text(char *out, usize out_size, usize pos, const char *text) {
    usize i = 0U;

    if (text == CLKS_NULL) {
        return pos;
    }

    while (text[i] != '\0') {
        pos = clks_syscall_procfs_append_char(out, out_size, pos, text[i]);
        i++;
    }

    return pos;
}

static usize clks_syscall_procfs_append_u64_dec(char *out, usize out_size, usize pos, u64 value) {
    char temp[32];
    usize len = 0U;
    usize i;

    if (value == 0ULL) {
        return clks_syscall_procfs_append_char(out, out_size, pos, '0');
    }

    while (value != 0ULL && len + 1U < sizeof(temp)) {
        temp[len++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    }

    for (i = 0U; i < len; i++) {
        pos = clks_syscall_procfs_append_char(out, out_size, pos, temp[len - 1U - i]);
    }

    return pos;
}

static usize clks_syscall_procfs_append_u64_hex(char *out, usize out_size, usize pos, u64 value) {
    i32 nibble;

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "0X");

    for (nibble = 15; nibble >= 0; nibble--) {
        u64 current = (value >> (u64)(nibble * 4)) & 0x0FULL;
        char ch = (current < 10ULL) ? (char)('0' + current) : (char)('A' + (current - 10ULL));
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ch);
    }

    return pos;
}

static usize clks_syscall_procfs_append_n(char *out, usize out_size, usize pos, const char *text, usize text_len) {
    usize i = 0U;

    if (text == CLKS_NULL) {
        return pos;
    }

    while (i < text_len) {
        pos = clks_syscall_procfs_append_char(out, out_size, pos, text[i]);
        i++;
    }

    return pos;
}

static clks_bool clks_syscall_is_hex(char ch) {
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

static u8 clks_syscall_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return (u8)(ch - '0');
    }

    if (ch >= 'a' && ch <= 'f') {
        return (u8)(10 + (ch - 'a'));
    }

    return (u8)(10 + (ch - 'A'));
}

static clks_bool clks_syscall_parse_symbol_line(const char *line, usize len, u64 *out_addr, const char **out_name,
                                                usize *out_name_len, const char **out_source, usize *out_source_len) {
    usize i = 0U;
    u64 addr = 0ULL;
    u32 digits = 0U;
    usize name_start;
    usize name_end;
    usize source_start;
    usize source_end;

    if (line == CLKS_NULL || out_addr == CLKS_NULL || out_name == CLKS_NULL || out_name_len == CLKS_NULL ||
        out_source == CLKS_NULL || out_source_len == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (len == 0U) {
        return CLKS_FALSE;
    }

    if (len >= 2U && line[0] == '0' && (line[1] == 'X' || line[1] == 'x')) {
        i = 2U;
    }

    while (i < len && clks_syscall_is_hex(line[i]) == CLKS_TRUE) {
        addr = (addr << 4) | (u64)clks_syscall_hex_value(line[i]);
        digits++;
        i++;
    }

    if (digits == 0U) {
        return CLKS_FALSE;
    }

    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }

    if (i >= len) {
        return CLKS_FALSE;
    }

    name_start = i;

    while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
        i++;
    }

    name_end = i;

    if (name_end <= name_start) {
        return CLKS_FALSE;
    }

    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }

    source_start = i;
    source_end = len;

    while (source_end > source_start &&
           (line[source_end - 1U] == ' ' || line[source_end - 1U] == '\t' || line[source_end - 1U] == '\r')) {
        source_end--;
    }

    *out_addr = addr;
    *out_name = &line[name_start];
    *out_name_len = name_end - name_start;
    *out_source = (source_end > source_start) ? &line[source_start] : CLKS_NULL;
    *out_source_len = (source_end > source_start) ? (source_end - source_start) : 0U;
    return CLKS_TRUE;
}

static clks_bool clks_syscall_symbols_ready(void) {
    const void *data;
    u64 size = 0ULL;

    if (clks_syscall_symbols_checked == CLKS_TRUE) {
        return (clks_syscall_symbols_data != CLKS_NULL && clks_syscall_symbols_size > 0ULL) ? CLKS_TRUE : CLKS_FALSE;
    }

    clks_syscall_symbols_checked = CLKS_TRUE;

    if (clks_fs_is_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    data = clks_fs_read_all(CLKS_SYSCALL_KERNEL_SYMBOL_FILE, &size);

    if (data == CLKS_NULL || size == 0ULL) {
        return CLKS_FALSE;
    }

    clks_syscall_symbols_data = (const char *)data;
    clks_syscall_symbols_size = size;
    return CLKS_TRUE;
}

static clks_bool clks_syscall_lookup_symbol(u64 addr, const char **out_name, usize *out_name_len, u64 *out_base,
                                            const char **out_source, usize *out_source_len) {
    const char *data;
    const char *end;
    const char *line;
    const char *best_name = CLKS_NULL;
    const char *best_source = CLKS_NULL;
    usize best_name_len = 0U;
    usize best_source_len = 0U;
    u64 best_addr = 0ULL;
    clks_bool found = CLKS_FALSE;

    if (out_name == CLKS_NULL || out_name_len == CLKS_NULL || out_base == CLKS_NULL || out_source == CLKS_NULL ||
        out_source_len == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_name = CLKS_NULL;
    *out_name_len = 0U;
    *out_base = 0ULL;
    *out_source = CLKS_NULL;
    *out_source_len = 0U;

    if (clks_syscall_symbols_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    data = clks_syscall_symbols_data;
    end = clks_syscall_symbols_data + clks_syscall_symbols_size;

    while (data < end) {
        u64 line_addr;
        const char *line_name;
        usize line_name_len;
        const char *line_source;
        usize line_source_len;
        usize line_len = 0U;

        line = data;

        while (data < end && *data != '\n') {
            data++;
            line_len++;
        }

        if (data < end && *data == '\n') {
            data++;
        }

        if (clks_syscall_parse_symbol_line(line, line_len, &line_addr, &line_name, &line_name_len, &line_source,
                                           &line_source_len) == CLKS_FALSE) {
            continue;
        }

        if (line_addr <= addr && (found == CLKS_FALSE || line_addr >= best_addr)) {
            best_addr = line_addr;
            best_name = line_name;
            best_name_len = line_name_len;
            best_source = line_source;
            best_source_len = line_source_len;
            found = CLKS_TRUE;
        }
    }

    if (found == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_name = best_name;
    *out_name_len = best_name_len;
    *out_base = best_addr;
    *out_source = best_source;
    *out_source_len = best_source_len;
    return CLKS_TRUE;
}

static usize clks_syscall_kdbg_format_symbol_into(char *out, usize out_size, usize pos, u64 addr) {
    const char *sym_name = CLKS_NULL;
    const char *sym_source = CLKS_NULL;
    usize sym_name_len = 0U;
    usize sym_source_len = 0U;
    u64 sym_base = 0ULL;
    clks_bool has_symbol;

    pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, addr);
    has_symbol = clks_syscall_lookup_symbol(addr, &sym_name, &sym_name_len, &sym_base, &sym_source, &sym_source_len);

    if (has_symbol == CLKS_TRUE) {
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_n(out, out_size, pos, sym_name, sym_name_len);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, '+');
        pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, addr - sym_base);

        if (sym_source != CLKS_NULL && sym_source_len > 0U) {
            pos = clks_syscall_procfs_append_text(out, out_size, pos, " @ ");
            pos = clks_syscall_procfs_append_n(out, out_size, pos, sym_source, sym_source_len);
        }
    } else {
        pos = clks_syscall_procfs_append_text(out, out_size, pos, " <no-symbol>");
    }

    return pos;
}

static usize clks_syscall_kdbg_append_bt_frame(char *out, usize out_size, usize pos, u64 index, u64 rip) {
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '#');
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, index);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
    pos = clks_syscall_kdbg_format_symbol_into(out, out_size, pos, rip);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');
    return pos;
}

static clks_bool clks_syscall_kdbg_stack_ptr_valid(u64 ptr, u64 stack_low, u64 stack_high) {
    if ((ptr & 0x7ULL) != 0ULL) {
        return CLKS_FALSE;
    }

    if (ptr < stack_low || ptr + 16ULL > stack_high) {
        return CLKS_FALSE;
    }

    if (ptr < CLKS_SYSCALL_KERNEL_ADDR_BASE) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static u64 clks_syscall_kdbg_sym(u64 arg0, u64 arg1, u64 arg2) {
    char text[CLKS_SYSCALL_KDBG_TEXT_MAX];
    usize len;

    if (arg1 == 0ULL || arg2 == 0ULL) {
        return 0ULL;
    }

    text[0] = '\0';
    len = clks_syscall_kdbg_format_symbol_into(text, sizeof(text), 0U, arg0);
    return clks_syscall_copy_text_to_user(arg1, arg2, text, len);
}

static u64 clks_syscall_kdbg_regs(u64 arg0, u64 arg1) {
    char text[CLKS_SYSCALL_KDBG_TEXT_MAX];
    usize pos = 0U;
    const struct clks_syscall_frame *frame = &clks_syscall_last_frame;

    if (arg0 == 0ULL || arg1 == 0ULL) {
        return 0ULL;
    }

    text[0] = '\0';

    if (clks_syscall_last_frame_valid == CLKS_FALSE) {
        pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "NO REG SNAPSHOT\n");
        return clks_syscall_copy_text_to_user(arg0, arg1, text, pos);
    }

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "RAX=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rax);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RBX=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rbx);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RCX=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rcx);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RDX=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rdx);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "RSI=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rsi);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RDI=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rdi);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RBP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rbp);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RSP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rsp);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "R8 =");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r8);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R9 =");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r9);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R10=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r10);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R11=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r11);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "R12=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r12);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R13=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r13);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R14=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r14);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R15=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r15);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "RIP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rip);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " CS=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->cs);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RFLAGS=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rflags);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "VECTOR=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->vector);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " ERROR=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->error_code);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " SS=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->ss);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    return clks_syscall_copy_text_to_user(arg0, arg1, text, pos);
}

static u64 clks_syscall_kdbg_bt(u64 arg0) {
    struct clks_syscall_kdbg_bt_req req;
    char text[CLKS_SYSCALL_KDBG_TEXT_MAX];
    usize pos = 0U;
    u64 frame_index = 0ULL;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));

    if (req.out_ptr == 0ULL || req.out_size == 0ULL) {
        return 0ULL;
    }

    text[0] = '\0';
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "BT RBP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, req.rbp);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RIP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, req.rip);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    if (req.rip != 0ULL) {
        pos = clks_syscall_kdbg_append_bt_frame(text, sizeof(text), pos, frame_index, req.rip);
        frame_index++;
    }

#if defined(CLKS_ARCH_X86_64)
    {
        u64 current_rbp = req.rbp;
        u64 current_rsp = 0ULL;
        u64 stack_low;
        u64 stack_high;

        __asm__ volatile("mov %%rsp, %0" : "=r"(current_rsp));

        stack_low = (current_rsp > CLKS_SYSCALL_KDBG_STACK_WINDOW_BYTES)
                        ? (current_rsp - CLKS_SYSCALL_KDBG_STACK_WINDOW_BYTES)
                        : CLKS_SYSCALL_KERNEL_ADDR_BASE;
        stack_high = current_rsp + CLKS_SYSCALL_KDBG_STACK_WINDOW_BYTES;

        if (stack_high < current_rsp) {
            stack_high = 0xFFFFFFFFFFFFFFFFULL;
        }

        if (stack_low < CLKS_SYSCALL_KERNEL_ADDR_BASE) {
            stack_low = CLKS_SYSCALL_KERNEL_ADDR_BASE;
        }

        if (clks_syscall_kdbg_stack_ptr_valid(current_rbp, stack_low, stack_high) == CLKS_TRUE) {
            while (frame_index < CLKS_SYSCALL_KDBG_BT_MAX_FRAMES) {
                const u64 *frame_ptr;
                u64 next_rbp;
                u64 ret_rip;

                frame_ptr = (const u64 *)(usize)current_rbp;
                next_rbp = frame_ptr[0];
                ret_rip = frame_ptr[1];

                if (ret_rip == 0ULL) {
                    break;
                }

                pos = clks_syscall_kdbg_append_bt_frame(text, sizeof(text), pos, frame_index, ret_rip);
                frame_index++;

                if (next_rbp <= current_rbp) {
                    break;
                }

                if (clks_syscall_kdbg_stack_ptr_valid(next_rbp, stack_low, stack_high) == CLKS_FALSE) {
                    break;
                }

                current_rbp = next_rbp;
            }
        } else {
            pos = clks_syscall_procfs_append_text(
                text, sizeof(text), pos, "NOTE: stack walk skipped (rbp not in current kernel stack window)\n");
        }
    }
#else
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "NOTE: stack walk unsupported on this arch\n");
#endif

    return clks_syscall_copy_text_to_user(req.out_ptr, req.out_size, text, pos);
}

static clks_bool clks_syscall_procfs_snapshot_for_path(const char *path, struct clks_exec_proc_snapshot *out_snap) {
    u64 pid;

    if (path == CLKS_NULL || out_snap == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_syscall_procfs_is_self(path) == CLKS_TRUE) {
        pid = clks_exec_current_pid();

        if (pid == 0ULL) {
            return CLKS_FALSE;
        }

        return clks_exec_proc_snapshot(pid, out_snap);
    }

    if (clks_syscall_procfs_parse_pid(path, &pid) == CLKS_TRUE) {
        return clks_exec_proc_snapshot(pid, out_snap);
    }

    return CLKS_FALSE;
}

static usize clks_syscall_procfs_render_snapshot(char *out, usize out_size,
                                                 const struct clks_exec_proc_snapshot *snap) {
    usize pos = 0U;

    if (out == CLKS_NULL || out_size == 0U || snap == CLKS_NULL) {
        return 0U;
    }

    out[0] = '\0';

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "pid=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->pid);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "ppid=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->ppid);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "state=");
    pos = clks_syscall_procfs_append_text(out, out_size, pos, clks_syscall_proc_state_name(snap->state));
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "state_id=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->state);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "tty=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->tty_index);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "runtime_ticks=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->runtime_ticks);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "mem_bytes=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->mem_bytes);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "exit_status=");
    pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, snap->exit_status);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "last_signal=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->last_signal);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "last_fault_vector=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->last_fault_vector);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "last_fault_error=");
    pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, snap->last_fault_error);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "last_fault_rip=");
    pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, snap->last_fault_rip);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "path=");
    pos = clks_syscall_procfs_append_text(out, out_size, pos, snap->path);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    return pos;
}

static usize clks_syscall_procfs_render_list(char *out, usize out_size) {
    usize pos = 0U;
    u64 proc_count = clks_exec_proc_count();
    u64 i;

    if (out == CLKS_NULL || out_size == 0U) {
        return 0U;
    }

    out[0] = '\0';
    pos = clks_syscall_procfs_append_text(out, out_size, pos, "pid state tty runtime mem path\n");

    for (i = 0ULL; i < proc_count; i++) {
        u64 pid = 0ULL;
        struct clks_exec_proc_snapshot snap;

        if (clks_exec_proc_pid_at(i, &pid) == CLKS_FALSE || pid == 0ULL) {
            continue;
        }

        if (clks_exec_proc_snapshot(pid, &snap) == CLKS_FALSE) {
            continue;
        }

        pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap.pid);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_text(out, out_size, pos, clks_syscall_proc_state_name(snap.state));
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap.tty_index);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap.runtime_ticks);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap.mem_bytes);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_text(out, out_size, pos, snap.path);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');
    }

    return pos;
}

static clks_bool clks_syscall_procfs_render_file(const char *path, char *out, usize out_size, usize *out_len) {
    struct clks_exec_proc_snapshot snap;

    if (out_len != CLKS_NULL) {
        *out_len = 0U;
    }

    if (path == CLKS_NULL || out == CLKS_NULL || out_size == 0U || out_len == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_syscall_procfs_is_list(path) == CLKS_TRUE) {
        *out_len = clks_syscall_procfs_render_list(out, out_size);
        return CLKS_TRUE;
    }

    if (clks_syscall_procfs_snapshot_for_path(path, &snap) == CLKS_TRUE) {
        *out_len = clks_syscall_procfs_render_snapshot(out, out_size, &snap);
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

static u64 clks_syscall_fs_child_count(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];
    u64 base_count;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_is_root(path) == CLKS_TRUE) {
        return 2ULL + clks_exec_proc_count();
    }

    base_count = clks_fs_count_children(path);

    if (base_count == (u64)-1) {
        return (u64)-1;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_fs_is_root(path) == CLKS_TRUE &&
        clks_syscall_fs_has_real_proc_dir() == CLKS_FALSE) {
        return base_count + 1ULL;
    }

    return base_count;
}

static u64 clks_syscall_fs_get_child_name(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (arg2 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg2, CLKS_SYSCALL_NAME_MAX) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_is_root(path) == CLKS_TRUE) {
        if (arg1 == 0ULL) {
            clks_memset((void *)arg2, 0, CLKS_SYSCALL_NAME_MAX);
            clks_memcpy((void *)arg2, "self", 5U);
            return 1ULL;
        }

        if (arg1 == 1ULL) {
            clks_memset((void *)arg2, 0, CLKS_SYSCALL_NAME_MAX);
            clks_memcpy((void *)arg2, "list", 5U);
            return 1ULL;
        }

        {
            u64 pid = 0ULL;
            char pid_text[32];
            usize len;

            if (clks_exec_proc_pid_at(arg1 - 2ULL, &pid) == CLKS_FALSE || pid == 0ULL) {
                return 0ULL;
            }

            clks_memset(pid_text, 0, sizeof(pid_text));
            len = clks_syscall_procfs_append_u64_dec(pid_text, sizeof(pid_text), 0U, pid);

            if (len + 1U > CLKS_SYSCALL_NAME_MAX) {
                return 0ULL;
            }

            clks_memset((void *)arg2, 0, CLKS_SYSCALL_NAME_MAX);
            clks_memcpy((void *)arg2, pid_text, len + 1U);
            return 1ULL;
        }
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_fs_is_root(path) == CLKS_TRUE &&
        clks_syscall_fs_has_real_proc_dir() == CLKS_FALSE) {
        if (arg1 == 0ULL) {
            clks_memset((void *)arg2, 0, CLKS_SYSCALL_NAME_MAX);
            clks_memcpy((void *)arg2, "proc", 5U);
            return 1ULL;
        }

        if (clks_fs_get_child_name(path, arg1 - 1ULL, (char *)arg2, (usize)CLKS_SYSCALL_NAME_MAX) == CLKS_FALSE) {
            return 0ULL;
        }

        return 1ULL;
    }

    if (clks_fs_get_child_name(path, arg1, (char *)arg2, (usize)CLKS_SYSCALL_NAME_MAX) == CLKS_FALSE) {
        return 0ULL;
    }

    return 1ULL;
}

static u64 clks_syscall_fs_read(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];
    const void *data;
    u64 file_size = 0ULL;
    u64 copy_len;

    if (arg1 == 0ULL || arg2 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, arg2) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (CLKS_CFG_PROCFS != 0 &&
        (clks_syscall_procfs_is_list(path) == CLKS_TRUE || clks_syscall_procfs_is_self(path) == CLKS_TRUE ||
         clks_syscall_procfs_parse_pid(path, &file_size) == CLKS_TRUE)) {
        char proc_text[CLKS_SYSCALL_PROCFS_TEXT_MAX];
        usize proc_len = 0U;

        if (clks_syscall_procfs_render_file(path, proc_text, sizeof(proc_text), &proc_len) == CLKS_FALSE) {
            return 0ULL;
        }

        copy_len = ((u64)proc_len < arg2) ? (u64)proc_len : arg2;

        if (copy_len == 0ULL) {
            return 0ULL;
        }

        clks_memcpy((void *)arg1, proc_text, (usize)copy_len);
        return copy_len;
    }

    data = clks_fs_read_all(path, &file_size);

    if (data == CLKS_NULL || file_size == 0ULL) {
        return 0ULL;
    }

    copy_len = (file_size < arg2) ? file_size : arg2;
    clks_memcpy((void *)arg1, data, (usize)copy_len);
    return copy_len;
}

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

static u64 clks_syscall_sleep_ticks(u64 arg0) {
    u64 before = clks_interrupts_timer_ticks();
    u64 slept;

    if (clks_wm_ready() == CLKS_TRUE) {
        clks_wm_tick(before);
    }

    slept = clks_exec_sleep_ticks(arg0);

    if (clks_wm_ready() == CLKS_TRUE) {
        clks_wm_tick(clks_interrupts_timer_ticks());
    }

    return slept;
}

static u64 clks_syscall_yield(void) {
    u64 tick;

    if (clks_wm_ready() == CLKS_TRUE) {
        clks_wm_tick(clks_interrupts_timer_ticks());
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

static u64 clks_syscall_audio_available(void) {
    return (clks_audio_available() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_audio_play_tone(u64 arg0, u64 arg1) {
    if (clks_audio_play_tone(arg0, arg1) == CLKS_FALSE) {
        return 0ULL;
    }

    return 1ULL;
}

static u64 clks_syscall_audio_stop(void) {
    clks_audio_stop();
    return 1ULL;
}
static u64 clks_syscall_fs_stat_type(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];
    struct clks_fs_node_info info;
    struct clks_exec_proc_snapshot snap;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_is_root(path) == CLKS_TRUE) {
        return (u64)CLKS_FS_NODE_DIR;
    }

    if (CLKS_CFG_PROCFS != 0 &&
        (clks_syscall_procfs_is_list(path) == CLKS_TRUE || clks_syscall_procfs_is_self(path) == CLKS_TRUE)) {
        return (u64)CLKS_FS_NODE_FILE;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_snapshot_for_path(path, &snap) == CLKS_TRUE) {
        return (u64)CLKS_FS_NODE_FILE;
    }

    if (clks_fs_stat(path, &info) == CLKS_FALSE) {
        return (u64)-1;
    }

    return (u64)info.type;
}

static u64 clks_syscall_fs_stat_size(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];
    struct clks_fs_node_info info;
    char proc_text[CLKS_SYSCALL_PROCFS_TEXT_MAX];
    usize proc_len = 0U;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_is_root(path) == CLKS_TRUE) {
        return 0ULL;
    }

    if (CLKS_CFG_PROCFS != 0 &&
        clks_syscall_procfs_render_file(path, proc_text, sizeof(proc_text), &proc_len) == CLKS_TRUE) {
        return (u64)proc_len;
    }

    if (clks_fs_stat(path, &info) == CLKS_FALSE) {
        return (u64)-1;
    }

    return info.size;
}

static u64 clks_syscall_fs_mkdir(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_fs_mkdir(path) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_fs_write_common(u64 arg0, u64 arg1, u64 arg2, clks_bool append_mode) {
    char path[CLKS_SYSCALL_PATH_MAX];
    const u8 *src = (const u8 *)arg1;
    u64 remaining = arg2;
    clks_bool first_chunk = CLKS_TRUE;
    clks_bool ok;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (arg2 == 0ULL) {
        if (append_mode == CLKS_TRUE) {
            ok = clks_fs_append(path, CLKS_NULL, 0ULL);
        } else {
            ok = clks_fs_write_all(path, CLKS_NULL, 0ULL);
        }

        return (ok == CLKS_TRUE) ? 1ULL : 0ULL;
    }

    if (arg1 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg1, arg2) == CLKS_FALSE) {
        return 0ULL;
    }

    while (remaining > 0ULL) {
        u64 chunk_len = remaining;
        void *heap_copy;

        if (chunk_len > CLKS_SYSCALL_FS_IO_CHUNK_LEN) {
            chunk_len = CLKS_SYSCALL_FS_IO_CHUNK_LEN;
        }

        heap_copy = clks_kmalloc((usize)chunk_len);
        if (heap_copy == CLKS_NULL) {
            return 0ULL;
        }

        clks_memcpy(heap_copy, (const void *)src, (usize)chunk_len);

        if (append_mode == CLKS_TRUE || first_chunk == CLKS_FALSE) {
            ok = clks_fs_append(path, heap_copy, chunk_len);
        } else {
            ok = clks_fs_write_all(path, heap_copy, chunk_len);
        }

        clks_kfree(heap_copy);

        if (ok == CLKS_FALSE) {
            return 0ULL;
        }

        src += chunk_len;
        remaining -= chunk_len;
        first_chunk = CLKS_FALSE;
    }

    return 1ULL;
}

static u64 clks_syscall_fs_write(u64 arg0, u64 arg1, u64 arg2) {
    return clks_syscall_fs_write_common(arg0, arg1, arg2, CLKS_FALSE);
}

static u64 clks_syscall_fs_append(u64 arg0, u64 arg1, u64 arg2) {
    return clks_syscall_fs_write_common(arg0, arg1, arg2, CLKS_TRUE);
}

static u64 clks_syscall_fs_remove(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_fs_remove(path) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_log_journal_count(void) {
    return clks_log_journal_count();
}

static u64 clks_syscall_log_journal_read(u64 arg0, u64 arg1, u64 arg2) {
    char line[CLKS_SYSCALL_JOURNAL_MAX_LEN];
    usize line_len;
    usize copy_len;

    if (arg1 == 0ULL || arg2 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, arg2) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_log_journal_read(arg0, line, sizeof(line)) == CLKS_FALSE) {
        return 0ULL;
    }

    line_len = clks_strlen(line) + 1U;
    copy_len = line_len;

    if (copy_len > (usize)arg2) {
        copy_len = (usize)arg2;
    }

    if (copy_len > sizeof(line)) {
        copy_len = sizeof(line);
    }

    clks_memcpy((void *)arg1, line, copy_len);
    ((char *)arg1)[copy_len - 1U] = '\0';
    return 1ULL;
}

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

#if CLKS_CFG_USC != 0
static void clks_syscall_usc_sleep_until_input(void) {
#if defined(CLKS_ARCH_X86_64)
    u64 flags = 0ULL;

    __asm__ volatile("pushfq; popq %0" : "=r"(flags) : : "memory");

    if ((flags & (1ULL << 9)) != 0ULL) {
        __asm__ volatile("hlt" : : : "memory");
    } else {
        __asm__ volatile("sti; hlt; cli" : : : "memory");
    }
#elif defined(CLKS_ARCH_AARCH64)
    clks_cpu_pause();
#endif
}

static const char *clks_syscall_usc_syscall_name(u64 id) {
    return clks_syscall_name(id);
}

static clks_bool clks_syscall_usc_is_dangerous(u64 id) {
    switch (id) {
    case CLKS_SYSCALL_FS_MKDIR:
        return (CLKS_CFG_USC_SC_FS_MKDIR != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_FS_WRITE:
        return (CLKS_CFG_USC_SC_FS_WRITE != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_FS_APPEND:
        return (CLKS_CFG_USC_SC_FS_APPEND != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_FS_REMOVE:
        return (CLKS_CFG_USC_SC_FS_REMOVE != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_EXEC_PATH:
        return (CLKS_CFG_USC_SC_EXEC_PATH != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_EXEC_PATHV:
        return (CLKS_CFG_USC_SC_EXEC_PATHV != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_EXEC_PATHV_IO:
        return (CLKS_CFG_USC_SC_EXEC_PATHV_IO != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_SPAWN_PATH:
        return (CLKS_CFG_USC_SC_SPAWN_PATH != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_SPAWN_PATHV:
        return (CLKS_CFG_USC_SC_SPAWN_PATHV != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_PROC_KILL:
        return (CLKS_CFG_USC_SC_PROC_KILL != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_SHUTDOWN:
        return (CLKS_CFG_USC_SC_SHUTDOWN != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_RESTART:
        return (CLKS_CFG_USC_SC_RESTART != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_SYSCALL_DISK_FORMAT_FAT32:
        return CLKS_TRUE;
    case CLKS_SYSCALL_DISK_WRITE_SECTOR:
        return CLKS_TRUE;
    default:
        return CLKS_FALSE;
    }
}

static void clks_syscall_usc_copy_path(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || dst_size == 0U) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1U < dst_size) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static clks_bool clks_syscall_usc_current_app_path(char *out_path, usize out_size) {
    u64 pid;
    struct clks_exec_proc_snapshot snap;

    if (out_path == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    out_path[0] = '\0';
    pid = clks_exec_current_pid();

    if (pid == 0ULL) {
        return CLKS_FALSE;
    }

    if (clks_exec_proc_snapshot(pid, &snap) == CLKS_FALSE || snap.path[0] == '\0') {
        return CLKS_FALSE;
    }

    clks_syscall_usc_copy_path(out_path, out_size, snap.path);
    return CLKS_TRUE;
}

static i32
clks_syscall_usc_find_path_in_table(const clks_bool used[CLKS_SYSCALL_USC_MAX_ALLOWED_APPS],
                                    const char table[CLKS_SYSCALL_USC_MAX_ALLOWED_APPS][CLKS_EXEC_PROC_PATH_MAX],
                                    const char *path) {
    u32 i;

    if (path == CLKS_NULL || path[0] == '\0') {
        return -1;
    }

    for (i = 0U; i < CLKS_SYSCALL_USC_MAX_ALLOWED_APPS; i++) {
        if (used[i] == CLKS_TRUE && clks_strcmp(table[i], path) == 0) {
            return (i32)i;
        }
    }

    return -1;
}

static clks_bool
clks_syscall_usc_add_path_to_table(clks_bool used[CLKS_SYSCALL_USC_MAX_ALLOWED_APPS],
                                   char table[CLKS_SYSCALL_USC_MAX_ALLOWED_APPS][CLKS_EXEC_PROC_PATH_MAX],
                                   const char *path) {
    u32 i;

    if (path == CLKS_NULL || path[0] == '\0') {
        return CLKS_FALSE;
    }

    if (clks_syscall_usc_find_path_in_table(used, table, path) >= 0) {
        return CLKS_TRUE;
    }

    for (i = 0U; i < CLKS_SYSCALL_USC_MAX_ALLOWED_APPS; i++) {
        if (used[i] == CLKS_FALSE) {
            used[i] = CLKS_TRUE;
            clks_syscall_usc_copy_path(table[i], CLKS_EXEC_PROC_PATH_MAX, path);
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static i32 clks_syscall_usc_find_session_allowed_path(const char *path) {
    return clks_syscall_usc_find_path_in_table(clks_syscall_usc_session_allowed_used,
                                               clks_syscall_usc_session_allowed_path, path);
}

static clks_bool clks_syscall_usc_remember_session_path(const char *path) {
    return clks_syscall_usc_add_path_to_table(clks_syscall_usc_session_allowed_used,
                                              clks_syscall_usc_session_allowed_path, path);
}

static i32 clks_syscall_usc_find_permanent_allowed_path(const char *path) {
    return clks_syscall_usc_find_path_in_table(clks_syscall_usc_permanent_allowed_used,
                                               clks_syscall_usc_permanent_allowed_path, path);
}

static clks_bool clks_syscall_usc_remember_permanent_in_memory(const char *path) {
    return clks_syscall_usc_add_path_to_table(clks_syscall_usc_permanent_allowed_used,
                                              clks_syscall_usc_permanent_allowed_path, path);
}

static clks_bool clks_syscall_usc_build_perm_rule_path(char *out_path, usize out_size) {
    const char *mount_path;
    const char *file_name = CLKS_SYSCALL_USC_PERM_RULE_FILE;
    usize mount_len;
    usize file_len;
    usize pos = 0U;

    if (out_path == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    out_path[0] = '\0';

    if (clks_disk_is_mounted() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    mount_path = clks_disk_mount_path();
    if (mount_path == CLKS_NULL || mount_path[0] == '\0') {
        return CLKS_FALSE;
    }

    mount_len = clks_strlen(mount_path);
    file_len = clks_strlen(file_name);

    if (mount_len >= out_size) {
        return CLKS_FALSE;
    }

    clks_memcpy(out_path, mount_path, mount_len);
    pos = mount_len;

    if (pos > 0U && out_path[pos - 1U] != '/') {
        if (pos + 1U >= out_size) {
            return CLKS_FALSE;
        }
        out_path[pos] = '/';
        pos++;
    }

    if (pos + file_len + 1U > out_size) {
        return CLKS_FALSE;
    }

    clks_memcpy(out_path + pos, file_name, file_len);
    pos += file_len;
    out_path[pos] = '\0';
    return CLKS_TRUE;
}

static void clks_syscall_usc_load_permanent_rules_if_needed(void) {
    char rules_path[CLKS_DISK_PATH_MAX];
    const u8 *data;
    u64 data_size = 0ULL;
    u64 pos = 0ULL;

    if (clks_syscall_usc_permanent_loaded == CLKS_TRUE) {
        return;
    }

    if (clks_syscall_usc_build_perm_rule_path(rules_path, sizeof(rules_path)) == CLKS_FALSE) {
        return;
    }

    clks_syscall_usc_permanent_loaded = CLKS_TRUE;
    data = (const u8 *)clks_disk_read_all(rules_path, &data_size);

    if (data == CLKS_NULL || data_size == 0ULL) {
        return;
    }

    while (pos < data_size) {
        u64 start = pos;
        u64 end;
        usize line_len;
        char line[CLKS_EXEC_PROC_PATH_MAX];

        while (pos < data_size && data[pos] != (u8)'\n' && data[pos] != (u8)'\r') {
            pos++;
        }

        end = pos;
        while (pos < data_size && (data[pos] == (u8)'\n' || data[pos] == (u8)'\r')) {
            pos++;
        }

        if (end <= start) {
            continue;
        }

        line_len = (usize)(end - start);
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1U;
        }

        clks_memcpy(line, data + start, line_len);
        line[line_len] = '\0';
        (void)clks_syscall_usc_remember_permanent_in_memory(line);
    }
}

static clks_bool clks_syscall_usc_append_permanent_rule(const char *path) {
    char rules_path[CLKS_DISK_PATH_MAX];
    char line[CLKS_EXEC_PROC_PATH_MAX + 2U];
    usize len;

    if (path == CLKS_NULL || path[0] == '\0') {
        return CLKS_FALSE;
    }

    if (clks_syscall_usc_build_perm_rule_path(rules_path, sizeof(rules_path)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    len = clks_strlen(path);
    if (len == 0U) {
        return CLKS_FALSE;
    }

    if (len + 1U >= sizeof(line)) {
        return CLKS_FALSE;
    }

    clks_memcpy(line, path, len);
    line[len] = '\n';
    line[len + 1U] = '\0';
    return clks_disk_append(rules_path, line, (u64)(len + 1U));
}

static clks_bool clks_syscall_usc_remember_permanent_path(const char *path) {
    clks_bool added;

    if (path == CLKS_NULL || path[0] == '\0') {
        return CLKS_FALSE;
    }

    clks_syscall_usc_load_permanent_rules_if_needed();

    if (clks_syscall_usc_find_permanent_allowed_path(path) >= 0) {
        return CLKS_TRUE;
    }

    if (clks_syscall_usc_append_permanent_rule(path) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    added = clks_syscall_usc_remember_permanent_in_memory(path);
    if (added == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static void clks_syscall_usc_emit_text_line(const char *label, const char *value) {
    char message[320];
    usize pos = 0U;

    message[0] = '\0';
    pos = clks_syscall_procfs_append_text(message, sizeof(message), pos, label);
    pos = clks_syscall_procfs_append_text(message, sizeof(message), pos, ": ");
    pos = clks_syscall_procfs_append_text(message, sizeof(message), pos, value);
    (void)pos;
    clks_log(CLKS_LOG_WARN, "USC", message);
}

static void clks_syscall_usc_emit_hex_line(const char *label, u64 value) {
    clks_log_hex(CLKS_LOG_WARN, "USC", label, value);
}

#if defined(CLKS_CFG_KEYBOARD) && (CLKS_CFG_KEYBOARD != 0)
#define CLKS_SYSCALL_USC_POPUP_WIDTH 520U
#define CLKS_SYSCALL_USC_POPUP_HEIGHT 228U
#define CLKS_SYSCALL_USC_POPUP_OWNER 0ULL
#define CLKS_SYSCALL_USC_POPUP_TEXT_SCALE 1U

struct clks_syscall_usc_popup_button {
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    enum clks_syscall_usc_decision decision;
};

const u8 *clks_font8x8_get(char ch);

static void clks_syscall_usc_popup_fill_rect(u32 *pixels, u32 surface_width, u32 surface_height, u32 x, u32 y,
                                             u32 width, u32 height, u32 color) {
    u32 row;

    if (pixels == CLKS_NULL || surface_width == 0U || surface_height == 0U || width == 0U || height == 0U) {
        return;
    }

    if (x >= surface_width || y >= surface_height) {
        return;
    }

    if (width > surface_width - x) {
        width = surface_width - x;
    }
    if (height > surface_height - y) {
        height = surface_height - y;
    }

    for (row = 0U; row < height; row++) {
        u32 *dst = pixels + ((u64)(y + row) * (u64)surface_width) + (u64)x;
        u32 col;

        for (col = 0U; col < width; col++) {
            dst[col] = color;
        }
    }
}

static void clks_syscall_usc_popup_draw_char(u32 *pixels, u32 surface_width, u32 surface_height, u32 x, u32 y, char ch,
                                             u32 color, u32 scale) {
    const u8 *glyph;
    u32 gy;

    if (pixels == CLKS_NULL || scale == 0U) {
        return;
    }

    glyph = clks_font8x8_get(ch);
    if (glyph == CLKS_NULL) {
        return;
    }

    for (gy = 0U; gy < 8U; gy++) {
        u32 gx;
        u8 bits = glyph[gy];

        for (gx = 0U; gx < 8U; gx++) {
            if ((bits & (u8)(0x80U >> gx)) != 0U) {
                clks_syscall_usc_popup_fill_rect(pixels, surface_width, surface_height, x + (gx * scale),
                                                 y + (gy * scale), scale, scale, color);
            }
        }
    }
}

static u32 clks_syscall_usc_popup_text_width(const char *text, u32 scale) {
    if (text == CLKS_NULL || scale == 0U) {
        return 0U;
    }

    return (u32)clks_strlen(text) * 8U * scale;
}

static void clks_syscall_usc_popup_draw_text(u32 *pixels, u32 surface_width, u32 surface_height, u32 x, u32 y,
                                             u32 max_x, const char *text, u32 color, u32 scale) {
    u32 cursor_x = x;
    u32 char_width;
    usize i = 0U;

    if (pixels == CLKS_NULL || text == CLKS_NULL || scale == 0U || x >= max_x) {
        return;
    }

    char_width = 8U * scale;
    while (text[i] != '\0' && cursor_x + char_width <= max_x) {
        if (text[i] != ' ') {
            clks_syscall_usc_popup_draw_char(pixels, surface_width, surface_height, cursor_x, y, text[i], color, scale);
        }
        cursor_x += char_width;
        i++;
    }
}

static void clks_syscall_usc_popup_draw_border(u32 *pixels, u32 width, u32 height, u32 x, u32 y, u32 rect_width,
                                               u32 rect_height, u32 color) {
    if (rect_width == 0U || rect_height == 0U) {
        return;
    }

    clks_syscall_usc_popup_fill_rect(pixels, width, height, x, y, rect_width, 1U, color);
    clks_syscall_usc_popup_fill_rect(pixels, width, height, x, y + rect_height - 1U, rect_width, 1U, color);
    clks_syscall_usc_popup_fill_rect(pixels, width, height, x, y, 1U, rect_height, color);
    clks_syscall_usc_popup_fill_rect(pixels, width, height, x + rect_width - 1U, y, 1U, rect_height, color);
}

static void clks_syscall_usc_popup_draw_button(u32 *pixels, u32 width, u32 height,
                                               const struct clks_syscall_usc_popup_button *button, const char *label,
                                               u32 fill, u32 border, u32 text) {
    u32 label_width;
    u32 label_x;
    u32 label_y;

    if (button == CLKS_NULL || label == CLKS_NULL) {
        return;
    }

    clks_syscall_usc_popup_fill_rect(pixels, width, height, button->x, button->y, button->width, button->height, fill);
    clks_syscall_usc_popup_draw_border(pixels, width, height, button->x, button->y, button->width, button->height,
                                       border);

    label_width = clks_syscall_usc_popup_text_width(label, CLKS_SYSCALL_USC_POPUP_TEXT_SCALE);
    label_x = button->x + ((button->width > label_width) ? ((button->width - label_width) / 2U) : 4U);
    label_y = button->y + ((button->height > 8U) ? ((button->height - 8U) / 2U) : 0U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, label_x, label_y, button->x + button->width - 4U, label,
                                     text, CLKS_SYSCALL_USC_POPUP_TEXT_SCALE);
}

static void clks_syscall_usc_popup_build_syscall_line(char *out, usize out_size, const char *name, u64 id) {
    usize pos = 0U;

    if (out == CLKS_NULL || out_size == 0U) {
        return;
    }

    out[0] = '\0';
    pos = clks_syscall_procfs_append_text(out, out_size, pos, "SYSCALL ");
    pos = clks_syscall_procfs_append_text(out, out_size, pos, name);
    pos = clks_syscall_procfs_append_text(out, out_size, pos, " ID ");
    (void)clks_syscall_procfs_append_u64_dec(out, out_size, pos, id);
}

static void clks_syscall_usc_popup_draw(u32 *pixels, u32 width, u32 height, const char *app_path, const char *name,
                                        u64 id) {
    static const struct clks_syscall_usc_popup_button buttons[4] = {
        {20U, 176U, 112U, 34U, CLKS_SYSCALL_USC_DECISION_ALLOW_ONCE},
        {140U, 176U, 112U, 34U, CLKS_SYSCALL_USC_DECISION_ALLOW_SESSION},
        {260U, 176U, 112U, 34U, CLKS_SYSCALL_USC_DECISION_ALLOW_PERMANENT},
        {380U, 176U, 112U, 34U, CLKS_SYSCALL_USC_DECISION_DENY},
    };
    char syscall_line[128];

    clks_syscall_usc_popup_build_syscall_line(syscall_line, sizeof(syscall_line), name, id);

    clks_syscall_usc_popup_fill_rect(pixels, width, height, 0U, 0U, width, height, 0x00F3F3F3UL);
    clks_syscall_usc_popup_fill_rect(pixels, width, height, 0U, 0U, width, 34U, 0x000078D7UL);
    clks_syscall_usc_popup_draw_border(pixels, width, height, 0U, 0U, width, height, 0x00005A9EUL);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 16U, 10U, width - 16U, "USERSAFECONTROLLER", 0x00FFFFFFUL,
                                     1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 18U, 50U, width - 18U,
                                     "A USER APP REQUESTS A DANGEROUS ACTION", 0x00202020UL, 1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 18U, 76U, width - 18U, "APP:", 0x00404040UL, 1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 58U, 76U, width - 18U, app_path, 0x00000000UL, 1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 18U, 100U, width - 18U, syscall_line, 0x00000000UL, 1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 18U, 128U, width - 18U,
                                     "CHOOSE: 1 ONCE  2 SESSION  3 PERMANENT  N DENY", 0x00404040UL, 1U);
    clks_syscall_usc_popup_fill_rect(pixels, width, height, 18U, 154U, width - 36U, 1U, 0x00D6D6D6UL);

    clks_syscall_usc_popup_draw_button(pixels, width, height, &buttons[0], "1 ONCE", 0x00FFFFFFUL, 0x000078D7UL,
                                       0x00000000UL);
    clks_syscall_usc_popup_draw_button(pixels, width, height, &buttons[1], "2 SESSION", 0x00FFFFFFUL, 0x000078D7UL,
                                       0x00000000UL);
    clks_syscall_usc_popup_draw_button(pixels, width, height, &buttons[2], "3 PERM", 0x00FFFFFFUL, 0x000078D7UL,
                                       0x00000000UL);
    clks_syscall_usc_popup_draw_button(pixels, width, height, &buttons[3], "N DENY", 0x00F8D7DAUL, 0x00A4262CUL,
                                       0x00000000UL);
}

static clks_bool clks_syscall_usc_decision_from_key(char ch, enum clks_syscall_usc_decision *out_decision) {
    if (out_decision == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (ch == '1' || ch == 'o' || ch == 'O') {
        *out_decision = CLKS_SYSCALL_USC_DECISION_ALLOW_ONCE;
        return CLKS_TRUE;
    }

    if (ch == '2' || ch == 's' || ch == 'S') {
        *out_decision = CLKS_SYSCALL_USC_DECISION_ALLOW_SESSION;
        return CLKS_TRUE;
    }

    if (ch == '3' || ch == 'p' || ch == 'P') {
        *out_decision = CLKS_SYSCALL_USC_DECISION_ALLOW_PERMANENT;
        return CLKS_TRUE;
    }

    if (ch == 'n' || ch == 'N' || ch == '\n' || ch == '\r' || ch == 27) {
        *out_decision = CLKS_SYSCALL_USC_DECISION_DENY;
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

static clks_bool clks_syscall_usc_decision_from_point(i32 x, i32 y, enum clks_syscall_usc_decision *out_decision) {
    static const struct clks_syscall_usc_popup_button buttons[4] = {
        {20U, 176U, 112U, 34U, CLKS_SYSCALL_USC_DECISION_ALLOW_ONCE},
        {140U, 176U, 112U, 34U, CLKS_SYSCALL_USC_DECISION_ALLOW_SESSION},
        {260U, 176U, 112U, 34U, CLKS_SYSCALL_USC_DECISION_ALLOW_PERMANENT},
        {380U, 176U, 112U, 34U, CLKS_SYSCALL_USC_DECISION_DENY},
    };
    u32 i;

    if (out_decision == CLKS_NULL || x < 0 || y < 0) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < 4U; i++) {
        i32 left = (i32)buttons[i].x;
        i32 top = (i32)buttons[i].y;
        i32 right = left + (i32)buttons[i].width;
        i32 bottom = top + (i32)buttons[i].height;

        if (x >= left && x < right && y >= top && y < bottom) {
            *out_decision = buttons[i].decision;
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_syscall_usc_prompt_popup(const char *app_path, const char *name, u64 id,
                                               enum clks_syscall_usc_decision *out_decision) {
    struct clks_framebuffer_info fb;
    u32 *pixels;
    u64 window_id;
    i32 x;
    i32 y;
    clks_bool decided = CLKS_FALSE;
    enum clks_syscall_usc_decision decision = CLKS_SYSCALL_USC_DECISION_DENY;

    if (out_decision == CLKS_NULL || clks_wm_is_foreground() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    fb = clks_fb_info();
    if (fb.width < CLKS_SYSCALL_USC_POPUP_WIDTH || fb.height < CLKS_SYSCALL_USC_POPUP_HEIGHT) {
        return CLKS_FALSE;
    }

    pixels = (u32 *)clks_kmalloc((usize)(CLKS_SYSCALL_USC_POPUP_WIDTH * CLKS_SYSCALL_USC_POPUP_HEIGHT * 4U));
    if (pixels == CLKS_NULL) {
        return CLKS_FALSE;
    }

    x = (i32)((fb.width - CLKS_SYSCALL_USC_POPUP_WIDTH) / 2U);
    y = (i32)((fb.height - CLKS_SYSCALL_USC_POPUP_HEIGHT) / 2U);
    clks_syscall_usc_popup_draw(pixels, CLKS_SYSCALL_USC_POPUP_WIDTH, CLKS_SYSCALL_USC_POPUP_HEIGHT, app_path, name,
                                id);

    window_id = clks_wm_create(CLKS_SYSCALL_USC_POPUP_OWNER, x, y, CLKS_SYSCALL_USC_POPUP_WIDTH,
                               CLKS_SYSCALL_USC_POPUP_HEIGHT, CLKS_WM_FLAG_TOPMOST);
    if (window_id == 0ULL) {
        clks_kfree(pixels);
        return CLKS_FALSE;
    }

    if (clks_wm_present(CLKS_SYSCALL_USC_POPUP_OWNER, window_id, pixels, CLKS_SYSCALL_USC_POPUP_WIDTH,
                        CLKS_SYSCALL_USC_POPUP_HEIGHT, CLKS_SYSCALL_USC_POPUP_WIDTH * 4U) == CLKS_FALSE) {
        (void)clks_wm_destroy(CLKS_SYSCALL_USC_POPUP_OWNER, window_id);
        clks_kfree(pixels);
        return CLKS_FALSE;
    }

    (void)clks_wm_set_focus(CLKS_SYSCALL_USC_POPUP_OWNER, window_id);

    while (decided == CLKS_FALSE) {
        struct clks_wm_event event;

        if (clks_wm_is_foreground() == CLKS_FALSE) {
            break;
        }

        (void)clks_wm_set_focus(CLKS_SYSCALL_USC_POPUP_OWNER, window_id);

        while (clks_wm_poll_event(CLKS_SYSCALL_USC_POPUP_OWNER, window_id, &event) == CLKS_TRUE) {
            if (event.type == CLKS_WM_EVENT_KEY) {
                char ch = (char)(u8)event.arg0;

                if (clks_syscall_usc_decision_from_key(ch, &decision) == CLKS_TRUE) {
                    decided = CLKS_TRUE;
                    break;
                }
            } else if (event.type == CLKS_WM_EVENT_MOUSE_BUTTON) {
                if (((event.arg0 & CLKS_MOUSE_BTN_LEFT) != 0ULL) && ((event.arg1 & CLKS_MOUSE_BTN_LEFT) != 0ULL)) {
                    i32 local_x = (i32)(i64)event.arg2;
                    i32 local_y = (i32)(i64)event.arg3;

                    if (clks_syscall_usc_decision_from_point(local_x, local_y, &decision) == CLKS_TRUE) {
                        decided = CLKS_TRUE;
                        break;
                    }
                }
            }
        }

        if (decided == CLKS_FALSE) {
            clks_syscall_usc_sleep_until_input();
        }
    }

    (void)clks_wm_destroy(CLKS_SYSCALL_USC_POPUP_OWNER, window_id);
    clks_kfree(pixels);

    if (decided == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_decision = decision;
    return CLKS_TRUE;
}
#endif

static enum clks_syscall_usc_decision clks_syscall_usc_prompt_allow(const char *app_path, u64 id, u64 arg0, u64 arg1,
                                                                    u64 arg2) {
    const char *name = clks_syscall_usc_syscall_name(id);
    u32 tty_index = clks_exec_current_tty();

#if !defined(CLKS_CFG_KEYBOARD) || (CLKS_CFG_KEYBOARD == 0)
    (void)tty_index;
    clks_syscall_usc_emit_text_line("BLOCK", "keyboard disabled, cannot prompt");
    return CLKS_SYSCALL_USC_DECISION_DENY;
#else
    clks_syscall_usc_emit_text_line("DANGEROUS_SYSCALL", "REQUEST DETECTED");
    clks_syscall_usc_emit_text_line("APP", app_path);
    clks_syscall_usc_emit_hex_line("SYSCALL_ID", id);
    clks_syscall_usc_emit_text_line("SYSCALL_NAME", name);
    clks_syscall_usc_emit_hex_line("ARG0", arg0);
    clks_syscall_usc_emit_hex_line("ARG1", arg1);
    clks_syscall_usc_emit_hex_line("ARG2", arg2);

    {
        enum clks_syscall_usc_decision popup_decision;

        if (clks_syscall_usc_prompt_popup(app_path, name, id, &popup_decision) == CLKS_TRUE) {
            return popup_decision;
        }
    }

    clks_log(CLKS_LOG_WARN, "USC", "CONFIRM: [1/o]=once [2/s]=session [3/p]=permanent [n]=deny");
    clks_tty_write("[WARN][USC] Allow mode [1/o once, 2/s session, 3/p permanent, n deny]: ");
    clks_serial_write("[WARN][USC] Allow mode [1/o once, 2/s session, 3/p permanent, n deny]: ");

    while (1) {
        char ch = '\0';

        if (clks_keyboard_pop_char_for_tty(tty_index, &ch) == CLKS_TRUE) {
            if (ch == '1' || ch == 'o' || ch == 'O') {
                clks_tty_write("1\n");
                clks_serial_write("1\n");
                return CLKS_SYSCALL_USC_DECISION_ALLOW_ONCE;
            }

            if (ch == '2' || ch == 's' || ch == 'S') {
                clks_tty_write("2\n");
                clks_serial_write("2\n");
                return CLKS_SYSCALL_USC_DECISION_ALLOW_SESSION;
            }

            if (ch == '3' || ch == 'p' || ch == 'P') {
                clks_tty_write("3\n");
                clks_serial_write("3\n");
                return CLKS_SYSCALL_USC_DECISION_ALLOW_PERMANENT;
            }

            if (ch == 'n' || ch == 'N' || ch == '\n' || ch == '\r' || ch == 27) {
                clks_tty_write("n\n");
                clks_serial_write("n\n");
                return CLKS_SYSCALL_USC_DECISION_DENY;
            }

            continue;
        }

        clks_syscall_usc_sleep_until_input();
    }
#endif
}
#endif

static clks_bool clks_syscall_usc_check(u64 id, u64 arg0, u64 arg1, u64 arg2) {
#if CLKS_CFG_USC == 0
    (void)id;
    (void)arg0;
    (void)arg1;
    (void)arg2;
    return CLKS_TRUE;
#else
    char app_path[CLKS_EXEC_PROC_PATH_MAX];
    enum clks_syscall_usc_decision decision;

    if (clks_syscall_usc_is_dangerous(id) == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (clks_exec_is_running() == CLKS_FALSE || clks_exec_current_path_is_user() == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (clks_syscall_usc_current_app_path(app_path, sizeof(app_path)) == CLKS_FALSE) {
        clks_syscall_usc_emit_text_line("BLOCK", "cannot resolve current app path");
        return CLKS_FALSE;
    }

    clks_syscall_usc_load_permanent_rules_if_needed();

    if (clks_syscall_usc_find_permanent_allowed_path(app_path) >= 0) {
        return CLKS_TRUE;
    }

    if (clks_syscall_usc_find_session_allowed_path(app_path) >= 0) {
        return CLKS_TRUE;
    }

    decision = clks_syscall_usc_prompt_allow(app_path, id, arg0, arg1, arg2);
    if (decision == CLKS_SYSCALL_USC_DECISION_ALLOW_ONCE) {
        clks_syscall_usc_emit_text_line("ALLOW_ONCE", app_path);
        return CLKS_TRUE;
    }

    if (decision == CLKS_SYSCALL_USC_DECISION_ALLOW_SESSION) {
        (void)clks_syscall_usc_remember_session_path(app_path);
        clks_syscall_usc_emit_text_line("ALLOW_SESSION", app_path);
        return CLKS_TRUE;
    }

    if (decision == CLKS_SYSCALL_USC_DECISION_ALLOW_PERMANENT) {
        if (clks_syscall_usc_remember_permanent_path(app_path) == CLKS_TRUE) {
            (void)clks_syscall_usc_remember_session_path(app_path);
            clks_syscall_usc_emit_text_line("ALLOW_PERMANENT", app_path);
        } else {
            (void)clks_syscall_usc_remember_session_path(app_path);
            clks_syscall_usc_emit_text_line("ALLOW_SESSION_FALLBACK", app_path);
        }
        return CLKS_TRUE;
    }

    clks_syscall_usc_emit_text_line("DENY", app_path);
    return CLKS_FALSE;
#endif
}

static void clks_syscall_stats_reset(void) {
    clks_syscall_stats_total = 0ULL;
    clks_memset(clks_syscall_stats_id_count, 0, sizeof(clks_syscall_stats_id_count));
    clks_memset(clks_syscall_stats_recent_id_count, 0, sizeof(clks_syscall_stats_recent_id_count));
    clks_memset(clks_syscall_stats_recent_ring, 0, sizeof(clks_syscall_stats_recent_ring));
    clks_syscall_stats_recent_head = 0U;
    clks_syscall_stats_recent_size = 0U;
}

static void clks_syscall_stats_record(u64 id) {
    u16 ring_id = 0xFFFFU;

    clks_syscall_stats_total++;

    if (id <= CLKS_SYSCALL_STATS_MAX_ID) {
        clks_syscall_stats_id_count[id]++;
    }

    if (id <= 0xFFFFULL) {
        ring_id = (u16)id;
    }

    if (clks_syscall_stats_recent_size >= CLKS_SYSCALL_STATS_RING_SIZE) {
        u64 old_id = (u64)clks_syscall_stats_recent_ring[clks_syscall_stats_recent_head];

        if (old_id <= CLKS_SYSCALL_STATS_MAX_ID && clks_syscall_stats_recent_id_count[old_id] > 0ULL) {
            clks_syscall_stats_recent_id_count[old_id]--;
        }
    } else {
        clks_syscall_stats_recent_size++;
    }

    clks_syscall_stats_recent_ring[clks_syscall_stats_recent_head] = ring_id;

    if (id <= CLKS_SYSCALL_STATS_MAX_ID) {
        clks_syscall_stats_recent_id_count[id]++;
    }

    clks_syscall_stats_recent_head++;

    if (clks_syscall_stats_recent_head >= CLKS_SYSCALL_STATS_RING_SIZE) {
        clks_syscall_stats_recent_head = 0U;
    }
}

static u64 clks_syscall_stats_total_count(void) {
    return clks_syscall_stats_total;
}

static u64 clks_syscall_stats_id(u64 id) {
    if (id > CLKS_SYSCALL_STATS_MAX_ID) {
        return 0ULL;
    }

    return clks_syscall_stats_id_count[id];
}

static u64 clks_syscall_stats_recent_window(void) {
    return (u64)clks_syscall_stats_recent_size;
}

static u64 clks_syscall_stats_recent_id(u64 id) {
    if (id > CLKS_SYSCALL_STATS_MAX_ID) {
        return 0ULL;
    }

    return clks_syscall_stats_recent_id_count[id];
}

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
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_sleep_ticks(frame->rbx));
    case CLKS_SYSCALL_YIELD:
        CLKS_SYSCALL_DISPATCH_RETURN(clks_syscall_yield());
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
