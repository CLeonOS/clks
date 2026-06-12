// Kernel main function

#include <clks/boot.h>
#include <clks/audio.h>
#include <clks/bootsplash.h>
#include <clks/clboot.h>
#include <clks/cpu.h>
#include <clks/desktop.h>
#include <clks/display.h>
#include <clks/driver.h>
#include <clks/exec.h>
#include <clks/framebuffer.h>
#include <clks/fs.h>
#include <clks/heap.h>
#include <clks/interrupts.h>
#include <clks/inputm.h>
#include <clks/keyboard.h>
#include <clks/kernel.h>
#include <clks/locale.h>
#include <clks/log.h>
#include <clks/mouse.h>
#include <clks/net.h>
#include <clks/pmm.h>
#include <clks/scheduler.h>
#include <clks/serial.h>
#include <clks/service.h>
#include <clks/string.h>
#include <clks/syscall.h>
#include <clks/tty.h>
#include <clks/types.h>
#include <clks/user.h>
#include <clks/userland.h>
#include <clks/vm.h>
#include <clks/wm.h>

/* Boot orchestration file: one wrong init order and the whole damn thing faceplants. */

static void clks_kmain_apply_boot_loglevel(void) {
    char value[24];

    if (clks_boot_cmdline_get_value("clks.loglevel", value, sizeof(value)) == CLKS_FALSE &&
        clks_boot_cmdline_get_value("loglevel", value, sizeof(value)) == CLKS_FALSE) {
        return;
    }

    if (clks_strcmp(value, "quiet") == 0 || clks_strcmp(value, "warn") == 0) {
        clks_log_set_min_level(CLKS_LOG_WARN);
    } else if (clks_strcmp(value, "error") == 0) {
        clks_log_set_min_level(CLKS_LOG_ERROR);
    } else if (clks_strcmp(value, "debug") == 0 || clks_strcmp(value, "verbose") == 0) {
        clks_log_set_min_level(CLKS_LOG_DEBUG);
    } else if (clks_strcmp(value, "info") == 0 || clks_strcmp(value, "normal") == 0) {
        clks_log_set_min_level(CLKS_LOG_INFO);
    }
}

static void clks_kmain_apply_boot_locale(void) {
    char value[CLKS_LOCALE_MAX];

    if (clks_boot_cmdline_get_value("clks.locale", value, sizeof(value)) == CLKS_FALSE &&
        clks_boot_cmdline_get_value("locale", value, sizeof(value)) == CLKS_FALSE) {
        return;
    }

    if (clks_locale_set(value, CLKS_FALSE) == CLKS_TRUE) {
        clks_log(CLKS_LOG_INFO, "LOCALE", "BOOT CMDLINE OVERRIDE");
        clks_log(CLKS_LOG_INFO, "LOCALE", value);
    } else {
        clks_log(CLKS_LOG_WARN, "LOCALE", "INVALID BOOT CMDLINE LOCALE");
        clks_log(CLKS_LOG_WARN, "LOCALE", value);
    }
}

static void clks_kmain_emit_clboot_log(void) {
    const char *bootlog;
    u64 size = 0ULL;
    u64 entries = 0ULL;
    u64 pos = 0ULL;
    char line[160];

    bootlog = clks_clboot_get_bootlog(&size, &entries);
    if (bootlog == CLKS_NULL || size == 0ULL) {
        return;
    }

    clks_log(CLKS_LOG_INFO, "CLBOOT", "BOOT LOG BEGIN");
    clks_log_u64(CLKS_LOG_INFO, "CLBOOT", "entry count", entries);

    while (pos < size && bootlog[pos] != '\0') {
        usize out = 0U;

        while (pos < size && bootlog[pos] != '\0' && bootlog[pos] != '\n' && out + 1U < sizeof(line)) {
            line[out++] = bootlog[pos++];
        }

        line[out] = '\0';
        if (out > 0U) {
            clks_log(CLKS_LOG_INFO, "CLBOOT", line);
        }

        while (pos < size && (bootlog[pos] == '\n' || bootlog[pos] == '\r')) {
            pos++;
        }

        if (out == 0U && pos < size && bootlog[pos] == '\0') {
            break;
        }
    }

    clks_log(CLKS_LOG_INFO, "CLBOOT", "BOOT LOG END");
}

