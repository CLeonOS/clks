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
    const char *ime_status;
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

    ime_status = clks_inputm_status_text();
    if (ime_status != CLKS_NULL && ime_status[0] != '\0' && clks_inputm_status_is_composing() == CLKS_TRUE) {
        clks_tty_status_append_text(line, clks_tty_cols, &cursor, ime_status);
        goto clks_tty_status_render;
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
    if (ime_status != CLKS_NULL && ime_status[0] != '\0') {
        clks_tty_status_append_text(line, clks_tty_cols, &cursor, " ");
        clks_tty_status_append_text(line, clks_tty_cols, &cursor, ime_status);
    }

clks_tty_status_render:
    col = 0U;
    cursor = 0U;
    while (col < clks_tty_cols && cursor < clks_tty_cols && cursor < CLKS_TTY_MAX_COLS) {
        u8 byte = (u8)line[cursor];
        u32 codepoint = (u32)' ';
        u32 used = 1U;
        u32 width;
        u32 j;

        if (byte == 0U) {
            codepoint = (u32)' ';
        } else if (byte < 0x80U) {
            codepoint = (u32)byte;
        } else if ((byte & 0xE0U) == 0xC0U && cursor + 1U < CLKS_TTY_MAX_COLS) {
            codepoint = ((u32)(byte & 0x1FU) << 6U) | (u32)(((u8)line[cursor + 1U]) & 0x3FU);
            used = 2U;
        } else if ((byte & 0xF0U) == 0xE0U && cursor + 2U < CLKS_TTY_MAX_COLS) {
            codepoint = ((u32)(byte & 0x0FU) << 12U) | ((u32)(((u8)line[cursor + 1U]) & 0x3FU) << 6U) |
                        (u32)(((u8)line[cursor + 2U]) & 0x3FU);
            used = 3U;
        } else if ((byte & 0xF8U) == 0xF0U && cursor + 3U < CLKS_TTY_MAX_COLS) {
            codepoint = ((u32)(byte & 0x07U) << 18U) | ((u32)(((u8)line[cursor + 1U]) & 0x3FU) << 12U) |
                        ((u32)(((u8)line[cursor + 2U]) & 0x3FU) << 6U) |
                        (u32)(((u8)line[cursor + 3U]) & 0x3FU);
            used = 4U;
        }

        width = clks_tty_codepoint_width(codepoint);
        if (width == 0U) {
            width = 1U;
        }
        if (col + width > clks_tty_cols) {
            break;
        }

        clks_tty_draw_cell_with_colors(status_row, col, codepoint, CLKS_TTY_STATUS_FG, CLKS_TTY_STATUS_BG,
                                       CLKS_TTY_STATUS_STYLE, 1U);
        if (col < CLKS_TTY_MAX_COLS) {
            clks_tty_status_cache[col] = (char)((codepoint < 0x80U) ? codepoint : '?');
        }
        for (j = 1U; j < width && col + j < clks_tty_cols; j++) {
            clks_tty_draw_cell_with_colors(status_row, col + j, CLKS_TTY_CELL_CONTINUATION, CLKS_TTY_STATUS_FG,
                                           CLKS_TTY_STATUS_BG, CLKS_TTY_STATUS_STYLE, 1U);
            if (col + j < CLKS_TTY_MAX_COLS) {
                clks_tty_status_cache[col + j] = '\0';
            }
        }

        col += width;
        cursor += used;
    }

    while (col < clks_tty_cols) {
        clks_tty_draw_cell_with_colors(status_row, col, (u32)' ', CLKS_TTY_STATUS_FG, CLKS_TTY_STATUS_BG,
                                       CLKS_TTY_STATUS_STYLE, 1U);
        if (col < CLKS_TTY_MAX_COLS) {
            clks_tty_status_cache[col] = ' ';
        }
        col++;
    }

    clks_tty_status_cache_valid = CLKS_TRUE;
}

void clks_tty_status_refresh(void) {
    if (clks_tty_is_ready == CLKS_FALSE) {
        return;
    }

    clks_tty_status_invalidate();
    clks_tty_draw_status_bar();
}
