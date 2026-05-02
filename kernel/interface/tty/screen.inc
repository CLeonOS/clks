static void clks_tty_reset_color_state(u32 tty_index) {
    clks_tty_current_fg[tty_index] = CLKS_TTY_FG;
    clks_tty_current_bg[tty_index] = CLKS_TTY_BG;
    clks_tty_ansi[tty_index].bold = CLKS_FALSE;
    clks_tty_ansi[tty_index].underline = CLKS_FALSE;
    clks_tty_ansi[tty_index].inverse = CLKS_FALSE;
    clks_tty_ansi[tty_index].font_scale = 1U;
}

static void clks_tty_reset_ansi_state(u32 tty_index) {
    clks_tty_ansi[tty_index].in_escape = CLKS_FALSE;
    clks_tty_ansi[tty_index].saw_csi = CLKS_FALSE;
    clks_tty_ansi[tty_index].len = 0U;
    clks_tty_ansi[tty_index].params[0] = '\0';
}

static void clks_tty_fill_row(u32 tty_index, u32 row, char ch) {
    u32 col;

    for (col = 0; col < clks_tty_cols; col++) {
        clks_tty_cells[tty_index][row][col] = ch;
        clks_tty_cell_fg[tty_index][row][col] = CLKS_TTY_FG;
        clks_tty_cell_bg[tty_index][row][col] = CLKS_TTY_BG;
        clks_tty_cell_style[tty_index][row][col] = CLKS_TTY_STYLE_NONE;
        clks_tty_cell_scale[tty_index][row][col] = 1U;
    }
}

static void clks_tty_clear_tty(u32 tty_index) {
    u32 row;

    for (row = 0; row < clks_tty_content_rows(); row++) {
        clks_tty_fill_row(tty_index, row, ' ');
    }

    clks_tty_cursor_row[tty_index] = 0U;
    clks_tty_cursor_col[tty_index] = 0U;
    clks_tty_line_scale[tty_index] = 1U;
    clks_tty_scrollback_head[tty_index] = 0U;
    clks_tty_scrollback_count[tty_index] = 0U;
    clks_tty_scrollback_offset[tty_index] = 0U;
    clks_tty_search_query[tty_index][0] = '\0';
    clks_tty_search_len[tty_index] = 0U;
    clks_tty_search_editing[tty_index] = CLKS_FALSE;
    clks_tty_search_has_match[tty_index] = CLKS_FALSE;
    clks_tty_search_not_found[tty_index] = CLKS_FALSE;
    clks_tty_search_match_doc[tty_index] = 0U;
}

static void clks_tty_hide_cursor(void) {
    u32 row;
    u32 col;

    if (clks_tty_is_ready == CLKS_FALSE || clks_tty_cursor_visible == CLKS_FALSE) {
        return;
    }

    if (clks_tty_scrollback_is_active(clks_tty_active_index) == CLKS_TRUE) {
        clks_tty_cursor_visible = CLKS_FALSE;
        return;
    }

    row = clks_tty_cursor_row[clks_tty_active_index];
    col = clks_tty_cursor_col[clks_tty_active_index];

    if (row < clks_tty_content_rows() && col < clks_tty_cols) {
        clks_tty_draw_cell(clks_tty_active_index, row, col);
    }

    clks_tty_cursor_visible = CLKS_FALSE;
}