#ifndef CLKS_CFG_AUDIO
#define CLKS_CFG_AUDIO 1
#endif

#ifndef CLKS_CFG_MOUSE
#define CLKS_CFG_MOUSE 1
#endif

#ifndef CLKS_CFG_DESKTOP
#define CLKS_CFG_DESKTOP 1
#endif

#ifndef CLKS_CFG_DRIVER_MANAGER
#define CLKS_CFG_DRIVER_MANAGER 1
#endif

#ifndef CLKS_CFG_HEAP_SELFTEST
#define CLKS_CFG_HEAP_SELFTEST 1
#endif

#ifndef CLKS_CFG_EXTERNAL_TTY_FONT
#define CLKS_CFG_EXTERNAL_TTY_FONT 1
#endif

#ifndef CLKS_CFG_KEYBOARD
#define CLKS_CFG_KEYBOARD 1
#endif

#ifndef CLKS_CFG_KLOGD_TASK
#define CLKS_CFG_KLOGD_TASK 1
#endif

#ifndef CLKS_CFG_KWORKER_TASK
#define CLKS_CFG_KWORKER_TASK 1
#endif

#ifndef CLKS_CFG_USRD_TASK
#define CLKS_CFG_USRD_TASK 1
#endif

#ifndef CLKS_CFG_BOOT_VIDEO_LOG
#define CLKS_CFG_BOOT_VIDEO_LOG 1
#endif

#ifndef CLKS_CFG_PMM_STATS_LOG
#define CLKS_CFG_PMM_STATS_LOG 1
#endif

#ifndef CLKS_CFG_HEAP_STATS_LOG
#define CLKS_CFG_HEAP_STATS_LOG 1
#endif

#ifndef CLKS_CFG_FS_ROOT_LOG
#define CLKS_CFG_FS_ROOT_LOG 1
#endif

#ifndef CLKS_CFG_SYSTEM_DIR_CHECK
#define CLKS_CFG_SYSTEM_DIR_CHECK 1
#endif

#ifndef CLKS_CFG_SYSCALL_TICK_QUERY
#define CLKS_CFG_SYSCALL_TICK_QUERY 1
#endif

#ifndef CLKS_CFG_TTY_READY_LOG
#define CLKS_CFG_TTY_READY_LOG 1
#endif

#ifndef CLKS_CFG_IDLE_DEBUG_LOG
#define CLKS_CFG_IDLE_DEBUG_LOG 1
#endif

#ifndef CLKS_CFG_SCHED_TASK_COUNT_LOG
#define CLKS_CFG_SCHED_TASK_COUNT_LOG 1
#endif

#ifndef CLKS_CFG_INTERRUPT_READY_LOG
#define CLKS_CFG_INTERRUPT_READY_LOG 1
#endif

#if CLKS_CFG_KLOGD_TASK
static void clks_task_klogd(u64 tick) {
    static u64 last_emit = 0ULL;

    clks_service_heartbeat(CLKS_SERVICE_LOG, tick);

    if (tick - last_emit >= 1000ULL) {
        clks_log_u64(CLKS_LOG_DEBUG, "TASK", "klogd tick", tick);
        last_emit = tick;
    }
}
#endif

#if CLKS_CFG_KWORKER_TASK
static void clks_task_kworker(u64 tick) {
    static u32 phase = 0U;

    clks_service_heartbeat(CLKS_SERVICE_SCHED, tick);

    switch (phase) {
    case 0U:
        clks_service_heartbeat(CLKS_SERVICE_MEM, tick);
        break;
    case 1U:
        clks_service_heartbeat(CLKS_SERVICE_FS, tick);
        break;
    case 2U:
        clks_service_heartbeat(CLKS_SERVICE_DRIVER, tick);
        break;
    default:
        clks_service_heartbeat(CLKS_SERVICE_LOG, tick);
        break;
    }

    phase = (phase + 1U) & 3U;
}
#endif

