#include <clks/exec.h>
#include <clks/fs.h>
#include <clks/keyboard.h>
#include <clks/log.h>
#include <clks/shell.h>
#include <clks/string.h>
#include <clks/tty.h>
#include <clks/types.h>

#define CLKS_SHELL_LINE_MAX       192U
#define CLKS_SHELL_CMD_MAX         32U
#define CLKS_SHELL_ARG_MAX        160U
#define CLKS_SHELL_NAME_MAX        96U
#define CLKS_SHELL_PATH_MAX       192U
#define CLKS_SHELL_CAT_LIMIT      512U
#define CLKS_SHELL_INPUT_BUDGET   128U
#define CLKS_SHELL_CLEAR_LINES     56U
#define CLKS_SHELL_HISTORY_MAX     16U
#define CLKS_SHELL_PROMPT_TEXT "cleonos> "

static clks_bool clks_shell_ready = CLKS_FALSE;
static char clks_shell_line[CLKS_SHELL_LINE_MAX];
static usize clks_shell_line_len = 0U;
static usize clks_shell_cursor = 0U;
static usize clks_shell_rendered_len = 0U;
static char clks_shell_cwd[CLKS_SHELL_PATH_MAX] = "/";

static char clks_shell_history[CLKS_SHELL_HISTORY_MAX][CLKS_SHELL_LINE_MAX];
static u32 clks_shell_history_count = 0U;
static i32 clks_shell_history_nav = -1;
static char clks_shell_nav_saved_line[CLKS_SHELL_LINE_MAX];
static usize clks_shell_nav_saved_len = 0U;
static usize clks_shell_nav_saved_cursor = 0U;