static void clks_tty_draw_cursor(void) {
    u32 row;
    u32 col;
    u32 fg;
    u32 bg;
    u8 style;
    char ch;

    if (clks_tty_is_ready == CLKS_FALSE) {
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

    row = clks_tty_cursor_row[clks_tty_active_index];
    col = clks_tty_cursor_col[clks_tty_active_index];

    if (row >= clks_tty_content_rows() || col >= clks_tty_cols) {
        clks_tty_cursor_visible = CLKS_FALSE;
        return;
    }

    ch = clks_tty_cells[clks_tty_active_index][row][col];
    fg = clks_tty_cell_fg[clks_tty_active_index][row][col];
    bg = clks_tty_cell_bg[clks_tty_active_index][row][col];
    style = clks_tty_cell_style[clks_tty_active_index][row][col];

    clks_tty_draw_cell_with_colors(row, col, ch, bg, fg, style, clks_tty_cell_scale[clks_tty_active_index][row][col]);
    clks_tty_cursor_visible = CLKS_TRUE;
}

static void clks_tty_draw_scrollbar(u32 scroll_count, u32 scroll_offset) {
    u32 content_rows;
    u32 doc_rows;
    u32 viewport_start;
    u32 track_x;
    u32 track_y;
    u32 track_w;
    u32 track_h;
    u32 thumb_h;
    u32 thumb_y;

    if (scroll_offset == 0U || clks_tty_cols == 0U || clks_tty_cell_width == 0U || clks_tty_cell_height == 0U) {
        return;
    }

    content_rows = clks_tty_content_rows();
    if (content_rows == 0U || scroll_count == 0U) {
        return;
    }

    doc_rows = scroll_count + content_rows;
    track_w = (CLKS_TTY_SCROLLBAR_W < clks_tty_cell_width) ? CLKS_TTY_SCROLLBAR_W : clks_tty_cell_width;
    track_x = (clks_tty_cols * clks_tty_cell_width) - track_w;
    track_y = 0U;
    track_h = content_rows * clks_tty_cell_height;

    if (track_h == 0U || doc_rows <= content_rows) {
        return;
    }

    viewport_start = (scroll_count >= scroll_offset) ? (scroll_count - scroll_offset) : 0U;
    thumb_h = (track_h * content_rows) / doc_rows;
    if (thumb_h < clks_tty_cell_height) {
        thumb_h = clks_tty_cell_height;
    }
    if (thumb_h > track_h) {
        thumb_h = track_h;
    }

    if (doc_rows == content_rows || track_h == thumb_h) {
        thumb_y = track_y;
    } else {
        thumb_y = track_y + (((track_h - thumb_h) * viewport_start) / (doc_rows - content_rows));
    }

    clks_fb_fill_rect(track_x, track_y, track_w, track_h, CLKS_TTY_SCROLLBAR_TRACK);
    clks_fb_fill_rect(track_x, thumb_y, track_w, thumb_h, CLKS_TTY_SCROLLBAR_THUMB);
}

static void clks_tty_redraw_active(void) {
    u32 row;
    u32 col;
    u32 tty_index = clks_tty_active_index;
    u32 scroll_count = clks_tty_scrollback_count[tty_index];
    u32 scroll_offset = clks_tty_scrollback_clamped_offset(tty_index);
    u32 start_doc = (scroll_count >= scroll_offset) ? (scroll_count - scroll_offset) : 0U;

    /* Full redraw is expensive, but sometimes the least cursed option. */
    clks_fb_clear(CLKS_TTY_BG);
    clks_tty_status_invalidate();
    clks_tty_cursor_visible = CLKS_FALSE;
    clks_tty_deferred_scroll_rows = 0U;
    clks_tty_dirty_reset();

    for (row = 0; row < clks_tty_content_rows(); row++) {
        u32 doc_index = start_doc + row;

        if (doc_index < scroll_count) {
            u32 phys = clks_tty_scrollback_logical_to_physical(tty_index, doc_index);

            for (col = 0; col < clks_tty_cols; col++) {
                clks_tty_draw_cell_with_colors(row, col, clks_tty_scrollback_cells[tty_index][phys][col],
                                               clks_tty_scrollback_fg[tty_index][phys][col],
                                               clks_tty_scrollback_bg[tty_index][phys][col],
                                               clks_tty_scrollback_style[tty_index][phys][col],
                                               clks_tty_scrollback_scale[tty_index][phys][col]);
            }

            continue;
        }

        {
            u32 src_row = doc_index - scroll_count;

            if (src_row >= clks_tty_content_rows()) {
                continue;
            }

            for (col = 0; col < clks_tty_cols; col++) {
                clks_tty_draw_cell_with_colors(
                    row, col, clks_tty_cells[tty_index][src_row][col], clks_tty_cell_fg[tty_index][src_row][col],
                    clks_tty_cell_bg[tty_index][src_row][col], clks_tty_cell_style[tty_index][src_row][col],
                    clks_tty_cell_scale[tty_index][src_row][col]);
            }
        }
    }

    if (scroll_offset == 0U) {
        clks_tty_draw_cursor();
    } else {
        clks_tty_draw_scrollbar(scroll_count, scroll_offset);
    }

    clks_tty_draw_status_bar();
}
static void clks_tty_scroll_up(u32 tty_index) {
    u32 row;

    if (clks_tty_line_scale[tty_index] > 1U) {
        clks_tty_line_scale[tty_index]--;
    }

    /* Classic terminal move: throw top line away, pretend everything is fine. */
    clks_tty_scrollback_push_row(tty_index, 0U);

    for (row = 1; row < clks_tty_content_rows(); row++) {
        clks_memcpy(clks_tty_cells[tty_index][row - 1U], clks_tty_cells[tty_index][row], clks_tty_cols);
        clks_memcpy(clks_tty_cell_fg[tty_index][row - 1U], clks_tty_cell_fg[tty_index][row],
                    (usize)clks_tty_cols * sizeof(u32));
        clks_memcpy(clks_tty_cell_bg[tty_index][row - 1U], clks_tty_cell_bg[tty_index][row],
                    (usize)clks_tty_cols * sizeof(u32));
        clks_memcpy(clks_tty_cell_style[tty_index][row - 1U], clks_tty_cell_style[tty_index][row], clks_tty_cols);
        clks_memcpy(clks_tty_cell_scale[tty_index][row - 1U], clks_tty_cell_scale[tty_index][row], clks_tty_cols);
    }

    clks_tty_fill_row(tty_index, clks_tty_content_rows() - 1U, ' ');

    if (tty_index == clks_tty_active_index) {
        if (clks_tty_scrollback_is_active(tty_index) == CLKS_TRUE) {
            clks_tty_redraw_active();
        } else {
            if (clks_tty_defer_draw == CLKS_TRUE) {
                if (clks_tty_deferred_scroll_rows < clks_tty_content_rows()) {
                    clks_tty_deferred_scroll_rows++;
                }
                clks_tty_dirty_shift_up_one();
                clks_tty_dirty_mark_row(clks_tty_content_rows() - 1U);
            } else {
                u32 col;

                clks_fb_scroll_up(clks_tty_cell_height, CLKS_TTY_BG);
                clks_tty_status_invalidate();
                for (col = 0U; col < clks_tty_cols; col++) {
                    clks_tty_draw_cell(tty_index, clks_tty_content_rows() - 1U, col);
                }
            }
        }
    }
}
static void clks_tty_put_visible(u32 tty_index, u32 row, u32 col, char ch) {
    u32 fg = clks_tty_current_fg[tty_index];
    u32 bg = clks_tty_current_bg[tty_index];
    u32 scale = clks_tty_clamp_scale((u32)clks_tty_ansi[tty_index].font_scale);
    u32 span_rows = scale;
    u32 span_cols = scale;
    u32 rr;
    u32 cc;
    u8 style = CLKS_TTY_STYLE_NONE;

    if (row >= clks_tty_content_rows() || col >= clks_tty_cols) {
        return;
    }

    if (row + span_rows > clks_tty_content_rows()) {
        span_rows = clks_tty_content_rows() - row;
    }

    if (col + span_cols > clks_tty_cols) {
        span_cols = clks_tty_cols - col;
    }

    if (span_rows == 0U || span_cols == 0U) {
        return;
    }

    if (clks_tty_ansi[tty_index].inverse == CLKS_TRUE) {
        u32 swap = fg;
        fg = bg;
        bg = swap;
    }

    if (clks_tty_ansi[tty_index].bold == CLKS_TRUE) {
        style = (u8)(style | CLKS_TTY_STYLE_BOLD);
    }

    if (clks_tty_ansi[tty_index].underline == CLKS_TRUE) {
        style = (u8)(style | CLKS_TTY_STYLE_UNDERLINE);
    }

    for (rr = 0U; rr < span_rows; rr++) {
        for (cc = 0U; cc < span_cols; cc++) {
            clks_tty_cells[tty_index][row + rr][col + cc] =
                (rr == 0U && cc == 0U) ? ch : CLKS_TTY_CELL_CONTINUATION;
            clks_tty_cell_fg[tty_index][row + rr][col + cc] = fg;
            clks_tty_cell_bg[tty_index][row + rr][col + cc] = bg;
            clks_tty_cell_style[tty_index][row + rr][col + cc] = style;
            clks_tty_cell_scale[tty_index][row + rr][col + cc] = (u8)scale;
        }
    }

    if (tty_index == clks_tty_active_index) {
        if (clks_tty_defer_draw == CLKS_TRUE) {
            for (rr = 0U; rr < span_rows; rr++) {
                clks_tty_dirty_mark_row(row + rr);
            }
        } else {
            for (rr = 0U; rr < span_rows; rr++) {
                for (cc = 0U; cc < span_cols; cc++) {
                    clks_tty_draw_cell(tty_index, row + rr, col + cc);
                }
            }
            clks_tty_draw_cell(tty_index, row, col);
        }
    }
}

static void clks_tty_advance_after_visible(u32 tty_index, u32 width_cells) {
    u32 row;
    u32 col;

    width_cells = clks_tty_clamp_scale(width_cells);
    clks_tty_cursor_col[tty_index] += width_cells;

    if (clks_tty_cursor_col[tty_index] < clks_tty_cols) {
        return;
    }

    row = clks_tty_cursor_row[tty_index] + clks_tty_line_scale[tty_index];
    col = 0U;

    while (row >= clks_tty_content_rows()) {
        clks_tty_scroll_up(tty_index);
        row--;
    }

    clks_tty_cursor_row[tty_index] = row;
    clks_tty_cursor_col[tty_index] = col;
    clks_tty_line_scale[tty_index] = 1U;
}

static void clks_tty_put_char_raw(u32 tty_index, char ch) {
    u32 row = clks_tty_cursor_row[tty_index];
    u32 col = clks_tty_cursor_col[tty_index];
    u32 scale = clks_tty_clamp_scale((u32)clks_tty_ansi[tty_index].font_scale);

    if (ch == '\r') {
        clks_tty_cursor_col[tty_index] = 0U;
        return;
    }

    if (ch == '\n') {
        clks_tty_cursor_col[tty_index] = 0U;
        clks_tty_cursor_row[tty_index] += clks_tty_line_scale[tty_index];
        clks_tty_line_scale[tty_index] = 1U;

        if (clks_tty_cursor_row[tty_index] >= clks_tty_content_rows()) {
            while (clks_tty_cursor_row[tty_index] >= clks_tty_content_rows()) {
                clks_tty_scroll_up(tty_index);
                clks_tty_cursor_row[tty_index]--;
            }
        }

        return;
    }

    if (ch == '\b') {
        u8 saved_scale = clks_tty_ansi[tty_index].font_scale;
        clks_tty_ansi[tty_index].font_scale = 1U;

        if (col == 0U && row == 0U) {
            clks_tty_ansi[tty_index].font_scale = saved_scale;
            return;
        }

        if (col == 0U) {
            row--;
            col = clks_tty_cols - 1U;
        } else {
            col--;
        }

        clks_tty_put_visible(tty_index, row, col, ' ');
        clks_tty_cursor_row[tty_index] = row;
        clks_tty_cursor_col[tty_index] = col;
        clks_tty_ansi[tty_index].font_scale = saved_scale;
        return;
    }

    if (ch == '\t') {
        clks_tty_put_char_raw(tty_index, ' ');
        clks_tty_put_char_raw(tty_index, ' ');
        clks_tty_put_char_raw(tty_index, ' ');
        clks_tty_put_char_raw(tty_index, ' ');
        return;
    }

    if (scale > 1U) {
        while ((clks_tty_cursor_row[tty_index] + scale) > clks_tty_content_rows()) {
            clks_tty_scroll_up(tty_index);
            if (clks_tty_cursor_row[tty_index] > 0U) {
                clks_tty_cursor_row[tty_index]--;
            } else {
                break;
            }
        }

        row = clks_tty_cursor_row[tty_index];

        if (col + scale > clks_tty_cols) {
            clks_tty_cursor_col[tty_index] = 0U;
            clks_tty_cursor_row[tty_index] += clks_tty_line_scale[tty_index];
            clks_tty_line_scale[tty_index] = 1U;

            while (clks_tty_cursor_row[tty_index] >= clks_tty_content_rows()) {
                clks_tty_scroll_up(tty_index);
                clks_tty_cursor_row[tty_index]--;
            }

            while ((clks_tty_cursor_row[tty_index] + scale) > clks_tty_content_rows()) {
                clks_tty_scroll_up(tty_index);
                if (clks_tty_cursor_row[tty_index] > 0U) {
                    clks_tty_cursor_row[tty_index]--;
                } else {
                    break;
                }
            }

            row = clks_tty_cursor_row[tty_index];
            col = clks_tty_cursor_col[tty_index];
        }

        if (clks_tty_line_scale[tty_index] < scale) {
            clks_tty_line_scale[tty_index] = (u8)scale;
        }
    }

    clks_tty_put_visible(tty_index, row, col, ch);
    clks_tty_advance_after_visible(tty_index, scale);
}

