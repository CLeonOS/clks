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