static clks_bool clks_shell_is_space(char ch) {
    return (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_shell_is_printable(char ch) {
    return (ch >= 32 && ch <= 126) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_shell_streq(const char *left, const char *right) {
    return (clks_strcmp(left, right) == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static void clks_shell_write(const char *text) {
    if (text == CLKS_NULL) {
        return;
    }

    clks_tty_write(text);
}

static void clks_shell_write_char(char ch) {
    clks_tty_write_char(ch);
}

static void clks_shell_writeln(const char *text) {
    clks_shell_write(text);
    clks_shell_write_char('\n');
}

static void clks_shell_prompt(void) {
    clks_shell_write(CLKS_SHELL_PROMPT_TEXT);
}

static void clks_shell_copy_line(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || src == CLKS_NULL || dst_size == 0U) {
        return;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void clks_shell_history_cancel_nav(void) {
    clks_shell_history_nav = -1;
    clks_shell_nav_saved_len = 0U;
    clks_shell_nav_saved_cursor = 0U;
    clks_shell_nav_saved_line[0] = '\0';
}

static void clks_shell_reset_line(void) {
    clks_shell_line_len = 0U;
    clks_shell_cursor = 0U;
    clks_shell_rendered_len = 0U;
    clks_shell_line[0] = '\0';
}

static void clks_shell_load_line(const char *line) {
    if (line == CLKS_NULL) {
        clks_shell_reset_line();
        return;
    }

    clks_shell_copy_line(clks_shell_line, sizeof(clks_shell_line), line);
    clks_shell_line_len = clks_strlen(clks_shell_line);
    clks_shell_cursor = clks_shell_line_len;
}

static void clks_shell_render_line(void) {
    usize i;

    if (clks_shell_ready == CLKS_FALSE) {
        return;
    }

    clks_shell_write_char('\r');
    clks_shell_prompt();

    for (i = 0U; i < clks_shell_line_len; i++) {
        clks_shell_write_char(clks_shell_line[i]);
    }

    for (i = clks_shell_line_len; i < clks_shell_rendered_len; i++) {
        clks_shell_write_char(' ');
    }

    clks_shell_write_char('\r');
    clks_shell_prompt();

    for (i = 0U; i < clks_shell_cursor; i++) {
        clks_shell_write_char(clks_shell_line[i]);
    }

    clks_shell_rendered_len = clks_shell_line_len;
}

static clks_bool clks_shell_line_has_non_space(const char *line) {
    usize i = 0U;

    if (line == CLKS_NULL) {
        return CLKS_FALSE;
    }

    while (line[i] != '\0') {
        if (clks_shell_is_space(line[i]) == CLKS_FALSE) {
            return CLKS_TRUE;
        }

        i++;
    }

    return CLKS_FALSE;
}

static void clks_shell_history_push(const char *line) {
    if (clks_shell_line_has_non_space(line) == CLKS_FALSE) {
        clks_shell_history_cancel_nav();
        return;
    }

    if (clks_shell_history_count > 0U &&
        clks_strcmp(clks_shell_history[clks_shell_history_count - 1U], line) == 0) {
        clks_shell_history_cancel_nav();
        return;
    }

    if (clks_shell_history_count < CLKS_SHELL_HISTORY_MAX) {
        clks_shell_copy_line(
            clks_shell_history[clks_shell_history_count],
            sizeof(clks_shell_history[clks_shell_history_count]),
            line
        );
        clks_shell_history_count++;
    } else {
        u32 i;

        for (i = 1U; i < CLKS_SHELL_HISTORY_MAX; i++) {
            clks_memcpy(
                clks_shell_history[i - 1U],
                clks_shell_history[i],
                CLKS_SHELL_LINE_MAX
            );
        }

        clks_shell_copy_line(
            clks_shell_history[CLKS_SHELL_HISTORY_MAX - 1U],
            sizeof(clks_shell_history[CLKS_SHELL_HISTORY_MAX - 1U]),
            line
        );
    }

    clks_shell_history_cancel_nav();
}

static void clks_shell_history_apply_current(void) {
    if (clks_shell_history_nav >= 0) {
        clks_shell_load_line(clks_shell_history[(u32)clks_shell_history_nav]);
    } else {
        clks_shell_copy_line(clks_shell_line, sizeof(clks_shell_line), clks_shell_nav_saved_line);
        clks_shell_line_len = clks_shell_nav_saved_len;
        if (clks_shell_line_len > CLKS_SHELL_LINE_MAX - 1U) {
            clks_shell_line_len = CLKS_SHELL_LINE_MAX - 1U;
            clks_shell_line[clks_shell_line_len] = '\0';
        }
        clks_shell_cursor = clks_shell_nav_saved_cursor;
        if (clks_shell_cursor > clks_shell_line_len) {
            clks_shell_cursor = clks_shell_line_len;
        }
    }

    clks_shell_render_line();
}

static void clks_shell_history_up(void) {
    if (clks_shell_history_count == 0U) {
        return;
    }

    if (clks_shell_history_nav < 0) {
        clks_shell_copy_line(clks_shell_nav_saved_line, sizeof(clks_shell_nav_saved_line), clks_shell_line);
        clks_shell_nav_saved_len = clks_shell_line_len;
        clks_shell_nav_saved_cursor = clks_shell_cursor;
        clks_shell_history_nav = (i32)clks_shell_history_count - 1;
    } else if (clks_shell_history_nav > 0) {
        clks_shell_history_nav--;
    }

    clks_shell_history_apply_current();
}

static void clks_shell_history_down(void) {
    if (clks_shell_history_nav < 0) {
        return;
    }

    if ((u32)clks_shell_history_nav + 1U < clks_shell_history_count) {
        clks_shell_history_nav++;
    } else {
        clks_shell_history_nav = -1;
    }

    clks_shell_history_apply_current();
}

static void clks_shell_trim(char *line) {
    usize start = 0U;
    usize i = 0U;
    usize len;

    if (line == CLKS_NULL) {
        return;
    }

    while (line[start] != '\0' && clks_shell_is_space(line[start]) == CLKS_TRUE) {
        start++;
    }

    if (start > 0U) {
        while (line[start + i] != '\0') {
            line[i] = line[start + i];
            i++;
        }

        line[i] = '\0';
    }

    len = clks_strlen(line);

    while (len > 0U && clks_shell_is_space(line[len - 1U]) == CLKS_TRUE) {
        line[len - 1U] = '\0';
        len--;
    }
}

static void clks_shell_split_line(const char *line,
                                  char *out_cmd,
                                  usize out_cmd_size,
                                  char *out_arg,
                                  usize out_arg_size) {
    usize i = 0U;
    usize cmd_pos = 0U;
    usize arg_pos = 0U;

    if (line == CLKS_NULL || out_cmd == CLKS_NULL || out_arg == CLKS_NULL) {
        return;
    }

    out_cmd[0] = '\0';
    out_arg[0] = '\0';

    while (line[i] != '\0' && clks_shell_is_space(line[i]) == CLKS_TRUE) {
        i++;
    }

    while (line[i] != '\0' && clks_shell_is_space(line[i]) == CLKS_FALSE) {
        if (cmd_pos + 1U < out_cmd_size) {
            out_cmd[cmd_pos++] = line[i];
        }
        i++;
    }

    out_cmd[cmd_pos] = '\0';

    while (line[i] != '\0' && clks_shell_is_space(line[i]) == CLKS_TRUE) {
        i++;
    }

    while (line[i] != '\0') {
        if (arg_pos + 1U < out_arg_size) {
            out_arg[arg_pos++] = line[i];
        }
        i++;
    }

    out_arg[arg_pos] = '\0';
}

static clks_bool clks_shell_has_suffix(const char *name, const char *suffix) {
    usize name_len;
    usize suffix_len;

    if (name == CLKS_NULL || suffix == CLKS_NULL) {
        return CLKS_FALSE;
    }

    name_len = clks_strlen(name);
    suffix_len = clks_strlen(suffix);

    if (suffix_len > name_len) {
        return CLKS_FALSE;
    }

    return (clks_strcmp(name + (name_len - suffix_len), suffix) == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_shell_resolve_exec_path(const char *arg, char *out_path, usize out_path_size) {
    usize cursor = 0U;
    usize i;

    if (arg == CLKS_NULL || out_path == CLKS_NULL || out_path_size == 0U) {
        return CLKS_FALSE;
    }

    if (arg[0] == '\0') {
        return CLKS_FALSE;
    }

    out_path[0] = '\0';

    if (arg[0] == '/') {
        usize len = clks_strlen(arg);
        if (len + 1U > out_path_size) {
            return CLKS_FALSE;
        }
        clks_memcpy(out_path, arg, len + 1U);
        return CLKS_TRUE;
    }

    {
        static const char shell_prefix[] = "/shell/";
        usize prefix_len = (sizeof(shell_prefix) - 1U);

        if (prefix_len + 1U >= out_path_size) {
            return CLKS_FALSE;
        }

        clks_memcpy(out_path, shell_prefix, prefix_len);
        cursor = prefix_len;
    }

    for (i = 0U; arg[i] != '\0'; i++) {
        if (cursor + 1U >= out_path_size) {
            return CLKS_FALSE;
        }

        out_path[cursor++] = arg[i];
    }

    out_path[cursor] = '\0';

    if (clks_shell_has_suffix(out_path, ".elf") == CLKS_FALSE) {
        static const char elf_suffix[] = ".elf";
        usize suffix_len = (sizeof(elf_suffix) - 1U);

        if (cursor + suffix_len + 1U > out_path_size) {
            return CLKS_FALSE;
        }

        clks_memcpy(out_path + cursor, elf_suffix, suffix_len + 1U);
    }

    return CLKS_TRUE;
}

static clks_bool clks_shell_path_push_component(char *path, usize path_size, usize *io_len, const char *component, usize comp_len) {
    if (path == CLKS_NULL || io_len == CLKS_NULL || component == CLKS_NULL || comp_len == 0U) {
        return CLKS_FALSE;
    }

    if (*io_len == 1U) {
        if (*io_len + comp_len >= path_size) {
            return CLKS_FALSE;
        }

        clks_memcpy(path + 1U, component, comp_len);
        *io_len = 1U + comp_len;
        path[*io_len] = '\0';
        return CLKS_TRUE;
    }

    if (*io_len + 1U + comp_len >= path_size) {
        return CLKS_FALSE;
    }

    path[*io_len] = '/';
    clks_memcpy(path + *io_len + 1U, component, comp_len);
    *io_len += (1U + comp_len);
    path[*io_len] = '\0';
    return CLKS_TRUE;
}

static void clks_shell_path_pop_component(char *path, usize *io_len) {
    if (path == CLKS_NULL || io_len == CLKS_NULL) {
        return;
    }

    if (*io_len <= 1U) {
        path[0] = '/';
        path[1] = '\0';
        *io_len = 1U;
        return;
    }

    while (*io_len > 1U && path[*io_len - 1U] != '/') {
        (*io_len)--;
    }

    if (*io_len > 1U) {
        (*io_len)--;
    }

    path[*io_len] = '\0';
}

static clks_bool clks_shell_path_parse_into(const char *src, char *out_path, usize out_size, usize *io_len) {
    usize i = 0U;

    if (src == CLKS_NULL || out_path == CLKS_NULL || io_len == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (src[0] == '/') {
        i = 1U;
    }

    while (src[i] != '\0') {
        usize start;
        usize len;

        while (src[i] == '/') {
            i++;
        }

        if (src[i] == '\0') {
            break;
        }

        start = i;

        while (src[i] != '\0' && src[i] != '/') {
            i++;
        }

        len = i - start;

        if (len == 1U && src[start] == '.') {
            continue;
        }

        if (len == 2U && src[start] == '.' && src[start + 1U] == '.') {
            clks_shell_path_pop_component(out_path, io_len);
            continue;
        }

        if (clks_shell_path_push_component(out_path, out_size, io_len, src + start, len) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static clks_bool clks_shell_resolve_path(const char *arg, char *out_path, usize out_size) {
    usize len = 1U;

    if (out_path == CLKS_NULL || out_size < 2U) {
        return CLKS_FALSE;
    }

    out_path[0] = '/';
    out_path[1] = '\0';

    if (arg == CLKS_NULL || arg[0] == '\0') {
        return clks_shell_path_parse_into(clks_shell_cwd, out_path, out_size, &len);
    }

    if (arg[0] != '/') {
        if (clks_shell_path_parse_into(clks_shell_cwd, out_path, out_size, &len) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return clks_shell_path_parse_into(arg, out_path, out_size, &len);
}

static clks_bool clks_shell_split_first_and_rest(const char *arg,
                                                 char *out_first,
                                                 usize out_first_size,
                                                 const char **out_rest) {
    usize i = 0U;
    usize p = 0U;

    if (arg == CLKS_NULL || out_first == CLKS_NULL || out_first_size == 0U || out_rest == CLKS_NULL) {
        return CLKS_FALSE;
    }

    out_first[0] = '\0';
    *out_rest = "";

    while (arg[i] != '\0' && clks_shell_is_space(arg[i]) == CLKS_TRUE) {
        i++;
    }

    if (arg[i] == '\0') {
        return CLKS_FALSE;
    }

    while (arg[i] != '\0' && clks_shell_is_space(arg[i]) == CLKS_FALSE) {
        if (p + 1U < out_first_size) {
            out_first[p++] = arg[i];
        }
        i++;
    }

    out_first[p] = '\0';

    while (arg[i] != '\0' && clks_shell_is_space(arg[i]) == CLKS_TRUE) {
        i++;
    }

    *out_rest = &arg[i];
    return CLKS_TRUE;
}

static void clks_shell_cmd_help(void) {
    clks_shell_writeln("commands:");
    clks_shell_writeln("  help");
    clks_shell_writeln("  ls [dir]");
    clks_shell_writeln("  cat <file>");
    clks_shell_writeln("  pwd");
    clks_shell_writeln("  cd [dir]");
    clks_shell_writeln("  mkdir <dir>      (/temp only)");
    clks_shell_writeln("  touch <file>     (/temp only)");
    clks_shell_writeln("  write <file> <text>   (/temp only)");
    clks_shell_writeln("  append <file> <text>  (/temp only)");
    clks_shell_writeln("  rm <path>        (/temp only)");
    clks_shell_writeln("  exec <path|name>");
    clks_shell_writeln("  clear");
    clks_shell_writeln("  kbdstat");
    clks_shell_writeln("edit keys: Left/Right, Home/End, Up/Down history");
}

static void clks_shell_cmd_ls(const char *arg) {
    struct clks_fs_node_info info;
    char path[CLKS_SHELL_PATH_MAX];
    const char *target = arg;
    u64 count;
    u64 i;

    if (target == CLKS_NULL || target[0] == '\0') {
        target = ".";
    }

    if (clks_shell_resolve_path(target, path, sizeof(path)) == CLKS_FALSE) {
        clks_shell_writeln("ls: invalid path");
        return;
    }

    if (clks_fs_stat(path, &info) == CLKS_FALSE || info.type != CLKS_FS_NODE_DIR) {
        clks_shell_writeln("ls: directory not found");
        return;
    }

    count = clks_fs_count_children(path);

    if (count == 0ULL) {
        clks_shell_writeln("(empty)");
        return;
    }

    for (i = 0ULL; i < count; i++) {
        char name[CLKS_SHELL_NAME_MAX];

        name[0] = '\0';
        if (clks_fs_get_child_name(path, i, name, sizeof(name)) == CLKS_FALSE) {
            continue;
        }

        clks_shell_writeln(name);
    }
}

static void clks_shell_cmd_cat(const char *arg) {
    const void *data;
    u64 size = 0ULL;
    u64 copy_len;
    char path[CLKS_SHELL_PATH_MAX];
    char out[CLKS_SHELL_CAT_LIMIT + 1U];

    if (arg == CLKS_NULL || arg[0] == '\0') {
        clks_shell_writeln("cat: file path required");
        return;
    }

    if (clks_shell_resolve_path(arg, path, sizeof(path)) == CLKS_FALSE) {
        clks_shell_writeln("cat: invalid path");
        return;
    }

    data = clks_fs_read_all(path, &size);

    if (data == CLKS_NULL) {
        clks_shell_writeln("cat: file not found");
        return;
    }

    if (size == 0ULL) {
        clks_shell_writeln("(empty file)");
        return;
    }

    copy_len = (size < CLKS_SHELL_CAT_LIMIT) ? size : CLKS_SHELL_CAT_LIMIT;
    clks_memcpy(out, data, (usize)copy_len);
    out[copy_len] = '\0';

    clks_shell_writeln(out);

    if (size > copy_len) {
        clks_shell_writeln("[cat] output truncated");
    }
}

static void clks_shell_cmd_pwd(void) {
    clks_shell_writeln(clks_shell_cwd);
}

static void clks_shell_cmd_cd(const char *arg) {
    struct clks_fs_node_info info;
    char path[CLKS_SHELL_PATH_MAX];
    const char *target = arg;

    if (target == CLKS_NULL || target[0] == '\0') {
        target = "/";
    }

    if (clks_shell_resolve_path(target, path, sizeof(path)) == CLKS_FALSE) {
        clks_shell_writeln("cd: invalid path");
        return;
    }

    if (clks_fs_stat(path, &info) == CLKS_FALSE || info.type != CLKS_FS_NODE_DIR) {
        clks_shell_writeln("cd: directory not found");
        return;
    }

    clks_shell_copy_line(clks_shell_cwd, sizeof(clks_shell_cwd), path);
}

static void clks_shell_cmd_mkdir(const char *arg) {
    char path[CLKS_SHELL_PATH_MAX];

    if (arg == CLKS_NULL || arg[0] == '\0') {
        clks_shell_writeln("mkdir: directory path required");
        return;
    }

    if (clks_shell_resolve_path(arg, path, sizeof(path)) == CLKS_FALSE) {
        clks_shell_writeln("mkdir: invalid path");
        return;
    }

    if (clks_fs_mkdir(path) == CLKS_FALSE) {
        clks_shell_writeln("mkdir: failed (only /temp writable)");
    }
}

static void clks_shell_cmd_touch(const char *arg) {
    static const char empty_data[1] = {'\0'};
    char path[CLKS_SHELL_PATH_MAX];

    if (arg == CLKS_NULL || arg[0] == '\0') {
        clks_shell_writeln("touch: file path required");
        return;
    }

    if (clks_shell_resolve_path(arg, path, sizeof(path)) == CLKS_FALSE) {
        clks_shell_writeln("touch: invalid path");
        return;
    }

    if (clks_fs_write_all(path, empty_data, 0ULL) == CLKS_FALSE) {
        clks_shell_writeln("touch: failed (only /temp writable)");
    }
}

static void clks_shell_cmd_write(const char *arg) {
    char path_arg[CLKS_SHELL_PATH_MAX];
    char abs_path[CLKS_SHELL_PATH_MAX];
    const char *payload;
    u64 payload_len;

    if (arg == CLKS_NULL || arg[0] == '\0') {
        clks_shell_writeln("write: usage write <file> <text>");
        return;
    }

    if (clks_shell_split_first_and_rest(arg, path_arg, sizeof(path_arg), &payload) == CLKS_FALSE) {
        clks_shell_writeln("write: usage write <file> <text>");
        return;
    }

    if (clks_shell_resolve_path(path_arg, abs_path, sizeof(abs_path)) == CLKS_FALSE) {
        clks_shell_writeln("write: invalid path");
        return;
    }

    payload_len = clks_strlen(payload);

    if (clks_fs_write_all(abs_path, payload, payload_len) == CLKS_FALSE) {
        clks_shell_writeln("write: failed (only /temp writable)");
    }
}

static void clks_shell_cmd_append(const char *arg) {
    char path_arg[CLKS_SHELL_PATH_MAX];
    char abs_path[CLKS_SHELL_PATH_MAX];
    const char *payload;
    u64 payload_len;

    if (arg == CLKS_NULL || arg[0] == '\0') {
        clks_shell_writeln("append: usage append <file> <text>");
        return;
    }

    if (clks_shell_split_first_and_rest(arg, path_arg, sizeof(path_arg), &payload) == CLKS_FALSE) {
        clks_shell_writeln("append: usage append <file> <text>");
        return;
    }

    if (clks_shell_resolve_path(path_arg, abs_path, sizeof(abs_path)) == CLKS_FALSE) {
        clks_shell_writeln("append: invalid path");
        return;
    }

    payload_len = clks_strlen(payload);

    if (clks_fs_append(abs_path, payload, payload_len) == CLKS_FALSE) {
        clks_shell_writeln("append: failed (only /temp writable)");
    }
}

static void clks_shell_cmd_rm(const char *arg) {
    char path[CLKS_SHELL_PATH_MAX];

    if (arg == CLKS_NULL || arg[0] == '\0') {
        clks_shell_writeln("rm: path required");
        return;
    }

    if (clks_shell_resolve_path(arg, path, sizeof(path)) == CLKS_FALSE) {
        clks_shell_writeln("rm: invalid path");
        return;
    }

    if (clks_fs_remove(path) == CLKS_FALSE) {
        clks_shell_writeln("rm: failed (only /temp writable; dir must be empty)");
    }
}

static void clks_shell_cmd_exec(const char *arg) {
    char path[CLKS_SHELL_PATH_MAX];
    u64 status = (u64)-1;

    if (clks_shell_resolve_exec_path(arg, path, sizeof(path)) == CLKS_FALSE) {
        clks_shell_writeln("exec: invalid target");
        return;
    }

    if (clks_exec_run_path(path, &status) == CLKS_TRUE && status == 0ULL) {
        clks_shell_writeln("exec: request accepted");
    } else {
        clks_shell_writeln("exec: request failed");
    }
}

static void clks_shell_cmd_clear(void) {
    u32 i;

    for (i = 0U; i < CLKS_SHELL_CLEAR_LINES; i++) {
        clks_shell_write_char('\n');
    }
}

static void clks_shell_cmd_kbdstat(void) {
    clks_shell_writeln("kbd stats emitted to kernel log");
    clks_log_hex(CLKS_LOG_INFO, "KBD", "BUFFERED", clks_keyboard_buffered_count());
    clks_log_hex(CLKS_LOG_INFO, "KBD", "PUSHED", clks_keyboard_push_count());
    clks_log_hex(CLKS_LOG_INFO, "KBD", "POPPED", clks_keyboard_pop_count());
    clks_log_hex(CLKS_LOG_INFO, "KBD", "DROPPED", clks_keyboard_drop_count());
}

static void clks_shell_execute_line(const char *line) {
    char line_buf[CLKS_SHELL_LINE_MAX];
    char cmd[CLKS_SHELL_CMD_MAX];
    char arg[CLKS_SHELL_ARG_MAX];
    usize i = 0U;

    if (line == CLKS_NULL) {
        return;
    }

    while (line[i] != '\0' && i + 1U < sizeof(line_buf)) {
        line_buf[i] = line[i];
        i++;
    }

    line_buf[i] = '\0';
    clks_shell_trim(line_buf);

    if (line_buf[0] == '\0') {
        return;
    }

    clks_shell_split_line(line_buf, cmd, sizeof(cmd), arg, sizeof(arg));

    if (clks_shell_streq(cmd, "help") == CLKS_TRUE) {
        clks_shell_cmd_help();
        return;
    }

    if (clks_shell_streq(cmd, "ls") == CLKS_TRUE) {
        clks_shell_cmd_ls(arg);
        return;
    }

    if (clks_shell_streq(cmd, "cat") == CLKS_TRUE) {
        clks_shell_cmd_cat(arg);
        return;
    }

    if (clks_shell_streq(cmd, "pwd") == CLKS_TRUE) {
        clks_shell_cmd_pwd();
        return;
    }

    if (clks_shell_streq(cmd, "cd") == CLKS_TRUE) {
        clks_shell_cmd_cd(arg);
        return;
    }

    if (clks_shell_streq(cmd, "mkdir") == CLKS_TRUE) {
        clks_shell_cmd_mkdir(arg);
        return;
    }

    if (clks_shell_streq(cmd, "touch") == CLKS_TRUE) {
        clks_shell_cmd_touch(arg);
        return;
    }

    if (clks_shell_streq(cmd, "write") == CLKS_TRUE) {
        clks_shell_cmd_write(arg);
        return;
    }

    if (clks_shell_streq(cmd, "append") == CLKS_TRUE) {
        clks_shell_cmd_append(arg);
        return;
    }

    if (clks_shell_streq(cmd, "rm") == CLKS_TRUE) {
        clks_shell_cmd_rm(arg);
        return;
    }

    if (clks_shell_streq(cmd, "exec") == CLKS_TRUE || clks_shell_streq(cmd, "run") == CLKS_TRUE) {
        clks_shell_cmd_exec(arg);
        return;
    }

    if (clks_shell_streq(cmd, "clear") == CLKS_TRUE) {
        clks_shell_cmd_clear();
        return;
    }

    if (clks_shell_streq(cmd, "kbdstat") == CLKS_TRUE) {
        clks_shell_cmd_kbdstat();
        return;
    }

    clks_shell_writeln("unknown command; type 'help'");
}

static void clks_shell_handle_char(char ch) {
    if (ch == '\r') {
        return;
    }

    if (ch == '\n') {
        clks_shell_write_char('\n');
        clks_shell_line[clks_shell_line_len] = '\0';
        clks_shell_history_push(clks_shell_line);
        clks_shell_execute_line(clks_shell_line);
        clks_shell_reset_line();
        clks_shell_history_cancel_nav();
        clks_shell_prompt();
        return;
    }

    if (ch == CLKS_KEY_UP) {
        clks_shell_history_up();
        return;
    }

    if (ch == CLKS_KEY_DOWN) {
        clks_shell_history_down();
        return;
    }

    if (ch == CLKS_KEY_LEFT) {
        if (clks_shell_cursor > 0U) {
            clks_shell_cursor--;
            clks_shell_render_line();
        }
        return;
    }

    if (ch == CLKS_KEY_RIGHT) {
        if (clks_shell_cursor < clks_shell_line_len) {
            clks_shell_cursor++;
            clks_shell_render_line();
        }
        return;
    }

    if (ch == CLKS_KEY_HOME) {
        if (clks_shell_cursor != 0U) {
            clks_shell_cursor = 0U;
            clks_shell_render_line();
        }
        return;
    }

    if (ch == CLKS_KEY_END) {
        if (clks_shell_cursor != clks_shell_line_len) {
            clks_shell_cursor = clks_shell_line_len;
            clks_shell_render_line();
        }
        return;
    }

    if (ch == '\b') {
        if (clks_shell_cursor > 0U && clks_shell_line_len > 0U) {
            usize i;

            clks_shell_history_cancel_nav();

            for (i = clks_shell_cursor - 1U; i < clks_shell_line_len; i++) {
                clks_shell_line[i] = clks_shell_line[i + 1U];
            }

            clks_shell_line_len--;
            clks_shell_cursor--;
            clks_shell_render_line();
        }
        return;
    }

    if (ch == CLKS_KEY_DELETE) {
        if (clks_shell_cursor < clks_shell_line_len) {
            usize i;

            clks_shell_history_cancel_nav();

            for (i = clks_shell_cursor; i < clks_shell_line_len; i++) {
                clks_shell_line[i] = clks_shell_line[i + 1U];
            }

            clks_shell_line_len--;
            clks_shell_render_line();
        }
        return;
    }

    if (ch == '\t') {
        ch = ' ';
    }

    if (clks_shell_is_printable(ch) == CLKS_FALSE) {
        return;
    }

    if (clks_shell_line_len + 1U >= CLKS_SHELL_LINE_MAX) {
        return;
    }

    clks_shell_history_cancel_nav();

    if (clks_shell_cursor == clks_shell_line_len) {
        clks_shell_line[clks_shell_line_len++] = ch;
        clks_shell_line[clks_shell_line_len] = '\0';
        clks_shell_cursor = clks_shell_line_len;
        clks_shell_write_char(ch);
        clks_shell_rendered_len = clks_shell_line_len;
        return;
    }

    {
        usize i;

        for (i = clks_shell_line_len; i > clks_shell_cursor; i--) {
            clks_shell_line[i] = clks_shell_line[i - 1U];
        }

        clks_shell_line[clks_shell_cursor] = ch;
        clks_shell_line_len++;
        clks_shell_cursor++;
        clks_shell_line[clks_shell_line_len] = '\0';
        clks_shell_render_line();
    }
}

void clks_shell_init(void) {
    clks_shell_reset_line();
    clks_shell_history_count = 0U;
    clks_shell_history_cancel_nav();
    clks_shell_copy_line(clks_shell_cwd, sizeof(clks_shell_cwd), "/");

    if (clks_tty_ready() == CLKS_FALSE) {
        clks_shell_ready = CLKS_FALSE;
        clks_log(CLKS_LOG_WARN, "SHELL", "TTY NOT READY; SHELL DISABLED");
        return;
    }

    clks_shell_ready = CLKS_TRUE;

    clks_shell_writeln("");
    clks_shell_writeln("CLeonOS interactive shell ready");
    clks_shell_writeln("type 'help' for commands");
    clks_shell_writeln("/temp is writable in kernel shell mode");
    clks_shell_prompt();

    clks_log(CLKS_LOG_INFO, "SHELL", "INTERACTIVE LOOP ONLINE");
}

static void clks_shell_drain_input(u32 budget_limit) {
    u32 budget = 0U;
    char ch;

    if (budget_limit == 0U || clks_shell_ready == CLKS_FALSE) {
        return;
    }

    while (budget < budget_limit) {
        if (clks_keyboard_pop_char(&ch) == CLKS_FALSE) {
            break;
        }

        clks_shell_handle_char(ch);
        budget++;
    }
}

void clks_shell_pump_input(u32 max_chars) {
    clks_shell_drain_input(max_chars);
}

void clks_shell_tick(u64 tick) {
    (void)tick;
    clks_shell_drain_input(CLKS_SHELL_INPUT_BUDGET);
}