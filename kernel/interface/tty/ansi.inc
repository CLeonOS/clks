static u32 clks_tty_ansi_clamp_255(u32 value) {
    return (value > 255U) ? 255U : value;
}

static u32 clks_tty_ansi_color_from_256(u32 index) {
    if (index < 16U) {
        return clks_tty_ansi_palette(index);
    }

    if (index >= 16U && index <= 231U) {
        static const u32 steps[6] = {0U, 95U, 135U, 175U, 215U, 255U};
        u32 n = index - 16U;
        u32 r = n / 36U;
        u32 g = (n / 6U) % 6U;
        u32 b = n % 6U;

        return (steps[r] << 16U) | (steps[g] << 8U) | steps[b];
    }

    if (index >= 232U && index <= 255U) {
        u32 gray = 8U + ((index - 232U) * 10U);
        return (gray << 16U) | (gray << 8U) | gray;
    }

    return CLKS_TTY_FG;
}

static u32 clks_tty_ansi_parse_params(const char *params, u32 len, u32 *out_values, u32 max_values) {
    u32 count = 0U;
    u32 value = 0U;
    clks_bool has_digit = CLKS_FALSE;
    u32 i;

    if (out_values == CLKS_NULL || max_values == 0U) {
        return 0U;
    }

    if (len == 0U) {
        out_values[0] = 0U;
        return 1U;
    }

    for (i = 0U; i <= len; i++) {
        char ch = (i < len) ? params[i] : ';';

        if (ch >= '0' && ch <= '9') {
            has_digit = CLKS_TRUE;
            value = (value * 10U) + (u32)(ch - '0');
            continue;
        }

        if (ch == ';') {
            if (count < max_values) {
                out_values[count++] = (has_digit == CLKS_TRUE) ? value : 0U;
            }

            value = 0U;
            has_digit = CLKS_FALSE;
            continue;
        }
    }

    if (count == 0U) {
        out_values[0] = 0U;
        return 1U;
    }

    return count;
}

static u32 clks_tty_ansi_param_or_default(const u32 *params, u32 count, u32 index, u32 default_value) {
    if (params == CLKS_NULL || index >= count || params[index] == 0U) {
        return default_value;
    }

    return params[index];
}

static void clks_tty_ansi_clear_line_mode(u32 tty_index, u32 mode) {
    u32 row = clks_tty_cursor_row[tty_index];
    u32 start = 0U;
    u32 end = 0U;
    u32 col;

    if (row >= clks_tty_content_rows()) {
        return;
    }

    if (mode == 0U) {
        start = clks_tty_cursor_col[tty_index];
        end = clks_tty_cols;
    } else if (mode == 1U) {
        start = 0U;
        end = clks_tty_cursor_col[tty_index] + 1U;
    } else {
        start = 0U;
        end = clks_tty_cols;
    }

    if (start >= clks_tty_cols) {
        start = clks_tty_cols;
    }

    if (end > clks_tty_cols) {
        end = clks_tty_cols;
    }

    for (col = start; col < end; col++) {
        clks_tty_cells[tty_index][row][col] = ' ';
        clks_tty_cell_fg[tty_index][row][col] = CLKS_TTY_FG;
        clks_tty_cell_bg[tty_index][row][col] = CLKS_TTY_BG;
        clks_tty_cell_style[tty_index][row][col] = CLKS_TTY_STYLE_NONE;
        clks_tty_cell_scale[tty_index][row][col] = 1U;
    }

    if (tty_index == clks_tty_active_index) {
        if (clks_tty_defer_draw == CLKS_TRUE) {
            clks_tty_dirty_mark_row(row);
        } else {
            for (col = start; col < end; col++) {
                clks_tty_draw_cell(tty_index, row, col);
            }
        }
    }
}

