#include <clks/boot.h>
#include <clks/elf64.h>
#include <clks/exec.h>
#include <clks/fs.h>
#include <clks/log.h>
#include <clks/panic.h>
#include <clks/rust.h>
#include <clks/string.h>
#include <clks/types.h>
#include <clks/userland.h>

#define CLKS_USERLAND_RETRY_INTERVAL 500ULL
#define CLKS_USERLAND_PATH_MAX 192U
#define CLKS_USERLAND_ARGS_MAX 256U
#define CLKS_USERLAND_ENV_MAX 512U
#define CLKS_USERLAND_CONFIG_MAX 192U

#ifndef CLKS_CFG_USER_SPACE_ENTER
#define CLKS_CFG_USER_SPACE_ENTER 1
#endif

#ifndef CLKS_CFG_USER_SPACE_ENTER_CONFIG
#define CLKS_CFG_USER_SPACE_ENTER_CONFIG 1
#endif

#ifndef CLKS_CFG_USER_SPACE_ENTER_STRICT_PANIC
#define CLKS_CFG_USER_SPACE_ENTER_STRICT_PANIC 1
#endif

static clks_bool clks_user_entry_ready = CLKS_FALSE;
static clks_bool clks_user_entry_exec_requested_flag = CLKS_FALSE;
static clks_bool clks_user_entry_enabled = CLKS_FALSE;
static u64 clks_user_launch_attempt_count = 0ULL;
static u64 clks_user_launch_success_count = 0ULL;
static u64 clks_user_launch_fail_count = 0ULL;
static u64 clks_user_last_try_tick = 0ULL;
static clks_bool clks_user_first_try_pending = CLKS_FALSE;
static u64 clks_user_entry_last_pid = 0ULL;
static char clks_user_entry_config_path[CLKS_USERLAND_CONFIG_MAX];
static char clks_user_entry_path[CLKS_USERLAND_PATH_MAX];
static char clks_user_entry_args[CLKS_USERLAND_ARGS_MAX];
static char clks_user_entry_env[CLKS_USERLAND_ENV_MAX];

static void clks_userland_copy_text(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || dst_size == 0U) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void clks_userland_select_config_path(void) {
    char mode[32];

    clks_userland_copy_text(clks_user_entry_config_path, sizeof(clks_user_entry_config_path),
                            "/system/configs/user_space_enter.conf");

    if (CLKS_CFG_USER_SPACE_ENTER_CONFIG == 0) {
        return;
    }

    if (clks_boot_cmdline_get_value("clks.installer", mode, sizeof(mode)) == CLKS_FALSE &&
        clks_boot_cmdline_get_value("installer", mode, sizeof(mode)) == CLKS_FALSE) {
        return;
    }

    if (clks_strcmp(mode, "install") == 0) {
        clks_userland_copy_text(clks_user_entry_config_path, sizeof(clks_user_entry_config_path),
                                "/system/configs/user_space_enter.install.conf");
    } else if (clks_strcmp(mode, "repair") == 0) {
        clks_userland_copy_text(clks_user_entry_config_path, sizeof(clks_user_entry_config_path),
                                "/system/configs/user_space_enter.repair.conf");
    } else if (clks_strcmp(mode, "update-kernel") == 0) {
        clks_userland_copy_text(clks_user_entry_config_path, sizeof(clks_user_entry_config_path),
                                "/system/configs/user_space_enter.update-kernel.conf");
    } else if (clks_strcmp(mode, "verify") == 0) {
        clks_userland_copy_text(clks_user_entry_config_path, sizeof(clks_user_entry_config_path),
                                "/system/configs/user_space_enter.verify.conf");
    } else {
        clks_log(CLKS_LOG_WARN, "USER", "UNKNOWN USER ENTRY BOOT MODE");
        clks_log(CLKS_LOG_WARN, "USER", mode);
    }
}

static void clks_userland_panic_config(const char *reason) {
    clks_log(CLKS_LOG_ERROR, "USER", "USER SPACE ENTER CONFIG FAILED");
    clks_log(CLKS_LOG_ERROR, "USER", reason);
    clks_log(CLKS_LOG_ERROR, "USER", clks_user_entry_config_path);
    clks_log(CLKS_LOG_ERROR, "USER", clks_user_entry_path[0] != '\0' ? clks_user_entry_path : "<empty path>");

#if CLKS_CFG_USER_SPACE_ENTER_STRICT_PANIC
    clks_panic(reason);
#else
    (void)reason;
#endif
}

