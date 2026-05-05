static void clks_wm_draw_window_to_scene_clipped(const struct clks_wm_window *win, const struct clks_wm_rect *clip) {
    struct clks_wm_rect win_rect;
    struct clks_wm_rect draw_rect;
    u32 src_x;
    u32 src_y;
    u32 copy_w;
    u32 copy_h;
    u32 row;
    usize row_bytes;
    u32 border_color;

    if (win == CLKS_NULL || clip == CLKS_NULL || win->used == CLKS_FALSE || win->presented == CLKS_FALSE ||
        win->pixels == CLKS_NULL || clks_wm_layers.ready == CLKS_FALSE) {
        return;
    }

    if (win->width == 0U || win->height == 0U) {
        return;
    }

    win_rect.l = win->x;
    win_rect.t = win->y;
    win_rect.r = win->x + (i32)win->width;
    win_rect.b = win->y + (i32)win->height;

    if (clks_wm_rect_intersect(&win_rect, clip, &draw_rect) == CLKS_FALSE) {
        return;
    }

    src_x = (u32)(draw_rect.l - win->x);
    src_y = (u32)(draw_rect.t - win->y);
    copy_w = (u32)(draw_rect.r - draw_rect.l);
    copy_h = (u32)(draw_rect.b - draw_rect.t);
    row_bytes = (usize)copy_w * 4U;

    for (row = 0U; row < copy_h; row++) {
        u64 src_offset = ((u64)(src_y + row) * (u64)win->width) + (u64)src_x;
        u64 dst_offset = ((u64)(u32)(draw_rect.t + (i32)row) * (u64)clks_wm_layers.width) + (u64)(u32)draw_rect.l;

        if (src_offset + (u64)copy_w > win->pixel_count || dst_offset + (u64)copy_w > clks_wm_layers.pixel_count) {
            return;
        }

        clks_memcpy(clks_wm_layers.scene + dst_offset, win->pixels + src_offset, row_bytes);
    }

    border_color = (win->focused == CLKS_TRUE) ? (u32)CLKS_WM_BORDER_ACTIVE : (u32)CLKS_WM_BORDER_INACTIVE;
    clks_wm_layer_fill_box_clipped(clks_wm_layers.scene, win->x, win->y, (i32)win->width, (i32)CLKS_WM_BORDER,
                                   border_color, clip);
    clks_wm_layer_fill_box_clipped(clks_wm_layers.scene, win->x, win->y + (i32)win->height - (i32)CLKS_WM_BORDER,
                                   (i32)win->width, (i32)CLKS_WM_BORDER, border_color, clip);
    clks_wm_layer_fill_box_clipped(clks_wm_layers.scene, win->x, win->y, (i32)CLKS_WM_BORDER, (i32)win->height,
                                   border_color, clip);
    clks_wm_layer_fill_box_clipped(clks_wm_layers.scene, win->x + (i32)win->width - (i32)CLKS_WM_BORDER, win->y,
                                   (i32)CLKS_WM_BORDER, (i32)win->height, border_color, clip);
}

static void clks_wm_draw_window_to_fb_clipped(const struct clks_wm_window *win, const struct clks_wm_rect *clip) {
    struct clks_wm_rect win_rect;
    struct clks_wm_rect draw_rect;
    u32 src_x;
    u32 src_y;
    u32 copy_w;
    u32 copy_h;
    u32 row;
    u32 border_color;

    if (win == CLKS_NULL || clip == CLKS_NULL || win->used == CLKS_FALSE || win->presented == CLKS_FALSE ||
        win->pixels == CLKS_NULL) {
        return;
    }

    if (win->width == 0U || win->height == 0U) {
        return;
    }

    win_rect.l = win->x;
    win_rect.t = win->y;
    win_rect.r = win->x + (i32)win->width;
    win_rect.b = win->y + (i32)win->height;

    if (clks_wm_rect_intersect(&win_rect, clip, &draw_rect) == CLKS_FALSE) {
        return;
    }

    src_x = (u32)(draw_rect.l - win->x);
    src_y = (u32)(draw_rect.t - win->y);
    copy_w = (u32)(draw_rect.r - draw_rect.l);
    copy_h = (u32)(draw_rect.b - draw_rect.t);

    for (row = 0U; row < copy_h; row++) {
        u64 src_offset = ((u64)(src_y + row) * (u64)win->width) + (u64)src_x;

        if (src_offset + (u64)copy_w > win->pixel_count) {
            return;
        }

        clks_wm_fb_blit_rgba(draw_rect.l, draw_rect.t + (i32)row, (const void *)(win->pixels + src_offset), copy_w, 1U,
                             win->width * 4U);
    }

    border_color = (win->focused == CLKS_TRUE) ? (u32)CLKS_WM_BORDER_ACTIVE : (u32)CLKS_WM_BORDER_INACTIVE;
    clks_wm_fill_fb_box_clipped(win->x, win->y, (i32)win->width, (i32)CLKS_WM_BORDER, border_color, clip);
    clks_wm_fill_fb_box_clipped(win->x, win->y + (i32)win->height - (i32)CLKS_WM_BORDER, (i32)win->width,
                                (i32)CLKS_WM_BORDER, border_color, clip);
    clks_wm_fill_fb_box_clipped(win->x, win->y, (i32)CLKS_WM_BORDER, (i32)win->height, border_color, clip);
    clks_wm_fill_fb_box_clipped(win->x + (i32)win->width - (i32)CLKS_WM_BORDER, win->y, (i32)CLKS_WM_BORDER,
                                (i32)win->height, border_color, clip);
}

static void clks_wm_update_scene_layer(void) {
    u32 dirty_index;

    if (clks_wm_scene_damage.count == 0U || clks_wm_layers.ready == CLKS_FALSE) {
        return;
    }

    for (dirty_index = 0U; dirty_index < clks_wm_scene_damage.count; dirty_index++) {
        u32 zi;
        struct clks_wm_rect dirty = clks_wm_scene_damage.rects[dirty_index];

        clks_wm_copy_background_to_scene(&dirty);

        for (zi = 0U; zi < clks_wm_z_count; zi++) {
            u32 slot = clks_wm_z_order[zi];
            clks_wm_draw_window_to_scene_clipped(&clks_wm_windows[slot], &dirty);
        }
    }

    clks_wm_scene_damage.count = 0U;
}