static void clks_tty_ansi_clear_screen_mode(u32 tty_index, u32 mode) {
    u32 row;
    u32 col;
    u32 content_rows = clks_tty_content_rows();
    u32 cursor_row = clks_tty_cursor_row[tty_index];
    u32 cursor_col = clks_tty_cursor_col[tty_index];

    if (cursor_row >= content_rows) {
        cursor_row = content_rows - 1U;
    }

    if (cursor_col >= clks_tty_cols) {
        cursor_col = clks_tty_cols - 1U;
    }

    if (mode == 0U) {
        for (row = cursor_row; row < content_rows; row++) {
            u32 start_col = (row == cursor_row) ? cursor_col : 0U;

            for (col = start_col; col < clks_tty_cols; col++) {
                clks_tty_cells[tty_index][row][col] = ' ';
                clks_tty_cell_fg[tty_index][row][col] = CLKS_TTY_FG;
                clks_tty_cell_bg[tty_index][row][col] = CLKS_TTY_BG;
                clks_tty_cell_style[tty_index][row][col] = CLKS_TTY_STYLE_NONE;
                clks_tty_cell_scale[tty_index][row][col] = 1U;
            }
        }
    } else if (mode == 1U) {
        for (row = 0U; row <= cursor_row; row++) {
            u32 end_col = (row == cursor_row) ? (cursor_col + 1U) : clks_tty_cols;

            for (col = 0U; col < end_col; col++) {
                clks_tty_cells[tty_index][row][col] = ' ';
                clks_tty_cell_fg[tty_index][row][col] = CLKS_TTY_FG;
                clks_tty_cell_bg[tty_index][row][col] = CLKS_TTY_BG;
                clks_tty_cell_style[tty_index][row][col] = CLKS_TTY_STYLE_NONE;
                clks_tty_cell_scale[tty_index][row][col] = 1U;
            }
        }
    } else {
        for (row = 0U; row < content_rows; row++) {
            for (col = 0U; col < clks_tty_cols; col++) {
                clks_tty_cells[tty_index][row][col] = ' ';
                clks_tty_cell_fg[tty_index][row][col] = CLKS_TTY_FG;
                clks_tty_cell_bg[tty_index][row][col] = CLKS_TTY_BG;
                clks_tty_cell_style[tty_index][row][col] = CLKS_TTY_STYLE_NONE;
                clks_tty_cell_scale[tty_index][row][col] = 1U;
            }
        }
    }

    if (mode == 3U) {
        clks_tty_scrollback_head[tty_index] = 0U;
        clks_tty_scrollback_count[tty_index] = 0U;
        clks_tty_scrollback_offset[tty_index] = 0U;
    }

    if (tty_index == clks_tty_active_index) {
        clks_tty_redraw_active();
    }
}

static void clks_tty_ansi_apply_sgr_params(u32 tty_index, const char *params, u32 len) {
    u32 values[16];
    u32 count = clks_tty_ansi_parse_params(params, len, values, 16U);
    u32 i;

    if (count == 0U) {
        return;
    }

    for (i = 0U; i < count; i++) {
        u32 code = values[i];

        if (code == 0U) {
            clks_tty_reset_color_state(tty_index);
            continue;
        }

        if (code == 1U) {
            clks_tty_ansi[tty_index].bold = CLKS_TRUE;
            continue;
        }

        if (code == 4U) {
            clks_tty_ansi[tty_index].underline = CLKS_TRUE;
            continue;
        }

        if (code == 7U) {
            clks_tty_ansi[tty_index].inverse = CLKS_TRUE;
            continue;
        }

        if (code == 21U || code == 22U) {
            clks_tty_ansi[tty_index].bold = CLKS_FALSE;
            continue;
        }

        if (code == 24U) {
            clks_tty_ansi[tty_index].underline = CLKS_FALSE;
            continue;
        }

        if (code == 27U) {
            clks_tty_ansi[tty_index].inverse = CLKS_FALSE;
            continue;
        }

        if (code == 39U) {
            clks_tty_current_fg[tty_index] = CLKS_TTY_FG;
            continue;
        }

        if (code == 49U) {
            clks_tty_current_bg[tty_index] = CLKS_TTY_BG;
            continue;
        }

        if (code >= 30U && code <= 37U) {
            u32 idx = code - 30U;

            if (clks_tty_ansi[tty_index].bold == CLKS_TRUE) {
                idx += 8U;
            }

            clks_tty_current_fg[tty_index] = clks_tty_ansi_palette(idx);
            continue;
        }

        if (code >= 90U && code <= 97U) {
            clks_tty_current_fg[tty_index] = clks_tty_ansi_palette((code - 90U) + 8U);
            continue;
        }

        if (code >= 40U && code <= 47U) {
            clks_tty_current_bg[tty_index] = clks_tty_ansi_palette(code - 40U);
            continue;
        }

        if (code >= 100U && code <= 107U) {
            clks_tty_current_bg[tty_index] = clks_tty_ansi_palette((code - 100U) + 8U);
            continue;
        }

        if ((code == 38U || code == 48U) && (i + 1U) < count) {
            u32 mode = values[i + 1U];
            u32 color;

            if (mode == 5U && (i + 2U) < count) {
                color = clks_tty_ansi_color_from_256(values[i + 2U]);

                if (code == 38U) {
                    clks_tty_current_fg[tty_index] = color;
                } else {
                    clks_tty_current_bg[tty_index] = color;
                }

                i += 2U;
                continue;
            }

            if (mode == 2U && (i + 4U) < count) {
                u32 r = clks_tty_ansi_clamp_255(values[i + 2U]);
                u32 g = clks_tty_ansi_clamp_255(values[i + 3U]);
                u32 b = clks_tty_ansi_clamp_255(values[i + 4U]);
                color = (r << 16U) | (g << 8U) | b;

                if (code == 38U) {
                    clks_tty_current_fg[tty_index] = color;
                } else {
                    clks_tty_current_bg[tty_index] = color;
                }

                i += 4U;
                continue;
            }
        }
    }
}

