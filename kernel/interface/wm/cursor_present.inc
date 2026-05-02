static clks_bool clks_wm_cursor_rect(i32 x, i32 y, struct clks_wm_rect *out_rect) {
    return clks_wm_rect_clip_to_screen(x - CLKS_WM_CURSOR_DAMAGE_RADIUS, y - CLKS_WM_CURSOR_DAMAGE_RADIUS,
                                       x + CLKS_WM_CURSOR_DAMAGE_RADIUS + 1, y + CLKS_WM_CURSOR_DAMAGE_RADIUS + 1,
                                       out_rect);
}

static void clks_wm_draw_inverted_cursor_pixel(i32 x, i32 y) {
    u32 under = 0U;

    if (x < 0 || y < 0) {
        return;
    }

    if (clks_fb_read_pixel((u32)x, (u32)y, &under) == CLKS_FALSE) {
        return;
    }

    clks_fb_draw_pixel((u32)x, (u32)y, (under ^ 0x00FFFFFFUL) & 0x00FFFFFFUL);
}

static void clks_wm_draw_cursor_clipped(i32 x, i32 y, u8 buttons, const struct clks_wm_rect *clip) {
    i32 dx;

    (void)buttons;

    if (clip == CLKS_NULL) {
        return;
    }

    for (dx = -CLKS_WM_CURSOR_RADIUS; dx <= CLKS_WM_CURSOR_RADIUS; dx++) {
        i32 px = x + dx;
        i32 py = y;

        if (px >= clip->l && px < clip->r && py >= clip->t && py < clip->b) {
            clks_wm_draw_inverted_cursor_pixel(px, py);
        }
    }

    for (dx = -CLKS_WM_CURSOR_RADIUS; dx <= CLKS_WM_CURSOR_RADIUS; dx++) {
        i32 px = x;
        i32 py = y + dx;

        if (dx == 0) {
            continue;
        }

        if (px >= clip->l && px < clip->r && py >= clip->t && py < clip->b) {
            clks_wm_draw_inverted_cursor_pixel(px, py);
        }
    }
}

static void clks_wm_present_rect(const struct clks_wm_rect *rect) {
    struct clks_mouse_state mouse = {0, 0, 0U, 0ULL, CLKS_FALSE};
    struct clks_wm_rect cursor;
    struct clks_wm_rect cursor_clip;
    const u32 *src;
    u32 width;
    u32 height;

    if (rect == CLKS_NULL || clks_wm_layers.ready == CLKS_FALSE || rect->r <= rect->l || rect->b <= rect->t) {
        return;
    }

    width = (u32)(rect->r - rect->l);
    height = (u32)(rect->b - rect->t);
    src = clks_wm_layers.scene + ((u64)(u32)rect->t * (u64)clks_wm_layers.width) + (u32)rect->l;
    clks_fb_blit_rgba(rect->l, rect->t, (const void *)src, width, height, clks_wm_layers.pitch_bytes);

    clks_wm_draw_stats_overlay_clipped(rect);

    clks_mouse_snapshot(&mouse);
    if (mouse.ready == CLKS_TRUE && clks_wm_cursor_rect(mouse.x, mouse.y, &cursor) == CLKS_TRUE &&
        clks_wm_rect_intersect(rect, &cursor, &cursor_clip) == CLKS_TRUE) {
        clks_wm_draw_cursor_clipped(mouse.x, mouse.y, mouse.buttons, &cursor_clip);
    }
}

static void clks_wm_present_rect_direct(const struct clks_wm_rect *rect) {
    struct clks_mouse_state mouse = {0, 0, 0U, 0ULL, CLKS_FALSE};
    struct clks_wm_rect cursor;
    struct clks_wm_rect cursor_clip;
    u32 zi;

    if (rect == CLKS_NULL || rect->r <= rect->l || rect->b <= rect->t) {
        return;
    }

    clks_wm_copy_background_to_fb(rect);

    for (zi = 0U; zi < clks_wm_z_count; zi++) {
        u32 slot = clks_wm_z_order[zi];
        clks_wm_draw_window_to_fb_clipped(&clks_wm_windows[slot], rect);
    }

    clks_wm_draw_stats_overlay_clipped(rect);

    clks_mouse_snapshot(&mouse);
    if (mouse.ready == CLKS_TRUE && clks_wm_cursor_rect(mouse.x, mouse.y, &cursor) == CLKS_TRUE &&
        clks_wm_rect_intersect(rect, &cursor, &cursor_clip) == CLKS_TRUE) {
        clks_wm_draw_cursor_clipped(mouse.x, mouse.y, mouse.buttons, &cursor_clip);
    }
}

static void clks_wm_flush_present_damage(u64 now_tick, u64 now_tsc) {
    u32 i;
    clks_bool scene_ready;

    if (clks_wm_present_damage.count == 0U) {
        return;
    }

    scene_ready = clks_wm_layer_ensure();
    if (scene_ready == CLKS_TRUE) {
        clks_wm_update_scene_layer();
    }

    for (i = 0U; i < clks_wm_present_damage.count; i++) {
        if (scene_ready == CLKS_TRUE) {
            clks_wm_present_rect(&clks_wm_present_damage.rects[i]);
        } else {
            clks_wm_present_rect_direct(&clks_wm_present_damage.rects[i]);
        }
    }

    clks_wm_scene_damage.count = 0U;
    clks_wm_present_damage.count = 0U;
    clks_wm_stats_on_present(now_tick, now_tsc);
    clks_wm_mark_stats_dirty();
}

static void clks_wm_flush_present_damage_now(void) {
    u64 now_tick;
    u64 now_tsc;

    if (clks_wm_ready_flag == CLKS_FALSE || clks_tty_active() != CLKS_WM_TTY_INDEX) {
        return;
    }

    if (clks_wm_active_last == CLKS_FALSE) {
        if (clks_wm_layers.ready == CLKS_TRUE) {
            clks_wm_layers.background_dirty = CLKS_TRUE;
        }
        clks_wm_mark_dirty_full();
        clks_wm_mark_stats_dirty();
        clks_wm_active_last = CLKS_TRUE;
    }

    now_tick = clks_interrupts_timer_ticks();
    now_tsc = clks_wm_read_tsc();
    clks_wm_timing_update_timebase(now_tick, now_tsc);
    clks_wm_flush_present_damage(now_tick, now_tsc);
}
