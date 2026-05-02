u64 clks_wm_create(u64 owner_pid, i32 x, i32 y, u32 width, u32 height, u64 flags) {
    struct clks_framebuffer_info fb_info;
    i32 slot;
    i32 max_x;
    i32 max_y;
    u64 pixel_count;
    u32 *pixels;

    if (clks_wm_ready_flag == CLKS_FALSE) {
        return 0ULL;
    }

    if (width < CLKS_WM_MIN_WIDTH || height < CLKS_WM_MIN_HEIGHT || width > CLKS_WM_MAX_WIDTH ||
        height > CLKS_WM_MAX_HEIGHT) {
        return 0ULL;
    }

    fb_info = clks_fb_info();
    if (width > fb_info.width || height > fb_info.height) {
        return 0ULL;
    }

    max_x = (i32)fb_info.width - (i32)width;
    max_y = (i32)fb_info.height - (i32)height;
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

    slot = clks_wm_find_free_slot();
    if (slot < 0) {
        return 0ULL;
    }

    pixel_count = (u64)width * (u64)height;
    pixels = (u32 *)clks_kmalloc((usize)(pixel_count * 4ULL));
    if (pixels == CLKS_NULL) {
        return 0ULL;
    }

    clks_memset(pixels, 0, (usize)(pixel_count * 4ULL));

    clks_wm_windows[(u32)slot].used = CLKS_TRUE;
    clks_wm_windows[(u32)slot].presented = CLKS_FALSE;
    clks_wm_windows[(u32)slot].focused = CLKS_FALSE;
    clks_wm_windows[(u32)slot].id = clks_wm_next_window_id++;
    clks_wm_windows[(u32)slot].owner_pid = owner_pid;
    clks_wm_windows[(u32)slot].flags = flags & CLKS_WM_FLAG_TOPMOST;
    clks_wm_windows[(u32)slot].x = x;
    clks_wm_windows[(u32)slot].y = y;
    clks_wm_windows[(u32)slot].width = width;
    clks_wm_windows[(u32)slot].height = height;
    clks_wm_windows[(u32)slot].pixels = pixels;
    clks_wm_windows[(u32)slot].pixel_count = pixel_count;
    clks_wm_windows[(u32)slot].queue.head = 0U;
    clks_wm_windows[(u32)slot].queue.count = 0U;

    clks_wm_z_insert_for_flags((u32)slot);
    clks_wm_set_focus_slot(slot);

    return clks_wm_windows[(u32)slot].id;
}

clks_bool clks_wm_destroy(u64 owner_pid, u64 window_id) {
    i32 slot = clks_wm_find_slot_by_id(window_id);

    if (slot < 0) {
        return CLKS_FALSE;
    }

    if (clks_wm_owner_allows(&clks_wm_windows[(u32)slot], owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_wm_remove_slot((u32)slot);
    return CLKS_TRUE;
}

clks_bool clks_wm_present(u64 owner_pid, u64 window_id, const void *pixels, u32 src_width, u32 src_height,
                          u32 src_pitch_bytes) {
    i32 slot = clks_wm_find_slot_by_id(window_id);
    struct clks_wm_window *win;
    u32 y;
    const u8 *src_bytes;

    if (slot < 0 || pixels == CLKS_NULL) {
        return CLKS_FALSE;
    }

    win = &clks_wm_windows[(u32)slot];
    if (clks_wm_owner_allows(win, owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (src_width != win->width || src_height != win->height || src_pitch_bytes < (win->width * 4U)) {
        return CLKS_FALSE;
    }

    src_bytes = (const u8 *)pixels;
    if (src_pitch_bytes == (win->width * 4U)) {
        clks_memcpy(win->pixels, src_bytes, (usize)(win->pixel_count * 4ULL));
    } else {
        for (y = 0U; y < win->height; y++) {
            const u32 *src_row = (const u32 *)(const void *)(src_bytes + ((usize)y * (usize)src_pitch_bytes));
            u32 *dst_row = win->pixels + ((u64)y * (u64)win->width);
            clks_memcpy(dst_row, src_row, (usize)(win->width * 4U));
        }
    }

    win->presented = CLKS_TRUE;
    clks_wm_mark_dirty_window(win);
    clks_wm_flush_present_damage_now();
    return CLKS_TRUE;
}