#if CLKS_CFG_USRD_TASK
static void clks_task_usrd(u64 tick) {
    clks_service_heartbeat(CLKS_SERVICE_USER, tick);
    clks_exec_tick(tick);
    clks_userland_tick(tick);
#if CLKS_CFG_DESKTOP
    if (clks_wm_ready() == CLKS_TRUE) {
        clks_wm_tick(tick);
    } else {
        clks_desktop_tick(tick);
    }
#endif
    clks_tty_tick(tick);
}
#endif

void clks_kernel_entry(u64 boot_magic, void *boot_info) {
    clks_clboot_set_info(boot_magic, (const struct clboot_info *)boot_info);
    clks_kernel_main();
}

void clks_kernel_main(void) {
    const struct limine_framebuffer *boot_fb;
    const struct limine_memmap_response *boot_memmap;
    struct clks_pmm_stats pmm_stats;
    struct clks_heap_stats heap_stats;
    struct clks_fs_node_info fs_system_dir = {0};
    u64 syscall_ticks;
    u64 fs_root_children;
    clks_bool boot_splash_was_active;
    clks_bool rescue_mode;
    clks_bool boot_splash_enabled;

    /* Serial first, because when graphics dies we still need a heartbeat. */
    clks_serial_init();
    clks_cpu_init_fpu();

    /* If boot protocol handshake fails, continuing would be pure fantasy. */
    if (clks_boot_base_revision_supported() == CLKS_FALSE) {
        clks_serial_write("[ERROR][BOOT] LIMINE BASE REVISION NOT SUPPORTED\n");
        clks_cpu_halt_forever();
    }

    boot_fb = clks_boot_get_framebuffer();
    rescue_mode = clks_boot_rescue_mode();
    clks_kmain_apply_boot_loglevel();
    boot_splash_enabled = (rescue_mode == CLKS_FALSE &&
                           clks_boot_cmdline_flag_enabled("clks.nosplash") == CLKS_FALSE &&
                           clks_boot_cmdline_flag_enabled("nosplash") == CLKS_FALSE)
                              ? CLKS_TRUE
                              : CLKS_FALSE;

    /* TTY comes up only when framebuffer exists; no pixels, no pretty lies. */
    if (boot_fb != CLKS_NULL) {
        clks_fb_init(boot_fb);
        clks_display_init();
        clks_tty_init();
        if (boot_splash_enabled == CLKS_TRUE) {
            clks_bootsplash_init();
        }
    }

    clks_log(CLKS_LOG_INFO, "BOOT", "CLeonKernelSystem START");
    clks_kmain_emit_clboot_log();
    if (rescue_mode == CLKS_TRUE) {
        clks_log(CLKS_LOG_WARN, "BOOT", "RESCUE MODE ENABLED");
    }
    clks_bootsplash_step(3U, "boot protocol");

    if (boot_fb == CLKS_NULL) {
        clks_log(CLKS_LOG_WARN, "VIDEO", "NO FRAMEBUFFER FROM LIMINE");
    } else {
#if CLKS_CFG_BOOT_VIDEO_LOG
        clks_log_u64(CLKS_LOG_INFO, "VIDEO", "width px", boot_fb->width);
        clks_log_u64(CLKS_LOG_INFO, "VIDEO", "height px", boot_fb->height);
        clks_log_bytes(CLKS_LOG_INFO, "VIDEO", "pitch", boot_fb->pitch);
        clks_log_u64(CLKS_LOG_INFO, "VIDEO", "bits per pixel", boot_fb->bpp);
#else
        clks_log(CLKS_LOG_WARN, "CFG", "BOOT VIDEO LOGS DISABLED BY MENUCONFIG");
#endif
    }
    clks_bootsplash_step(8U, "video online");

#if defined(CLKS_ARCH_X86_64)
    clks_log(CLKS_LOG_INFO, "ARCH", "X86_64 ONLINE");
#elif defined(CLKS_ARCH_AARCH64)
    clks_log(CLKS_LOG_INFO, "ARCH", "AARCH64 ONLINE");
#endif
    clks_bootsplash_step(12U, "architecture online");

    boot_memmap = clks_boot_get_memmap();

    if (boot_memmap == CLKS_NULL) {
        clks_log(CLKS_LOG_ERROR, "MEM", "NO LIMINE MEMMAP RESPONSE");
        clks_cpu_halt_forever();
    }

    clks_pmm_init(boot_memmap);
    pmm_stats = clks_pmm_get_stats();
    clks_vm_init();
    clks_bootsplash_step(22U, "memory manager");

#if CLKS_CFG_PMM_STATS_LOG
    clks_log_u64(CLKS_LOG_INFO, "PMM", "managed pages", pmm_stats.managed_pages);
    clks_log_u64(CLKS_LOG_INFO, "PMM", "free pages", pmm_stats.free_pages);
    clks_log_u64(CLKS_LOG_INFO, "PMM", "used pages", pmm_stats.used_pages);
    clks_log_u64(CLKS_LOG_INFO, "PMM", "dropped pages", pmm_stats.dropped_pages);
#else
    (void)pmm_stats;
    clks_log(CLKS_LOG_WARN, "CFG", "PMM STATS LOGS DISABLED BY MENUCONFIG");
#endif

    clks_heap_init();
    heap_stats = clks_heap_get_stats();
    clks_bootsplash_step(28U, "heap allocator");

#if CLKS_CFG_HEAP_STATS_LOG
    clks_log_bytes(CLKS_LOG_INFO, "HEAP", "total", heap_stats.total_bytes);
    clks_log_bytes(CLKS_LOG_INFO, "HEAP", "free", heap_stats.free_bytes);
#else
    (void)heap_stats;
    clks_log(CLKS_LOG_WARN, "CFG", "HEAP STATS LOGS DISABLED BY MENUCONFIG");
#endif

#if CLKS_CFG_HEAP_SELFTEST
    void *heap_probe = clks_kmalloc(128);

    if (heap_probe == CLKS_NULL) {
        clks_log(CLKS_LOG_ERROR, "HEAP", "KMALLOC SELFTEST FAILED");
    } else {
        clks_log(CLKS_LOG_INFO, "HEAP", "KMALLOC SELFTEST OK");
        clks_kfree(heap_probe);
    }
#else
    clks_log(CLKS_LOG_WARN, "CFG", "HEAP SELFTEST DISABLED BY MENUCONFIG");
#endif

    clks_fs_init();
    clks_bootsplash_step(36U, "filesystem probe");

    if (clks_fs_is_ready() == CLKS_FALSE) {
        clks_log(CLKS_LOG_ERROR, "FS", "RAMDISK FS INIT FAILED");
        clks_cpu_halt_forever();
    }

    clks_locale_init();
    clks_kmain_apply_boot_locale();
    clks_bootsplash_step(42U, "filesystem online");

    fs_root_children = clks_fs_count_children("/");
#if CLKS_CFG_FS_ROOT_LOG
    clks_log_u64(CLKS_LOG_INFO, "FS", "root entries", fs_root_children);
#else
    (void)fs_root_children;
#endif

#if CLKS_CFG_SYSTEM_DIR_CHECK
    if (clks_fs_stat("/system", &fs_system_dir) == CLKS_FALSE || fs_system_dir.type != CLKS_FS_NODE_DIR) {
        clks_log(CLKS_LOG_ERROR, "FS", "/SYSTEM DIRECTORY CHECK FAILED");
        clks_cpu_halt_forever();
    }
#else
    (void)fs_system_dir;
    clks_log(CLKS_LOG_WARN, "CFG", "/SYSTEM DIRECTORY CHECK DISABLED BY MENUCONFIG");
#endif

    if (boot_fb != CLKS_NULL) {
#if CLKS_CFG_EXTERNAL_TTY_FONT
        const void *tty_ttf_blob;
        const void *emoji_ttf_blob;
        u64 tty_ttf_size = 0ULL;
        u64 emoji_ttf_size = 0ULL;

        tty_ttf_blob = clks_fs_read_all("/system/others/fonts/tty.ttf", &tty_ttf_size);

        if (tty_ttf_blob != CLKS_NULL && clks_fb_load_ttf_font(tty_ttf_blob, tty_ttf_size) == CLKS_TRUE) {
            emoji_ttf_blob = clks_fs_read_all("/system/others/fonts/emoji.ttf", &emoji_ttf_size);
            if (emoji_ttf_blob != CLKS_NULL &&
                clks_fb_load_emoji_ttf_font(emoji_ttf_blob, emoji_ttf_size) == CLKS_TRUE) {
                clks_log(CLKS_LOG_INFO, "TTY", "EMOJI TTF LOADED");
                clks_log_bytes(CLKS_LOG_INFO, "TTY", "emoji font size", emoji_ttf_size);
            } else {
                clks_log(CLKS_LOG_WARN, "TTY", "EMOJI TTF LOAD FAILED, EMOJI FALLBACK DISABLED");
                clks_log_bytes(CLKS_LOG_WARN, "TTY", "emoji font size", emoji_ttf_size);
            }
            clks_tty_init();
            clks_bootsplash_step(50U, "tty font loaded");
            clks_log(CLKS_LOG_INFO, "TTY", "EXTERNAL TTF LOADED");
            clks_log_bytes(CLKS_LOG_INFO, "TTY", "font size", tty_ttf_size);
        } else {
            clks_log(CLKS_LOG_WARN, "TTY", "EXTERNAL TTF LOAD FAILED, USING BUILTIN");
            clks_log_bytes(CLKS_LOG_WARN, "TTY", "font size", tty_ttf_size);
            clks_bootsplash_step(50U, "builtin tty font");
        }
#else
        clks_log(CLKS_LOG_WARN, "CFG", "EXTERNAL TTY FONT LOADING DISABLED BY MENUCONFIG");
        clks_bootsplash_step(50U, "tty font skipped");
#endif
    }

    clks_exec_init();
    clks_user_init();
    clks_bootsplash_step(56U, "exec runtime");
#if CLKS_CFG_AUDIO
    clks_audio_init();
#else
    clks_log(CLKS_LOG_WARN, "CFG", "AUDIO DISABLED BY MENUCONFIG");
#endif
#if CLKS_CFG_KEYBOARD
    clks_inputm_init();
    clks_keyboard_init();
#else
    clks_log(CLKS_LOG_WARN, "CFG", "KEYBOARD DISABLED BY MENUCONFIG");
#endif
#if CLKS_CFG_MOUSE
    clks_mouse_init();
#else
    clks_log(CLKS_LOG_WARN, "CFG", "MOUSE DISABLED BY MENUCONFIG");
#endif
#if CLKS_CFG_DESKTOP
    clks_desktop_init();
    clks_wm_init();
#else
    clks_log(CLKS_LOG_WARN, "CFG", "DESKTOP DISABLED BY MENUCONFIG");
#endif
    clks_bootsplash_step(64U, "devices online");

    if (clks_userland_init() == CLKS_FALSE) {
        clks_log(CLKS_LOG_ERROR, "USER", "USERLAND INIT FAILED");
        clks_cpu_halt_forever();
    }
    clks_bootsplash_step(72U, "userland online");

    clks_net_init();
    clks_bootsplash_step(78U, "network online");

#if CLKS_CFG_DRIVER_MANAGER
    clks_driver_init();
#else
    clks_log(CLKS_LOG_WARN, "CFG", "DRIVER MANAGER DISABLED BY MENUCONFIG");
#endif

    clks_bootsplash_step(84U, "drivers online");

    /* Scheduler init is the "okay, now this mess is actually alive" moment. */
    clks_scheduler_init();
    clks_bootsplash_step(88U, "scheduler online");

#if CLKS_CFG_KLOGD_TASK
    if (clks_scheduler_add_kernel_task_ex("klogd", 4U, clks_task_klogd) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "SCHED", "FAILED TO ADD KLOGD TASK");
    }
