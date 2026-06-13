#ifndef CLKS_RUST_H
#define CLKS_RUST_H

#include <clks/types.h>

typedef struct clks_rust_kv_entry {
    const char *key;
    char *out_value;
    usize out_size;
    clks_bool found;
    clks_bool truncated;
} clks_rust_kv_entry;

typedef struct clks_rust_utf8_state {
    u32 codepoint;
    u8 remaining;
    u8 expected;
} clks_rust_utf8_state;

typedef struct clks_rust_syscall_meta {
    u64 id;
    const char *name;
    u32 category;
    u8 argc;
    u32 flags;
    u32 usc_gate;
} clks_rust_syscall_meta;

#define CLKS_RUST_UTF8_NEED_MORE 0U
#define CLKS_RUST_UTF8_CODEPOINT 1U
#define CLKS_RUST_UTF8_REPLACEMENT 2U

#define CLKS_RUST_PATH_POLICY_USER_DB 0x00000001U
#define CLKS_RUST_PATH_POLICY_TEMP 0x00000002U
#define CLKS_RUST_PATH_POLICY_HOME 0x00000004U
#define CLKS_RUST_PATH_POLICY_HOME_ROOT 0x00000008U
#define CLKS_RUST_PATH_POLICY_UNDER_HOME_ROOT 0x00000010U
#define CLKS_RUST_PATH_POLICY_SYSTEM 0x00000020U
#define CLKS_RUST_PATH_POLICY_SYSTEM_DRIVERS 0x00000040U
#define CLKS_RUST_PATH_POLICY_USER_PROGRAM 0x00000080U

clks_bool clks_rust_trim_copy(const char *data, u64 data_len, char *out, usize out_size);
clks_bool clks_rust_parse_key_values(const char *data, u64 data_len, clks_rust_kv_entry *entries, u64 entry_count);
clks_bool clks_rust_cmdline_flag_enabled(const char *cmdline, const char *name);
clks_bool clks_rust_cmdline_get_value(const char *cmdline, const char *name, char *out, usize out_size);

u64 clks_rust_syscall_meta_max_id(void);
clks_bool clks_rust_syscall_meta_fill(u64 id, clks_rust_syscall_meta *out);

clks_bool clks_rust_path_normalize_absolute(const char *path, char *out, usize out_size);
clks_bool clks_rust_path_normalize_relative(const char *path, char *out, usize out_size);
clks_bool clks_rust_path_normalize_external_internal(const char *path, char *out, usize out_size);
clks_bool clks_rust_path_at_or_under(const char *path, const char *prefix);
clks_bool clks_rust_path_has_prefix(const char *path, const char *prefix);
clks_bool clks_rust_path_is_user_program(const char *path);
u32 clks_rust_path_policy_flags(const char *path, const char *home);

void clks_rust_klog_init(void);
clks_bool clks_rust_klog_push(u32 level, const char *tag, const char *line);
u64 clks_rust_klog_count(void);
clks_bool clks_rust_klog_read(u64 index_from_oldest, char *out_line, usize out_line_size);
u64 clks_rust_klog_count_filtered(u32 min_level, const char *tag);
clks_bool clks_rust_klog_read_filtered(u32 min_level, const char *tag, u64 index_from_oldest_matching,
                                       char *out_line, usize out_line_size);

void clks_rust_utf8_state_reset(clks_rust_utf8_state *state);
u32 clks_rust_utf8_decode_byte(clks_rust_utf8_state *state, u8 byte, u32 *out_codepoint);
clks_bool clks_rust_utf8_next(const char *text, u64 text_len, u64 *io_index, u32 *out_codepoint);
clks_bool clks_rust_utf8_next_strict(const char *text, u64 text_len, u64 *io_index, u32 *out_codepoint);
u32 clks_rust_unicode_width(u32 codepoint);

#endif
