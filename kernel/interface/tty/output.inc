void clks_tty_init(void) {
    struct clks_framebuffer_info info;
    u32 tty;

    if (clks_fb_ready() == CLKS_FALSE) {
        clks_tty_is_ready = CLKS_FALSE;
        return;
    }

    /* If framebuffer lies here, the whole UI goes to hell immediately. */
    info = clks_fb_info();
    clks_tty_cell_width = clks_fb_half_cell_width();
    clks_tty_cell_height = clks_fb_cell_height();

    if (clks_tty_cell_width == 0U) {
        clks_tty_cell_width = 8U;
    }

    if (clks_tty_cell_height == 0U) {
        clks_tty_cell_height = 8U;
    }

    clks_tty_rows = info.height / clks_tty_cell_height;
    clks_tty_cols = info.width / clks_tty_cell_width;

    if (clks_tty_rows > CLKS_TTY_MAX_ROWS) {
        clks_tty_rows = CLKS_TTY_MAX_ROWS;
    }

    if (clks_tty_cols > CLKS_TTY_MAX_COLS) {
        clks_tty_cols = CLKS_TTY_MAX_COLS;
    }

    if (clks_tty_rows == 0U || clks_tty_cols == 0U) {
        clks_tty_is_ready = CLKS_FALSE;
        return;
    }

    for (tty = 0U; tty < CLKS_TTY_COUNT; tty++) {
        clks_tty_cursor_row[tty] = 0U;
        clks_tty_cursor_col[tty] = 0U;
        clks_tty_reset_color_state(tty);
        clks_tty_reset_ansi_state(tty);
        clks_tty_clear_tty(tty);
    }

    clks_tty_active_index = 0U;
    clks_tty_is_ready = CLKS_TRUE;
    clks_tty_cursor_visible = CLKS_FALSE;
    clks_tty_blink_enabled = CLKS_TRUE;
    clks_tty_defer_draw = CLKS_FALSE;
    clks_tty_deferred_scroll_rows = 0U;
    clks_tty_dirty_reset();
    clks_tty_reset_blink_timer();
    clks_tty_redraw_active();
}

void clks_tty_write_char(char ch) {
    clks_tty_write_n_internal(clks_tty_active_index, &ch, 1U);
}

void clks_tty_write_n(const char *text, usize len) {
    clks_tty_write_n_internal(clks_tty_active_index, text, len);
}

void clks_tty_write_n_to(u32 tty_index, const char *text, usize len) {
    clks_tty_write_n_internal(tty_index, text, len);
}

void clks_tty_write(const char *text) {
    if (text == CLKS_NULL) {
        return;
    }

    clks_tty_write_n(text, clks_strlen(text));
}

void clks_tty_clear_active(void) {
    if (clks_tty_is_ready == CLKS_FALSE) {
        return;
    }

    clks_tty_hide_cursor();
    clks_tty_clear_tty(clks_tty_active_index);
    clks_tty_deferred_scroll_rows = 0U;
    clks_tty_status_invalidate();
    clks_tty_cursor_visible = CLKS_FALSE;
    clks_tty_dirty_reset();
    clks_tty_reset_blink_timer();
    clks_tty_redraw_active();
}

static void clks_tty_utf8_reset(u32 tty_index) {
    clks_tty_utf8[tty_index].codepoint = 0U;
    clks_tty_utf8[tty_index].remaining = 0U;
    clks_tty_utf8[tty_index].expected = 0U;
}

static clks_bool clks_tty_utf8_emit_byte(u32 tty_index, u8 byte) {
    clks_tty_utf8_state *state = &clks_tty_utf8[tty_index];

    if (state->remaining == 0U) {
        if (byte < 0x80U) {
            clks_tty_put_codepoint_raw(tty_index, (u32)byte);
            return CLKS_TRUE;
        }

        if ((byte & 0xE0U) == 0xC0U) {
            state->codepoint = (u32)(byte & 0x1FU);
            state->remaining = 1U;
            state->expected = 2U;
            return CLKS_TRUE;
        }

        if ((byte & 0xF0U) == 0xE0U) {
            state->codepoint = (u32)(byte & 0x0FU);
            state->remaining = 2U;
            state->expected = 3U;
            return CLKS_TRUE;
        }

        if ((byte & 0xF8U) == 0xF0U) {
            state->codepoint = (u32)(byte & 0x07U);
            state->remaining = 3U;
            state->expected = 4U;
            return CLKS_TRUE;
        }

        clks_tty_put_codepoint_raw(tty_index, CLKS_TTY_UTF8_REPLACEMENT);
        return CLKS_FALSE;
    }

    if ((byte & 0xC0U) != 0x80U) {
        clks_tty_utf8_reset(tty_index);
        clks_tty_put_codepoint_raw(tty_index, CLKS_TTY_UTF8_REPLACEMENT);
        return CLKS_FALSE;
    }

    state->codepoint = (state->codepoint << 6U) | (u32)(byte & 0x3FU);
    state->remaining--;

    if (state->remaining == 0U) {
        u32 codepoint = state->codepoint;
        u8 expected = state->expected;
        clks_bool valid = CLKS_TRUE;

        if ((expected == 2U && codepoint < 0x80U) || (expected == 3U && codepoint < 0x800U) ||
            (expected == 4U && codepoint < 0x10000U) || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            valid = CLKS_FALSE;
        }

        clks_tty_utf8_reset(tty_index);
        clks_tty_put_codepoint_raw(tty_index, (valid == CLKS_TRUE) ? codepoint : CLKS_TTY_UTF8_REPLACEMENT);
    }

    return CLKS_TRUE;
}

static void clks_tty_write_n_internal(u32 tty_index, const char *text, usize len) {
    usize i = 0U;
    clks_bool target_active;

    if (clks_tty_is_ready == CLKS_FALSE || text == CLKS_NULL || len == 0U) {
        return;
    }

    if (tty_index >= CLKS_TTY_COUNT) {
        tty_index = clks_tty_active_index;
    }

    target_active = (tty_index == clks_tty_active_index) ? CLKS_TRUE : CLKS_FALSE;

    if (target_active == CLKS_TRUE) {
        clks_tty_deferred_scroll_rows = 0U;
        clks_tty_hide_cursor();

        if (clks_tty_scrollback_is_active(tty_index) == CLKS_TRUE) {
            clks_tty_scrollback_follow_tail(tty_index);
            clks_tty_redraw_active();
        }
    }

    clks_tty_defer_draw = CLKS_TRUE;

    while (i < len) {
        if (clks_tty_ansi_process_byte(tty_index, text[i]) == CLKS_FALSE) {
            (void)clks_tty_utf8_emit_byte(tty_index, (u8)text[i]);
        }

        i++;
    }

    clks_tty_defer_draw = CLKS_FALSE;

    if (target_active == CLKS_TRUE) {
        if (clks_tty_deferred_scroll_rows > 0U) {
            clks_fb_scroll_up(clks_tty_deferred_scroll_rows * clks_tty_cell_height, CLKS_TTY_BG);
            clks_tty_status_invalidate();
            clks_tty_deferred_scroll_rows = 0U;
        }

        clks_tty_flush_dirty();
        clks_tty_draw_cursor();
        clks_tty_draw_status_bar();
        clks_tty_reset_blink_timer();
    }
}
