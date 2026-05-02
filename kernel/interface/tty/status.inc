static void clks_tty_draw_status_bar(void) {
    u32 tty_index;
    u32 status_row;
    u32 content_rows;
    u32 cursor = 0U;
    u32 col;
    u32 shown_row;
    u32 shown_col;
    u32 scroll_offset;
    const char *run_mode = "KERNEL";
    const char *input_mode;
    char line[CLKS_TTY_MAX_COLS + 1U];

    if (clks_tty_is_ready == CLKS_FALSE || clks_tty_status_bar_enabled() == CLKS_FALSE) {
        return;
    }

    if (clks_tty_active_index == CLKS_TTY_DESKTOP_INDEX) {
        return;
    }

    tty_index = clks_tty_active_index;
    content_rows = clks_tty_content_rows();
    status_row = clks_tty_rows - 1U;

    for (col = 0U; col < clks_tty_cols && col < CLKS_TTY_MAX_COLS; col++) {
        line[col] = ' ';
    }

    if (clks_tty_cols < CLKS_TTY_MAX_COLS) {
        line[clks_tty_cols] = '\0';
    } else {
        line[CLKS_TTY_MAX_COLS] = '\0';
    }

    shown_row = clks_tty_cursor_row[tty_index] + 1U;
    shown_col = clks_tty_cursor_col[tty_index] + 1U;
    scroll_offset = clks_tty_scrollback_clamped_offset(tty_index);
    input_mode = (scroll_offset > 0U) ? "SCROLL" : "INPUT";

    if (clks_exec_is_running() == CLKS_TRUE && clks_exec_current_tty() == tty_index) {
        run_mode = (clks_exec_current_path_is_user() == CLKS_TRUE) ? "USER" : "KAPP";
    }

    clks_tty_status_append_text(line, clks_tty_cols, &cursor, "TTY");
    clks_tty_status_append_u32_dec(line, clks_tty_cols, &cursor, tty_index);
    clks_tty_status_append_text(line, clks_tty_cols, &cursor, " ");
    clks_tty_status_append_text(line, clks_tty_cols, &cursor, "ROW:");
    if (shown_row > content_rows) {
        shown_row = content_rows;
    }
    clks_tty_status_append_u32_dec(line, clks_tty_cols, &cursor, shown_row);
    clks_tty_status_append_text(line, clks_tty_cols, &cursor, " COL:");
    clks_tty_status_append_u32_dec(line, clks_tty_cols, &cursor, shown_col);
    clks_tty_status_append_text(line, clks_tty_cols, &cursor, " SB:");
    clks_tty_status_append_u32_dec(line, clks_tty_cols, &cursor, scroll_offset);
    clks_tty_status_append_text(line, clks_tty_cols, &cursor, " IN:");
    clks_tty_status_append_text(line, clks_tty_cols, &cursor, input_mode);
    if (scroll_offset > 0U || clks_tty_search_editing[tty_index] == CLKS_TRUE || clks_tty_search_len[tty_index] > 0U) {
        clks_tty_status_append_text(line, clks_tty_cols, &cursor, " /");
        clks_tty_status_append_text(line, clks_tty_cols, &cursor, clks_tty_search_query[tty_index]);
        if (clks_tty_search_editing[tty_index] == CLKS_TRUE) {
            clks_tty_status_append_text(line, clks_tty_cols, &cursor, "_");
        } else if (clks_tty_search_not_found[tty_index] == CLKS_TRUE) {
            clks_tty_status_append_text(line, clks_tty_cols, &cursor, "?");
        }
        if (scroll_offset > 0U) {
            clks_tty_status_append_text(line, clks_tty_cols, &cursor, " n/N");
        }
    }
    clks_tty_status_append_text(line, clks_tty_cols, &cursor, " MODE:");
    clks_tty_status_append_text(line, clks_tty_cols, &cursor, run_mode);

    for (col = 0U; col < clks_tty_cols; col++) {
        char ch = (col < CLKS_TTY_MAX_COLS) ? line[col] : ' ';

        if (clks_tty_status_cache_valid == CLKS_TRUE && col < CLKS_TTY_MAX_COLS && clks_tty_status_cache[col] == ch) {
            continue;
        }

        clks_tty_draw_cell_with_colors(status_row, col, ch, CLKS_TTY_STATUS_FG, CLKS_TTY_STATUS_BG,
                                       CLKS_TTY_STATUS_STYLE, 1U);
        if (col < CLKS_TTY_MAX_COLS) {
            clks_tty_status_cache[col] = ch;
        }
    }

    clks_tty_status_cache_valid = CLKS_TRUE;
}

