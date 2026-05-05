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
    if (buf[0] == '[' && buf[1] == 'D' && buf[2] == 'R' && buf[3] == 'I' && buf[4] == 'V' && buf[5] == 'E' &&
        buf[6] == 'R' && buf[7] == ']') {
        return len;
    }

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

static u64 clks_syscall_display_info(u64 arg0, u64 arg1) {
    u32 target = (u32)arg0;
    struct clks_syscall_display_info_user *out_info = (struct clks_syscall_display_info_user *)(usize)arg1;
    struct clks_display_mode mode;

    if (arg1 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(*out_info)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_display_mode_get(target, &mode) == CLKS_FALSE) {
        return 0ULL;
    }

    out_info->target = (u64)target;
    out_info->physical_width = (u64)mode.physical_width;
    out_info->physical_height = (u64)mode.physical_height;
    out_info->logical_width = (u64)mode.logical_width;
    out_info->logical_height = (u64)mode.logical_height;
    return 1ULL;
}

static u64 clks_syscall_display_set_mode(u64 arg0) {
    struct clks_syscall_display_set_mode_req req;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.logical_width > 0xFFFFFFFFULL || req.logical_height > 0xFFFFFFFFULL) {
        return 0ULL;
    }

    if (req.target == CLKS_DISPLAY_TARGET_TTY) {
        return (clks_tty_set_resolution((u32)req.logical_width, (u32)req.logical_height) == CLKS_TRUE) ? 1ULL : 0ULL;
    }

    if (req.target == CLKS_DISPLAY_TARGET_WM) {
        return (clks_wm_set_resolution((u32)req.logical_width, (u32)req.logical_height) == CLKS_TRUE) ? 1ULL : 0ULL;
    }

    return 0ULL;
}

static u64 clks_syscall_tty_grid_info(u64 arg0) {
    struct clks_syscall_tty_grid_info_user *out_info = (struct clks_syscall_tty_grid_info_user *)(usize)arg0;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg0, (u64)sizeof(*out_info)) == CLKS_FALSE) {
        return 0ULL;
    }

    out_info->cols = (u64)clks_tty_text_cols();
    out_info->rows = (u64)clks_tty_text_rows();
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
