void clks_tty_switch(u32 tty_index) {
    if (clks_tty_is_ready == CLKS_FALSE) {
        return;
    }

    if (tty_index >= CLKS_TTY_COUNT) {
        return;
    }

    clks_tty_hide_cursor();
    clks_tty_active_index = tty_index;
    clks_tty_cursor_visible = CLKS_FALSE;
    clks_tty_deferred_scroll_rows = 0U;
    clks_tty_redraw_active();
    clks_tty_reset_blink_timer();
}

void clks_tty_tick(u64 tick) {
    if (clks_tty_is_ready == CLKS_FALSE || clks_tty_blink_enabled == CLKS_FALSE) {
        return;
    }

    if (clks_tty_active_index == CLKS_TTY_DESKTOP_INDEX) {
        clks_tty_cursor_visible = CLKS_FALSE;
        return;
    }

    if (clks_tty_scrollback_is_active(clks_tty_active_index) == CLKS_TRUE) {
        clks_tty_cursor_visible = CLKS_FALSE;
        return;
    }

    if (clks_tty_blink_last_tick == CLKS_TTY_BLINK_TICK_UNSET) {
        clks_tty_blink_last_tick = tick;

        if (clks_tty_cursor_visible == CLKS_FALSE) {
            clks_tty_draw_cursor();
        }

        return;
    }

    if (tick < clks_tty_blink_last_tick) {
        clks_tty_blink_last_tick = tick;
        return;
    }

    if ((tick - clks_tty_blink_last_tick) < CLKS_TTY_CURSOR_BLINK_INTERVAL_TICKS) {
        return;
    }

    clks_tty_blink_last_tick = tick;

    if (clks_tty_cursor_visible == CLKS_TRUE) {
        clks_tty_hide_cursor();
    } else {
        clks_tty_draw_cursor();
    }
}