static void clks_tty_ansi_apply_font_scale(u32 tty_index, const u32 *values, u32 count) {
    u32 scale;

    if (tty_index >= CLKS_TTY_COUNT) {
        return;
    }

    scale = clks_tty_ansi_param_or_default(values, count, 0U, 1U);
    clks_tty_ansi[tty_index].font_scale = (u8)clks_tty_clamp_scale(scale);
}

static void clks_tty_ansi_process_csi_final(u32 tty_index, const char *params, u32 len, char final) {
    u32 values[16];
    u32 count = clks_tty_ansi_parse_params(params, len, values, 16U);
    clks_bool private_mode = (len > 0U && params[0] == '?') ? CLKS_TRUE : CLKS_FALSE;

    if (final == 'm') {
        clks_tty_ansi_apply_sgr_params(tty_index, params, len);
        return;
    }

    if (final == 'z') {
        clks_tty_ansi_apply_font_scale(tty_index, values, count);
        return;
    }

    if (final == 'J') {
        u32 mode = (count == 0U) ? 0U : values[0];
        clks_tty_ansi_clear_screen_mode(tty_index, mode);
        return;
    }

    if (final == 'K') {
        u32 mode = (count == 0U) ? 0U : values[0];
        clks_tty_ansi_clear_line_mode(tty_index, mode);
        return;
    }

    if (final == 'H' || final == 'f') {
        u32 row = clks_tty_ansi_param_or_default(values, count, 0U, 1U);
        u32 col = clks_tty_ansi_param_or_default(values, count, 1U, 1U);

        if (row > 0U) {
            row--;
        }

        if (col > 0U) {
            col--;
        }

        if (row >= clks_tty_content_rows()) {
            row = clks_tty_content_rows() - 1U;
        }

        if (col >= clks_tty_cols) {
            col = clks_tty_cols - 1U;
        }

        clks_tty_cursor_row[tty_index] = row;
        clks_tty_cursor_col[tty_index] = col;
        return;
    }

    if (final == 'A') {
        u32 n = clks_tty_ansi_param_or_default(values, count, 0U, 1U);

        if (n > clks_tty_cursor_row[tty_index]) {
            clks_tty_cursor_row[tty_index] = 0U;
        } else {
            clks_tty_cursor_row[tty_index] -= n;
        }

        return;
    }

    if (final == 'B') {
        u32 n = clks_tty_ansi_param_or_default(values, count, 0U, 1U);
        u32 max_row = clks_tty_content_rows() - 1U;

        if (clks_tty_cursor_row[tty_index] + n > max_row) {
            clks_tty_cursor_row[tty_index] = max_row;
        } else {
            clks_tty_cursor_row[tty_index] += n;
        }

        return;
    }

    if (final == 'C') {
        u32 n = clks_tty_ansi_param_or_default(values, count, 0U, 1U);
        u32 max_col = clks_tty_cols - 1U;

        if (clks_tty_cursor_col[tty_index] + n > max_col) {
            clks_tty_cursor_col[tty_index] = max_col;
        } else {
            clks_tty_cursor_col[tty_index] += n;
        }

        return;
    }

    if (final == 'D') {
        u32 n = clks_tty_ansi_param_or_default(values, count, 0U, 1U);

        if (n > clks_tty_cursor_col[tty_index]) {
            clks_tty_cursor_col[tty_index] = 0U;
        } else {
            clks_tty_cursor_col[tty_index] -= n;
        }

        return;
    }

    if (final == 'E' || final == 'F') {
        u32 n = clks_tty_ansi_param_or_default(values, count, 0U, 1U);

        if (final == 'E') {
            u32 max_row = clks_tty_content_rows() - 1U;

            if (clks_tty_cursor_row[tty_index] + n > max_row) {
                clks_tty_cursor_row[tty_index] = max_row;
            } else {
                clks_tty_cursor_row[tty_index] += n;
            }
        } else {
            if (n > clks_tty_cursor_row[tty_index]) {
                clks_tty_cursor_row[tty_index] = 0U;
            } else {
                clks_tty_cursor_row[tty_index] -= n;
            }
        }

        clks_tty_cursor_col[tty_index] = 0U;
        return;
    }

    if (final == 'G') {
        u32 col = clks_tty_ansi_param_or_default(values, count, 0U, 1U);

        if (col > 0U) {
            col--;
        }

        if (col >= clks_tty_cols) {
            col = clks_tty_cols - 1U;
        }

        clks_tty_cursor_col[tty_index] = col;
        return;
    }

    if (final == 'd') {
        u32 row = clks_tty_ansi_param_or_default(values, count, 0U, 1U);

        if (row > 0U) {
            row--;
        }

        if (row >= clks_tty_content_rows()) {
            row = clks_tty_content_rows() - 1U;
        }

        clks_tty_cursor_row[tty_index] = row;
        return;
    }

    if (final == 's') {
        clks_tty_ansi[tty_index].saved_row = clks_tty_cursor_row[tty_index];
        clks_tty_ansi[tty_index].saved_col = clks_tty_cursor_col[tty_index];
        return;
    }

    if (final == 'u') {
        u32 row = clks_tty_ansi[tty_index].saved_row;
        u32 col = clks_tty_ansi[tty_index].saved_col;

        if (row >= clks_tty_content_rows()) {
            row = clks_tty_content_rows() - 1U;
        }

        if (col >= clks_tty_cols) {
            col = clks_tty_cols - 1U;
        }

        clks_tty_cursor_row[tty_index] = row;
        clks_tty_cursor_col[tty_index] = col;
        return;
    }

    if ((final == 'h' || final == 'l') && private_mode == CLKS_TRUE) {
        u32 mode = (count == 0U) ? 0U : values[0];

        if (mode == 25U) {
            if (final == 'h') {
                clks_tty_blink_enabled = CLKS_TRUE;
                if (tty_index == clks_tty_active_index) {
                    clks_tty_draw_cursor();
                }
            } else {
                clks_tty_blink_enabled = CLKS_FALSE;
                if (tty_index == clks_tty_active_index) {
                    clks_tty_hide_cursor();
                }
            }
        }

        return;
    }
}

