void clks_tty_scrollback_page_up(void) {
    u32 tty_index;
    u32 max_offset;
    u32 next_offset;

    if (clks_tty_is_ready == CLKS_FALSE) {
        return;
    }

    tty_index = clks_tty_active_index;

    if (tty_index == CLKS_TTY_DESKTOP_INDEX) {
        return;
    }

    max_offset = clks_tty_scrollback_max_offset(tty_index);

    if (max_offset == 0U) {
        return;
    }

    clks_tty_hide_cursor();
    next_offset = clks_tty_scrollback_clamped_offset(tty_index) + clks_tty_scrollback_page_step();

    if (next_offset > max_offset) {
        next_offset = max_offset;
    }

    if (next_offset != clks_tty_scrollback_offset[tty_index]) {
        clks_tty_scrollback_offset[tty_index] = next_offset;
        clks_tty_redraw_active();
        clks_tty_reset_blink_timer();
    }
}

void clks_tty_scrollback_page_down(void) {
    u32 tty_index;
    u32 current_offset;
    u32 next_offset;

    if (clks_tty_is_ready == CLKS_FALSE) {
        return;
    }

    tty_index = clks_tty_active_index;

    if (tty_index == CLKS_TTY_DESKTOP_INDEX) {
        return;
    }

    current_offset = clks_tty_scrollback_clamped_offset(tty_index);

    if (current_offset == 0U) {
        return;
    }

    clks_tty_hide_cursor();

    if (current_offset > clks_tty_scrollback_page_step()) {
        next_offset = current_offset - clks_tty_scrollback_page_step();
    } else {
        next_offset = 0U;
    }

    if (next_offset != clks_tty_scrollback_offset[tty_index]) {
        clks_tty_scrollback_offset[tty_index] = next_offset;
        clks_tty_redraw_active();
        clks_tty_reset_blink_timer();
    }
}

clks_bool clks_tty_scrollback_handle_key(char ch) {
    u32 tty_index;
    clks_bool redraw = CLKS_FALSE;

    if (clks_tty_is_ready == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    tty_index = clks_tty_active_index;
    if (tty_index == CLKS_TTY_DESKTOP_INDEX) {
        return CLKS_FALSE;
    }

    if (clks_tty_scrollback_is_active(tty_index) != CLKS_TRUE && clks_tty_search_editing[tty_index] != CLKS_TRUE) {
        return CLKS_FALSE;
    }

    if (clks_tty_search_editing[tty_index] == CLKS_TRUE) {
        if (ch == 27) {
            clks_tty_search_editing[tty_index] = CLKS_FALSE;
            clks_tty_search_not_found[tty_index] = CLKS_FALSE;
            redraw = CLKS_TRUE;
        } else if (ch == 8 || ch == 127) {
            if (clks_tty_search_len[tty_index] > 0U) {
                clks_tty_search_len[tty_index]--;
                clks_tty_search_query[tty_index][clks_tty_search_len[tty_index]] = '\0';
                clks_tty_search_has_match[tty_index] = CLKS_FALSE;
                clks_tty_search_not_found[tty_index] = CLKS_FALSE;
            }
            redraw = CLKS_TRUE;
        } else if (ch == '\n' || ch == '\r') {
            clks_tty_search_editing[tty_index] = CLKS_FALSE;
            if (clks_tty_search_len[tty_index] > 0U) {
                (void)clks_tty_search_find(tty_index, CLKS_FALSE);
            }
            redraw = CLKS_TRUE;
        } else if ((unsigned char)ch >= 0x20U && (unsigned char)ch <= 0x7EU) {
            if (clks_tty_search_len[tty_index] + 1U < CLKS_TTY_SEARCH_MAX) {
                clks_tty_search_query[tty_index][clks_tty_search_len[tty_index]++] = ch;
                clks_tty_search_query[tty_index][clks_tty_search_len[tty_index]] = '\0';
                clks_tty_search_has_match[tty_index] = CLKS_FALSE;
                clks_tty_search_not_found[tty_index] = CLKS_FALSE;
            }
            redraw = CLKS_TRUE;
        } else {
            return CLKS_TRUE;
        }

        if (redraw == CLKS_TRUE) {
            clks_tty_redraw_active();
            clks_tty_reset_blink_timer();
        }
        return CLKS_TRUE;
    }

    if (ch == '/') {
        clks_tty_search_editing[tty_index] = CLKS_TRUE;
        clks_tty_search_len[tty_index] = 0U;
        clks_tty_search_query[tty_index][0] = '\0';
        clks_tty_search_has_match[tty_index] = CLKS_FALSE;
        clks_tty_search_not_found[tty_index] = CLKS_FALSE;
        clks_tty_redraw_active();
        clks_tty_reset_blink_timer();
        return CLKS_TRUE;
    }

    if (ch == 'n' || ch == 'N') {
        if (clks_tty_search_len[tty_index] > 0U) {
            (void)clks_tty_search_find(tty_index, (ch == 'N') ? CLKS_TRUE : CLKS_FALSE);
            clks_tty_redraw_active();
            clks_tty_reset_blink_timer();
        }
        return CLKS_TRUE;
    }

    if (ch == 27) {
        clks_tty_search_editing[tty_index] = CLKS_FALSE;
        clks_tty_search_not_found[tty_index] = CLKS_FALSE;
        clks_tty_redraw_active();
        clks_tty_reset_blink_timer();
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}