#else
    clks_log(CLKS_LOG_WARN, "SCHED", "KLOGD TASK DISABLED BY MENUCONFIG");
#endif

#if CLKS_CFG_KWORKER_TASK
    if (clks_scheduler_add_kernel_task_ex("kworker", 3U, clks_task_kworker) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "SCHED", "FAILED TO ADD KWORKER TASK");
    }
#else
    clks_log(CLKS_LOG_WARN, "SCHED", "KWORKER TASK DISABLED BY MENUCONFIG");
#endif

#if CLKS_CFG_USRD_TASK
    if (clks_scheduler_add_kernel_task_ex("usrd", 4U, clks_task_usrd) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "SCHED", "FAILED TO ADD USRD TASK");
    }
#else
    clks_log(CLKS_LOG_WARN, "SCHED", "USRD TASK DISABLED BY MENUCONFIG");
#endif

#if CLKS_CFG_SCHED_TASK_COUNT_LOG
    {
        struct clks_scheduler_stats sched_stats = clks_scheduler_get_stats();
        clks_log_u64(CLKS_LOG_INFO, "SCHED", "task count", sched_stats.task_count);
    }
#else
    clks_log(CLKS_LOG_WARN, "CFG", "SCHED TASK COUNT LOG DISABLED BY MENUCONFIG");