static clks_bool clks_userland_parse_config(void) {
    const char *data;
    u64 size = 0ULL;
    clks_rust_kv_entry entries[3];

    clks_user_entry_path[0] = '\0';
    clks_user_entry_args[0] = '\0';
    clks_user_entry_env[0] = '\0';

    data = (const char *)clks_fs_read_all(clks_user_entry_config_path, &size);
    if (data == CLKS_NULL || size == 0ULL) {
        clks_userland_panic_config("missing user_space_enter config");
        return CLKS_FALSE;
    }

    entries[0].key = "path";
    entries[0].out_value = clks_user_entry_path;
    entries[0].out_size = sizeof(clks_user_entry_path);
    entries[1].key = "args";
    entries[1].out_value = clks_user_entry_args;
    entries[1].out_size = sizeof(clks_user_entry_args);
    entries[2].key = "env";
    entries[2].out_value = clks_user_entry_env;
    entries[2].out_size = sizeof(clks_user_entry_env);

    if (clks_rust_parse_key_values(data, size, entries, 3ULL) == CLKS_FALSE) {
        clks_userland_panic_config("user_space_enter parse failed");
        return CLKS_FALSE;
    }

    if (entries[0].truncated == CLKS_TRUE || entries[1].truncated == CLKS_TRUE || entries[2].truncated == CLKS_TRUE) {
        clks_userland_panic_config("user_space_enter value too long");
        return CLKS_FALSE;
    }

    if (clks_user_entry_path[0] == '\0') {
        clks_userland_panic_config("empty user_space_enter path");
        return CLKS_FALSE;
    }

    if (clks_user_entry_path[0] != '/') {
        clks_userland_panic_config("user_space_enter path is not absolute");
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_userland_probe_elf(const char *path) {
    const void *image;
    u64 size = 0ULL;
    struct clks_elf64_info info;

    image = clks_fs_read_all(path, &size);

    if (image == CLKS_NULL) {
        clks_userland_panic_config("entry ELF missing");
        return CLKS_FALSE;
    }

    if (clks_elf64_inspect(image, size, &info) == CLKS_FALSE) {
        clks_userland_panic_config("entry ELF inspect failed");
        return CLKS_FALSE;
    }

    clks_log(CLKS_LOG_INFO, "USER", "USER ENTRY ELF READY");
    clks_log(CLKS_LOG_INFO, "USER", path);
    clks_log_bytes(CLKS_LOG_INFO, "USER", "ELF size", size);
    clks_log_hex(CLKS_LOG_INFO, "USER", "ENTRY", info.entry);
    return CLKS_TRUE;
}

static clks_bool clks_userland_request_entry_exec(void) {
    u64 pid = (u64)-1;

    if (clks_user_entry_ready == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_user_launch_attempt_count++;
    clks_log(CLKS_LOG_INFO, "USER", "USER ENTRY SPAWN START");
    clks_log(CLKS_LOG_INFO, "USER", clks_user_entry_path);

    if (clks_exec_spawn_pathv(clks_user_entry_path, clks_user_entry_args, clks_user_entry_env, &pid) == CLKS_TRUE) {
        clks_user_entry_exec_requested_flag = CLKS_TRUE;
        clks_user_entry_last_pid = pid;
        clks_user_launch_success_count++;

        clks_log(CLKS_LOG_INFO, "USER", "USER ENTRY SPAWN REQUESTED");
        clks_log(CLKS_LOG_INFO, "USER", clks_user_entry_path);
        clks_log_u64(CLKS_LOG_INFO, "USER", "entry pid", pid);
        return CLKS_TRUE;
    }

    clks_user_launch_fail_count++;
    clks_log(CLKS_LOG_WARN, "USER", "USER ENTRY EXEC REQUEST FAILED");
    clks_log(CLKS_LOG_WARN, "USER", clks_user_entry_path);
    return CLKS_FALSE;
}

clks_bool clks_userland_init(void) {
    clks_log(CLKS_LOG_INFO, "USER", "USERLAND FRAMEWORK ONLINE");

    clks_user_entry_ready = CLKS_FALSE;
    clks_user_entry_exec_requested_flag = CLKS_FALSE;
    clks_user_entry_enabled = (CLKS_CFG_USER_SPACE_ENTER != 0) ? CLKS_TRUE : CLKS_FALSE;
    clks_user_launch_attempt_count = 0ULL;
    clks_user_launch_success_count = 0ULL;
    clks_user_launch_fail_count = 0ULL;
    clks_user_last_try_tick = 0ULL;
    clks_user_first_try_pending = CLKS_TRUE;
    clks_user_entry_last_pid = 0ULL;

    if (clks_user_entry_enabled == CLKS_FALSE) {
        clks_userland_panic_config("user_space_enter disabled");
        return CLKS_FALSE;
    }

    clks_userland_select_config_path();
    clks_log(CLKS_LOG_INFO, "USER", "USER ENTRY CONFIG");
    clks_log(CLKS_LOG_INFO, "USER", clks_user_entry_config_path);

    if (clks_userland_parse_config() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_userland_probe_elf(clks_user_entry_path) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_user_entry_ready = CLKS_TRUE;
    clks_log(CLKS_LOG_INFO, "USER", "USER ENTRY READY");
    return CLKS_TRUE;
}

void clks_userland_tick(u64 tick) {
    if (clks_user_entry_enabled == CLKS_FALSE || clks_user_entry_ready == CLKS_FALSE) {
        return;
    }

    if (clks_user_entry_exec_requested_flag == CLKS_TRUE && clks_exec_has_user_process() == CLKS_TRUE) {
        return;
    }

    if (clks_user_entry_exec_requested_flag == CLKS_TRUE && clks_exec_has_user_process() == CLKS_FALSE) {
        clks_user_entry_exec_requested_flag = CLKS_FALSE;
        clks_user_entry_last_pid = 0ULL;
    }

    if (clks_user_first_try_pending == CLKS_TRUE) {
        clks_user_first_try_pending = CLKS_FALSE;
        clks_user_last_try_tick = tick;
        (void)clks_userland_request_entry_exec();
        return;
    }

    if (tick - clks_user_last_try_tick < CLKS_USERLAND_RETRY_INTERVAL) {
        return;
    }

    clks_user_last_try_tick = tick;
    (void)clks_userland_request_entry_exec();
}

clks_bool clks_userland_shell_ready(void) {
    return clks_user_entry_ready;
}

clks_bool clks_userland_shell_exec_requested(void) {
    return clks_user_entry_exec_requested_flag;
}

clks_bool clks_userland_shell_auto_exec_enabled(void) {
    return clks_user_entry_enabled;
}

u64 clks_userland_launch_attempts(void) {
    return clks_user_launch_attempt_count;
}

u64 clks_userland_launch_success(void) {
    return clks_user_launch_success_count;
}

u64 clks_userland_launch_failures(void) {
    return clks_user_launch_fail_count;
}
