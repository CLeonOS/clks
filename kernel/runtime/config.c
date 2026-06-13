#include <clks/config.h>
#include <clks/fs.h>
#include <clks/log.h>
#include <clks/rust.h>
#include <clks/string.h>
#include <clks/types.h>

static char clks_config_theme_value[CLKS_CONFIG_THEME_MAX] = "default";
static char clks_config_startup_command_value[CLKS_CONFIG_STARTUP_MAX] = "";

static void clks_config_copy(char *dst, usize dst_size, const char *src) {
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

static void clks_config_read_theme(void) {
    const char *data;
    u64 size = 0ULL;
    clks_rust_kv_entry entry;

    clks_config_copy(clks_config_theme_value, sizeof(clks_config_theme_value), "default");

    data = (const char *)clks_fs_read_all(CLKS_CONFIG_THEME_PATH, &size);
    if (data == CLKS_NULL || size == 0ULL) {
        return;
    }

    entry.key = "theme";
    entry.out_value = clks_config_theme_value;
    entry.out_size = sizeof(clks_config_theme_value);
    if (clks_rust_parse_key_values(data, size, &entry, 1ULL) == CLKS_FALSE || entry.found == CLKS_FALSE ||
        clks_config_theme_value[0] == '\0') {
        (void)clks_rust_trim_copy(data, size, clks_config_theme_value, sizeof(clks_config_theme_value));
    }

    if (clks_config_theme_value[0] == '\0') {
        clks_config_copy(clks_config_theme_value, sizeof(clks_config_theme_value), "default");
    }
}

static void clks_config_read_startup(void) {
    const char *data;
    u64 size = 0ULL;
    clks_rust_kv_entry entries[2];

    clks_config_startup_command_value[0] = '\0';

    data = (const char *)clks_fs_read_all(CLKS_CONFIG_STARTUP_PATH, &size);
    if (data == CLKS_NULL || size == 0ULL) {
        return;
    }

    entries[0].key = "command";
    entries[0].out_value = clks_config_startup_command_value;
    entries[0].out_size = sizeof(clks_config_startup_command);
    entries[1].key = "startup";
    entries[1].out_value = clks_config_startup_command_value;
    entries[1].out_size = sizeof(clks_config_startup_command);

    if (clks_rust_parse_key_values(data, size, entries, 2ULL) == CLKS_FALSE ||
        (entries[0].found == CLKS_FALSE && entries[1].found == CLKS_FALSE)) {
        (void)clks_rust_trim_copy(data, size, clks_config_startup_command_value, sizeof(clks_config_startup_command));
    }
}

void clks_config_init(void) {
    clks_config_read_theme();
    clks_config_read_startup();

    clks_log(CLKS_LOG_INFO, "CFG", "SYSTEM CONFIG CACHE READY");
    clks_log(CLKS_LOG_INFO, "CFG", clks_config_theme_value);
    if (clks_config_startup_command_value[0] != '\0') {
        clks_log(CLKS_LOG_INFO, "CFG", "STARTUP COMMAND CONFIGURED");
    }
}

const char *clks_config_theme(void) {
    return clks_config_theme_value;
}

const char *clks_config_startup_command(void) {
    return clks_config_startup_command_value;
}