static clks_bool clks_tty_ansi_process_byte(u32 tty_index, char ch) {
    clks_tty_ansi_state *state = &clks_tty_ansi[tty_index];

    if (state->in_escape == CLKS_FALSE) {
        if ((u8)ch == 0x1BU) {
            state->in_escape = CLKS_TRUE;
            state->saw_csi = CLKS_FALSE;
            state->len = 0U;
            state->params[0] = '\0';
            return CLKS_TRUE;
        }

        return CLKS_FALSE;
    }

    if (state->saw_csi == CLKS_FALSE) {
        if (ch == '[') {
            state->saw_csi = CLKS_TRUE;
            return CLKS_TRUE;
        }

        if (ch == '7') {
            state->saved_row = clks_tty_cursor_row[tty_index];
            state->saved_col = clks_tty_cursor_col[tty_index];
            clks_tty_reset_ansi_state(tty_index);
            return CLKS_TRUE;
        }

        if (ch == '8') {
            u32 row = state->saved_row;
            u32 col = state->saved_col;

            if (row >= clks_tty_content_rows()) {
                row = clks_tty_content_rows() - 1U;
            }

            if (col >= clks_tty_cols) {
                col = clks_tty_cols - 1U;
            }

            clks_tty_cursor_row[tty_index] = row;
            clks_tty_cursor_col[tty_index] = col;
            clks_tty_reset_ansi_state(tty_index);
            return CLKS_TRUE;
        }

        clks_tty_reset_ansi_state(tty_index);
        return CLKS_TRUE;
    }

    if ((ch >= '0' && ch <= '9') || ch == ';' || ch == '?') {
        if (state->len < CLKS_TTY_ANSI_MAX_LEN) {
            state->params[state->len++] = ch;
            state->params[state->len] = '\0';
        } else {
            clks_tty_reset_ansi_state(tty_index);
        }

        return CLKS_TRUE;
    }

    clks_tty_ansi_process_csi_final(tty_index, state->params, state->len, ch);
    clks_tty_reset_ansi_state(tty_index);
    return CLKS_TRUE;
}
