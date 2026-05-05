void clks_wm_init(void) {
    u32 i;

    if (clks_fb_ready() == CLKS_FALSE) {
        clks_wm_ready_flag = CLKS_FALSE;
        clks_log(CLKS_LOG_WARN, "WM", "FRAMEBUFFER NOT READY");
        return;
    }

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        clks_memset(&clks_wm_windows[i], 0, sizeof(clks_wm_windows[i]));
    }

    clks_wm_z_count = 0U;
    clks_wm_next_window_id = 1ULL;
    clks_wm_active_last = CLKS_FALSE;
    clks_wm_last_mouse_x = -1;
    clks_wm_last_mouse_y = -1;
    clks_wm_last_mouse_buttons = 0U;
    clks_wm_last_mouse_ready = CLKS_FALSE;
    clks_wm_scene_damage.count = 0U;
    clks_wm_present_damage.count = 0U;
    clks_wm_timing_reset();

    clks_wm_ready_flag = CLKS_TRUE;
    clks_log(CLKS_LOG_INFO, "WM", "KERNEL WINDOW FRAMEWORK ONLINE");
    clks_log(CLKS_LOG_INFO, "WM", "MULTI-RECT COMPOSITOR ONLINE");
}

static i32 clks_wm_origin_x(void) {
    return clks_display_origin_x(CLKS_DISPLAY_TARGET_WM);
}

static i32 clks_wm_origin_y(void) {
    return clks_display_origin_y(CLKS_DISPLAY_TARGET_WM);
}

static void clks_wm_fb_fill_rect(i32 x, i32 y, u32 width, u32 height, u32 color) {
    clks_fb_fill_rect((u32)(x + clks_wm_origin_x()), (u32)(y + clks_wm_origin_y()), width, height, color);
}

static void clks_wm_fb_blit_rgba(i32 x, i32 y, const void *pixels, u32 width, u32 height, u32 pitch_bytes) {
    clks_fb_blit_rgba(x + clks_wm_origin_x(), y + clks_wm_origin_y(), pixels, width, height, pitch_bytes);
}

static void clks_wm_fb_draw_char(i32 x, i32 y, char ch, u32 fg, u32 bg) {
    clks_fb_draw_char((u32)(x + clks_wm_origin_x()), (u32)(y + clks_wm_origin_y()), ch, fg, bg);
}

static void clks_wm_fb_draw_pixel(i32 x, i32 y, u32 color) {
    clks_fb_draw_pixel((u32)(x + clks_wm_origin_x()), (u32)(y + clks_wm_origin_y()), color);
}

static clks_bool clks_wm_fb_read_pixel(i32 x, i32 y, u32 *out_color) {
    return clks_fb_read_pixel((u32)(x + clks_wm_origin_x()), (u32)(y + clks_wm_origin_y()), out_color);
}

void clks_wm_tick(u64 tick) {
    (void)tick;

    if (clks_wm_ready_flag == CLKS_FALSE) {
        return;
    }

    clks_wm_reap_dead_owners();
    clks_wm_pump_foreground();
}

clks_bool clks_wm_ready(void) {
    return clks_wm_ready_flag;
}

clks_bool clks_wm_is_foreground(void) {
    if (clks_wm_ready_flag == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return (clks_tty_active() == CLKS_WM_TTY_INDEX) ? CLKS_TRUE : CLKS_FALSE;
}

u32 clks_wm_width(void) {
    return clks_display_width(CLKS_DISPLAY_TARGET_WM);
}

u32 clks_wm_height(void) {
    return clks_display_height(CLKS_DISPLAY_TARGET_WM);
}

clks_bool clks_wm_set_resolution(u32 width, u32 height) {
    if (clks_display_mode_set(CLKS_DISPLAY_TARGET_WM, width, height) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_wm_layer_free();
    clks_wm_scene_damage.count = 0U;
    clks_wm_present_damage.count = 0U;
    clks_wm_active_last = CLKS_FALSE;
    clks_wm_mark_dirty_full();
    clks_wm_mark_stats_dirty();
    return CLKS_TRUE;
}

u64 clks_wm_window_count(void) {
    clks_wm_reap_dead_owners();
    return (u64)clks_wm_z_count;
}

clks_bool clks_wm_window_id_at(u64 index, u64 *out_window_id) {
    u32 slot;

    if (out_window_id == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_window_id = 0ULL;
    clks_wm_reap_dead_owners();
    if (index >= (u64)clks_wm_z_count) {
        return CLKS_FALSE;
    }

    slot = clks_wm_z_order[(u32)index];
    if (slot >= CLKS_WM_MAX_WINDOWS || clks_wm_windows[slot].used == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_window_id = clks_wm_windows[slot].id;
    return CLKS_TRUE;
}

clks_bool clks_wm_snapshot(u64 window_id, struct clks_wm_snapshot *out_snapshot) {
    i32 slot;
    const struct clks_wm_window *win;

    if (out_snapshot == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_memset(out_snapshot, 0, sizeof(*out_snapshot));
    clks_wm_reap_dead_owners();
    slot = clks_wm_find_slot_by_id(window_id);
    if (slot < 0) {
        return CLKS_FALSE;
    }

    win = &clks_wm_windows[(u32)slot];
    out_snapshot->window_id = win->id;
    out_snapshot->owner_pid = win->owner_pid;
    out_snapshot->flags = win->flags;
    out_snapshot->x = (u64)(i64)win->x;
    out_snapshot->y = (u64)(i64)win->y;
    out_snapshot->width = (u64)win->width;
    out_snapshot->height = (u64)win->height;
    out_snapshot->focused = (win->focused == CLKS_TRUE) ? 1ULL : 0ULL;
    out_snapshot->presented = (win->presented == CLKS_TRUE) ? 1ULL : 0ULL;
    out_snapshot->event_count = (u64)win->queue.count;
    return CLKS_TRUE;
}
