clks_bool clks_wm_poll_event(u64 owner_pid, u64 window_id, struct clks_wm_event *out_event) {
    i32 slot = clks_wm_find_slot_by_id(window_id);

    if (slot < 0 || out_event == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_wm_owner_allows(&clks_wm_windows[(u32)slot], owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_wm_pump_foreground();
    return clks_wm_event_pop((u32)slot, out_event);
}

clks_bool clks_wm_move(u64 owner_pid, u64 window_id, i32 x, i32 y) {
    i32 slot = clks_wm_find_slot_by_id(window_id);
    struct clks_wm_window *win;
    i32 max_x;
    i32 max_y;
    u32 screen_w;
    u32 screen_h;

    if (slot < 0) {
        return CLKS_FALSE;
    }

    win = &clks_wm_windows[(u32)slot];
    if (clks_wm_owner_allows(win, owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    screen_w = clks_wm_width();
    screen_h = clks_wm_height();
    max_x = (i32)screen_w - (i32)win->width;
    max_y = (i32)screen_h - (i32)win->height;

    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    if (x < 0) {
        x = 0;
    }
    if (y < (i32)CLKS_WM_TOPBAR_HEIGHT) {
        y = (i32)CLKS_WM_TOPBAR_HEIGHT;
    }
    if (x > max_x) {
        x = max_x;
    }
    if (y > max_y) {
        y = max_y;
    }

    if (win->x != x || win->y != y) {
        i32 old_x = win->x;
        i32 old_y = win->y;
        clks_wm_mark_scene_dirty_rect(old_x, old_y, old_x + (i32)win->width, old_y + (i32)win->height);
        win->x = x;
        win->y = y;
        clks_wm_mark_dirty_window(win);
    }

    return CLKS_TRUE;
}

clks_bool clks_wm_set_focus(u64 owner_pid, u64 window_id) {
    i32 slot = clks_wm_find_slot_by_id(window_id);

    (void)owner_pid;

    if (slot < 0) {
        return CLKS_FALSE;
    }

    clks_wm_set_focus_slot(slot);
    return CLKS_TRUE;
}

clks_bool clks_wm_set_flags(u64 owner_pid, u64 window_id, u64 flags) {
    i32 slot = clks_wm_find_slot_by_id(window_id);
    struct clks_wm_window *win;
    u64 next_flags;

    if (slot < 0) {
        return CLKS_FALSE;
    }

    win = &clks_wm_windows[(u32)slot];
    if (clks_wm_owner_allows(win, owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    next_flags = flags & CLKS_WM_FLAG_TOPMOST;
    if (win->flags == next_flags) {
        return CLKS_TRUE;
    }

    clks_wm_mark_dirty_window(win);
    win->flags = next_flags;
    clks_wm_z_remove_slot((u32)slot);
    clks_wm_z_insert_for_flags((u32)slot);
    clks_wm_mark_dirty_window(win);
    return CLKS_TRUE;
}

clks_bool clks_wm_resize(u64 owner_pid, u64 window_id, u32 width, u32 height) {
    i32 slot = clks_wm_find_slot_by_id(window_id);
    struct clks_wm_window *win;
    u64 pixel_count;
    u32 *pixels;
    u32 copy_w;
    u32 copy_h;
    u32 y;
    i32 max_x;
    i32 max_y;
    u32 screen_w;
    u32 screen_h;

    if (slot < 0) {
        return CLKS_FALSE;
    }

    win = &clks_wm_windows[(u32)slot];
    if (clks_wm_owner_allows(win, owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (width < CLKS_WM_MIN_WIDTH || height < CLKS_WM_MIN_HEIGHT || width > CLKS_WM_MAX_WIDTH ||
        height > CLKS_WM_MAX_HEIGHT) {
        return CLKS_FALSE;
    }

    screen_w = clks_wm_width();
    screen_h = clks_wm_height();
    if (width > screen_w || height > screen_h) {
        return CLKS_FALSE;
    }

    pixel_count = (u64)width * (u64)height;
    pixels = (u32 *)clks_kmalloc((usize)(pixel_count * 4ULL));
    if (pixels == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_memset(pixels, 0, (usize)(pixel_count * 4ULL));
    copy_w = (win->width < width) ? win->width : width;
    copy_h = (win->height < height) ? win->height : height;
    for (y = 0U; y < copy_h; y++) {
        clks_memcpy(pixels + ((u64)y * (u64)width), win->pixels + ((u64)y * (u64)win->width), (usize)(copy_w * 4U));
    }

    clks_wm_mark_dirty_window(win);
    if (win->pixels != CLKS_NULL) {
        clks_kfree(win->pixels);
    }

    win->pixels = pixels;
    win->pixel_count = pixel_count;
    win->width = width;
    win->height = height;

    max_x = (i32)screen_w - (i32)width;
    max_y = (i32)screen_h - (i32)height;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }
    if (win->x > max_x) {
        win->x = max_x;
    }
    if (win->y > max_y) {
        win->y = max_y;
    }
    if (win->y < (i32)CLKS_WM_TOPBAR_HEIGHT) {
        win->y = (i32)CLKS_WM_TOPBAR_HEIGHT;
    }

    clks_wm_mark_dirty_window(win);
    return CLKS_TRUE;
}
