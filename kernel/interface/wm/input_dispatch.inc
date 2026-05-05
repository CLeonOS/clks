#if CLKS_CFG_WM_INPUT_DISPATCH != 0
static void clks_wm_dispatch_keyboard_to_focused(void) {
    i32 focused_slot = clks_wm_focused_slot();
    u32 budget = 64U;

    if (focused_slot < 0) {
        return;
    }

    while (budget > 0U) {
        char ch = '\0';

        if (clks_keyboard_pop_char_for_tty(CLKS_WM_TTY_INDEX, &ch) == CLKS_FALSE) {
            break;
        }

        clks_wm_event_push((u32)focused_slot, CLKS_WM_EVENT_KEY, (u64)(u8)ch, 0ULL, 0ULL, 0ULL);
        budget--;
    }
}

static void clks_wm_dispatch_mouse(void) {
    struct clks_mouse_state mouse = {0, 0, 0U, 0ULL, CLKS_FALSE};
    i32 logical_x;
    i32 logical_y;
    i32 focused_slot;
    i32 hit_slot;
    i32 target_slot;

    clks_mouse_snapshot(&mouse);
    logical_x = clks_display_map_x_phys_to_logical(CLKS_DISPLAY_TARGET_WM, mouse.x);
    logical_y = clks_display_map_y_phys_to_logical(CLKS_DISPLAY_TARGET_WM, mouse.y);
    if (logical_x < 0 || logical_y < 0 || logical_x >= (i32)clks_wm_width() || logical_y >= (i32)clks_wm_height()) {
        mouse.ready = CLKS_FALSE;
    }
    focused_slot = clks_wm_focused_slot();
    hit_slot = (mouse.ready == CLKS_TRUE) ? clks_wm_window_hit_test(logical_x, logical_y) : -1;
    target_slot = focused_slot;

    if (mouse.ready == CLKS_TRUE && (mouse.buttons & CLKS_MOUSE_BTN_LEFT) != 0U &&
        (clks_wm_last_mouse_buttons & CLKS_MOUSE_BTN_LEFT) == 0U && hit_slot >= 0) {
        clks_wm_set_focus_slot(hit_slot);
        focused_slot = hit_slot;
    }

    if (mouse.ready == CLKS_TRUE) {
        if (((mouse.buttons | clks_wm_last_mouse_buttons) & CLKS_MOUSE_BTN_LEFT) != 0U && focused_slot >= 0) {
            target_slot = focused_slot;
        } else if (hit_slot >= 0) {
            target_slot = hit_slot;
        }
    }

    if (mouse.ready == CLKS_TRUE && target_slot >= 0) {
        const struct clks_wm_window *target_win = &clks_wm_windows[(u32)target_slot];
        i64 local_x = (i64)logical_x - (i64)target_win->x;
        i64 local_y = (i64)logical_y - (i64)target_win->y;

        if (logical_x != clks_wm_last_mouse_x || logical_y != clks_wm_last_mouse_y) {
            clks_wm_event_push((u32)target_slot, CLKS_WM_EVENT_MOUSE_MOVE, (u64)(i64)logical_x, (u64)(i64)logical_y,
                               (u64)local_x, (u64)local_y);
        }

        if (mouse.buttons != clks_wm_last_mouse_buttons) {
            u64 changed = (u64)(mouse.buttons ^ clks_wm_last_mouse_buttons);
            clks_wm_event_push((u32)target_slot, CLKS_WM_EVENT_MOUSE_BUTTON, (u64)mouse.buttons, changed, (u64)local_x,
                               (u64)local_y);
        }
    }

    if (clks_wm_last_mouse_ready == CLKS_TRUE &&
        (mouse.ready != clks_wm_last_mouse_ready || logical_x != clks_wm_last_mouse_x ||
         logical_y != clks_wm_last_mouse_y || mouse.buttons != clks_wm_last_mouse_buttons)) {
        clks_wm_mark_dirty_cursor(clks_wm_last_mouse_x, clks_wm_last_mouse_y);
    }

    if (mouse.ready == CLKS_TRUE &&
        (mouse.ready != clks_wm_last_mouse_ready || logical_x != clks_wm_last_mouse_x ||
         logical_y != clks_wm_last_mouse_y || mouse.buttons != clks_wm_last_mouse_buttons)) {
        clks_wm_mark_dirty_cursor(logical_x, logical_y);
    }

    clks_wm_last_mouse_ready = mouse.ready;
    clks_wm_last_mouse_x = logical_x;
    clks_wm_last_mouse_y = logical_y;
    clks_wm_last_mouse_buttons = mouse.buttons;
}
#endif

static void clks_wm_pump_foreground(void) {
    u64 now_tick;
    u64 now_tsc;

    if (clks_wm_ready_flag == CLKS_FALSE) {
        return;
    }

    if (clks_tty_active() != CLKS_WM_TTY_INDEX) {
        clks_wm_active_last = CLKS_FALSE;
        clks_wm_scene_damage.count = 0U;
        clks_wm_present_damage.count = 0U;
        clks_wm_timing.stats_anchor_valid = CLKS_FALSE;
        return;
    }

    (void)clks_wm_layer_ensure();

    if (clks_wm_active_last == CLKS_FALSE) {
        if (clks_wm_layers.ready == CLKS_TRUE) {
            clks_wm_layers.background_dirty = CLKS_TRUE;
        }
        clks_wm_mark_dirty_full();
        clks_wm_mark_stats_dirty();
    }

#if CLKS_CFG_WM_INPUT_DISPATCH != 0
    clks_wm_dispatch_mouse();
    clks_wm_dispatch_keyboard_to_focused();
#endif

    now_tick = clks_interrupts_timer_ticks();
    now_tsc = clks_wm_read_tsc();
    clks_wm_timing_update_timebase(now_tick, now_tsc);

    if (clks_wm_present_damage.count != 0U && clks_wm_frame_paced(now_tick, now_tsc) == CLKS_TRUE) {
        clks_wm_flush_present_damage(now_tick, now_tsc);
    }

    clks_wm_active_last = CLKS_TRUE;
}
