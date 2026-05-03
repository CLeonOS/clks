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
    case CLKS_SYSCALL_DISK_FSCK_FAT32:
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

    clks_syscall_usc_popup_fill_rect(pixels, width, height, 0U, 0U, width, height, 0x00001024UL);
    clks_syscall_usc_popup_fill_rect(pixels, width, height, 6U, 6U, width - 12U, height - 12U, 0x00002048UL);
    clks_syscall_usc_popup_draw_border(pixels, width, height, 0U, 0U, width, height, 0x0000D7FFUL);
    clks_syscall_usc_popup_draw_border(pixels, width, height, 4U, 4U, width - 8U, height - 8U, 0x000078D7UL);
    clks_syscall_usc_popup_fill_rect(pixels, width, height, 12U, 10U, width - 24U, 20U, 0x00000080UL);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 20U, 16U, width - 20U, " USER SAFE CONTROLLER ",
                                     0x00FFFFFFUL, 1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 18U, 48U, width - 18U,
                                     "DANGEROUS SYSCALL REQUEST", 0x00FFFF66UL, 1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 18U, 74U, width - 18U, "APP:", 0x0080FFFFUL, 1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 58U, 74U, width - 18U, app_path, 0x00FFFFFFUL, 1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 18U, 100U, width - 18U, syscall_line, 0x00FFFFFFUL, 1U);
    clks_syscall_usc_popup_draw_text(pixels, width, height, 18U, 128U, width - 18U,
                                     "SELECT: 1 ONCE  2 SESSION  3 PERMANENT  N DENY", 0x00C0C0C0UL, 1U);
    clks_syscall_usc_popup_fill_rect(pixels, width, height, 18U, 154U, width - 36U, 1U, 0x000078D7UL);

    clks_syscall_usc_popup_draw_button(pixels, width, height, &buttons[0], "1 ONCE", 0x00002070UL, 0x0000D7FFUL,
                                       0x00FFFFFFUL);
    clks_syscall_usc_popup_draw_button(pixels, width, height, &buttons[1], "2 SESSION", 0x00002070UL, 0x0000D7FFUL,
                                       0x00FFFFFFUL);
    clks_syscall_usc_popup_draw_button(pixels, width, height, &buttons[2], "3 PERM", 0x00002070UL, 0x0000D7FFUL,
                                       0x00FFFFFFUL);
    clks_syscall_usc_popup_draw_button(pixels, width, height, &buttons[3], "N DENY", 0x00600018UL, 0x00FF6060UL,
                                       0x00FFFFFFUL);
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

static clks_bool clks_syscall_usc_prompt_framebuffer_tui(const char *app_path, const char *name, u64 id,
                                                         u32 tty_index,
                                                         enum clks_syscall_usc_decision *out_decision) {
    struct clks_framebuffer_info fb;
    u32 *pixels;
    u32 *saved;
    u32 x;
    u32 y;
    u32 row;
    u32 col;
    clks_bool decided = CLKS_FALSE;
    enum clks_syscall_usc_decision decision = CLKS_SYSCALL_USC_DECISION_DENY;

    if (out_decision == CLKS_NULL || clks_fb_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    fb = clks_fb_info();
    if (fb.bpp != 32U || fb.width < CLKS_SYSCALL_USC_POPUP_WIDTH || fb.height < CLKS_SYSCALL_USC_POPUP_HEIGHT) {
        return CLKS_FALSE;
    }

    pixels = (u32 *)clks_kmalloc((usize)(CLKS_SYSCALL_USC_POPUP_WIDTH * CLKS_SYSCALL_USC_POPUP_HEIGHT * 4U));
    if (pixels == CLKS_NULL) {
        return CLKS_FALSE;
    }

    saved = (u32 *)clks_kmalloc((usize)(CLKS_SYSCALL_USC_POPUP_WIDTH * CLKS_SYSCALL_USC_POPUP_HEIGHT * 4U));
    if (saved == CLKS_NULL) {
        clks_kfree(pixels);
        return CLKS_FALSE;
    }

    x = (fb.width - CLKS_SYSCALL_USC_POPUP_WIDTH) / 2U;
    y = (fb.height - CLKS_SYSCALL_USC_POPUP_HEIGHT) / 2U;

    for (row = 0U; row < CLKS_SYSCALL_USC_POPUP_HEIGHT; row++) {
        for (col = 0U; col < CLKS_SYSCALL_USC_POPUP_WIDTH; col++) {
            u32 rgb = 0U;
            (void)clks_fb_read_pixel(x + col, y + row, &rgb);
            saved[((u64)row * CLKS_SYSCALL_USC_POPUP_WIDTH) + col] = rgb;
        }
    }

    clks_syscall_usc_popup_draw(pixels, CLKS_SYSCALL_USC_POPUP_WIDTH, CLKS_SYSCALL_USC_POPUP_HEIGHT, app_path, name,
                                id);
    clks_fb_blit_rgba((i32)x, (i32)y, pixels, CLKS_SYSCALL_USC_POPUP_WIDTH, CLKS_SYSCALL_USC_POPUP_HEIGHT,
                      CLKS_SYSCALL_USC_POPUP_WIDTH * 4U);

    while (decided == CLKS_FALSE) {
        char ch = '\0';

        if (clks_keyboard_pop_char_for_tty(tty_index, &ch) == CLKS_TRUE) {
            if (clks_syscall_usc_decision_from_key(ch, &decision) == CLKS_TRUE) {
                decided = CLKS_TRUE;
                break;
            }
        }

        clks_syscall_usc_sleep_until_input();
    }

    clks_fb_blit_rgba((i32)x, (i32)y, saved, CLKS_SYSCALL_USC_POPUP_WIDTH, CLKS_SYSCALL_USC_POPUP_HEIGHT,
                      CLKS_SYSCALL_USC_POPUP_WIDTH * 4U);
    clks_kfree(saved);
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
#if !defined(CLKS_CFG_KEYBOARD) || (CLKS_CFG_KEYBOARD == 0)
    (void)app_path;
    (void)id;
    (void)arg0;
    (void)arg1;
    (void)arg2;
    clks_syscall_usc_emit_text_line("BLOCK", "keyboard disabled, cannot prompt");
    return CLKS_SYSCALL_USC_DECISION_DENY;
#else
    const char *name = clks_syscall_usc_syscall_name(id);
    u32 tty_index = clks_exec_current_tty();

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

    {
        enum clks_syscall_usc_decision tui_decision;

        if (clks_syscall_usc_prompt_framebuffer_tui(app_path, name, id, tty_index, &tui_decision) == CLKS_TRUE) {
            return tui_decision;
        }
    }

    clks_syscall_usc_emit_text_line("BLOCK", "screen prompt unavailable");
    return CLKS_SYSCALL_USC_DECISION_DENY;
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