#endif

    clks_service_init();
    clks_bootsplash_step(91U, "kernel services");

    clks_syscall_init();
    clks_bootsplash_step(95U, "syscalls online");

    clks_interrupts_init();
#if CLKS_CFG_INTERRUPT_READY_LOG
    clks_log(CLKS_LOG_INFO, "INT", "IDT + PIC INITIALIZED");
#endif
    clks_bootsplash_step(97U, "interrupts online");

#if CLKS_CFG_SYSCALL_TICK_QUERY
    syscall_ticks = clks_syscall_invoke_kernel(CLKS_SYSCALL_TIMER_TICKS, 0ULL, 0ULL, 0ULL);
    clks_log_u64(CLKS_LOG_INFO, "SYSCALL", "timer ticks", syscall_ticks);
#else
    (void)syscall_ticks;
    clks_log(CLKS_LOG_WARN, "CFG", "SYSCALL TICK QUERY DISABLED BY MENUCONFIG");
#endif

    clks_bootsplash_step(99U, "user entry armed");

#if CLKS_CFG_KEYBOARD
    clks_keyboard_set_input_ready(CLKS_TRUE);
#endif

#if CLKS_CFG_TTY_READY_LOG
    clks_log_u64(CLKS_LOG_INFO, "TTY", "terminal count", (u64)clks_tty_count());
    clks_log_u64(CLKS_LOG_INFO, "TTY", "active terminal", (u64)clks_tty_active());
    clks_log(CLKS_LOG_INFO, "TTY", "VIRTUAL TTY0 READY");
    clks_log(CLKS_LOG_INFO, "TTY", "CURSOR ENABLED");
#endif
#if CLKS_CFG_IDLE_DEBUG_LOG
    clks_log(CLKS_LOG_DEBUG, "KERNEL", "IDLE LOOP ENTER");
#endif

    boot_splash_was_active = clks_bootsplash_active();
    clks_bootsplash_finish();
    if (boot_splash_was_active == CLKS_TRUE) {
        clks_tty_clear_active();
    }

    /* Infinite idle loop: glamorous name for "wait forever and hope interrupts behave". */
    for (;;) {
        clks_net_poll();
        u64 tick_now = clks_interrupts_timer_ticks();
        clks_scheduler_dispatch_ready(tick_now);
#if defined(CLKS_ARCH_X86_64)
        __asm__ volatile("hlt");
#elif defined(CLKS_ARCH_AARCH64)
        __asm__ volatile("wfe");
#endif
    }
}
