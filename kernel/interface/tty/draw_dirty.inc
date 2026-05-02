static u32 clks_tty_ansi_palette(u32 index) {
    /* Good-enough ANSI colors, because perfect fidelity can wait until never. */
    static const u32 palette[16] = {0x00000000U, 0x00CD3131U, 0x000DBC79U, 0x00E5E510U, 0x002472C8U, 0x00BC3FBCU,
                                    0x0011A8CDU, 0x00E5E5E5U, 0x00666666U, 0x00F14C4CU, 0x0023D18BU, 0x00F5F543U,
                                    0x003B8EEAU, 0x00D670D6U, 0x0029B8DBU, 0x00FFFFFFU};

    if (index < 16U) {
        return palette[index];
    }

    return CLKS_TTY_FG;
}

static void clks_tty_reset_blink_timer(void) {
    clks_tty_blink_last_tick = CLKS_TTY_BLINK_TICK_UNSET;
}

static u32 clks_tty_clamp_scale(u32 scale) {
    if (scale < CLKS_TTY_FONT_SCALE_MIN) {
        return CLKS_TTY_FONT_SCALE_MIN;
    }

    if (scale > CLKS_TTY_FONT_SCALE_MAX) {
        return CLKS_TTY_FONT_SCALE_MAX;
    }

    return scale;
}

static void clks_tty_draw_cell_with_colors(u32 row, u32 col, char ch, u32 fg, u32 bg, u8 style, u8 scale) {
    if (ch == CLKS_TTY_CELL_CONTINUATION) {
        return;
    }

    scale = (u8)clks_tty_clamp_scale((u32)scale);
    clks_fb_draw_char_scaled(col * clks_tty_cell_width, row * clks_tty_cell_height, ch, fg, bg, (u32)style,
                             (u32)scale);
}

static void clks_tty_draw_cell(u32 tty_index, u32 row, u32 col) {
    clks_tty_draw_cell_with_colors(row, col, clks_tty_cells[tty_index][row][col], clks_tty_cell_fg[tty_index][row][col],
                                   clks_tty_cell_bg[tty_index][row][col], clks_tty_cell_style[tty_index][row][col],
                                   clks_tty_cell_scale[tty_index][row][col]);
}

static void clks_tty_dirty_reset(void) {
    u32 row;

    clks_memset(clks_tty_dirty_rows, 0, sizeof(clks_tty_dirty_rows));
    for (row = 0U; row < CLKS_TTY_MAX_ROWS; row++) {
        clks_tty_dirty_col_min[row] = CLKS_TTY_MAX_COLS;
        clks_tty_dirty_col_max[row] = 0U;
    }
    clks_tty_dirty_any = CLKS_FALSE;
}

static void clks_tty_dirty_mark_row(u32 row) {
    if (row >= clks_tty_content_rows() || row >= CLKS_TTY_MAX_ROWS) {
        return;
    }

    clks_tty_dirty_rows[row] = CLKS_TRUE;
    clks_tty_dirty_col_min[row] = 0U;
    clks_tty_dirty_col_max[row] = (clks_tty_cols > 0U) ? (clks_tty_cols - 1U) : 0U;
    clks_tty_dirty_any = CLKS_TRUE;
}

static void clks_tty_dirty_mark_cell(u32 row, u32 col) {
    if (row >= clks_tty_content_rows() || row >= CLKS_TTY_MAX_ROWS) {
        return;
    }

    if (col >= clks_tty_cols) {
        return;
    }

    if (clks_tty_dirty_rows[row] != CLKS_TRUE) {
        clks_tty_dirty_rows[row] = CLKS_TRUE;
        clks_tty_dirty_col_min[row] = col;
        clks_tty_dirty_col_max[row] = col;
        clks_tty_dirty_any = CLKS_TRUE;
        return;
    }

    if (clks_tty_dirty_col_min[row] > col) {
        clks_tty_dirty_col_min[row] = col;
    }

    if (clks_tty_dirty_col_max[row] < col) {
        clks_tty_dirty_col_max[row] = col;
    }
}

static void clks_tty_dirty_shift_up_one(void) {
    u32 row;
    u32 content_rows = clks_tty_content_rows();

    if (content_rows == 0U) {
        return;
    }

    for (row = 1U; row < content_rows; row++) {
        clks_tty_dirty_rows[row - 1U] = clks_tty_dirty_rows[row];
        clks_tty_dirty_col_min[row - 1U] = clks_tty_dirty_col_min[row];
        clks_tty_dirty_col_max[row - 1U] = clks_tty_dirty_col_max[row];
    }

    clks_tty_dirty_rows[content_rows - 1U] = CLKS_FALSE;
    clks_tty_dirty_col_min[content_rows - 1U] = CLKS_TTY_MAX_COLS;
    clks_tty_dirty_col_max[content_rows - 1U] = 0U;

    clks_tty_dirty_any = CLKS_FALSE;
    for (row = 0U; row < content_rows; row++) {
        if (clks_tty_dirty_rows[row] == CLKS_TRUE) {
            clks_tty_dirty_any = CLKS_TRUE;
            break;
        }
    }
}

static void clks_tty_flush_dirty(void) {
    u32 tty_index;
    u32 row;
    u32 col;
    u32 col_start;
    u32 col_end;

    if (clks_tty_is_ready == CLKS_FALSE || clks_tty_dirty_any == CLKS_FALSE) {
        return;
    }

    tty_index = clks_tty_active_index;

    if (clks_tty_scrollback_is_active(tty_index) == CLKS_TRUE) {
        clks_tty_redraw_active();
        clks_tty_dirty_reset();
        return;
    }

    /* Dirty-row redraw: cheap as hell, but it keeps typing snappy. */
    for (row = 0U; row < clks_tty_content_rows(); row++) {
        if (clks_tty_dirty_rows[row] != CLKS_TRUE) {
            continue;
        }

        col_start = clks_tty_dirty_col_min[row];
        col_end = clks_tty_dirty_col_max[row];

        if (col_start >= clks_tty_cols) {
            continue;
        }

        if (col_end >= clks_tty_cols) {
            col_end = clks_tty_cols - 1U;
        }

        if (col_start > col_end) {
            continue;
        }

        for (col = col_start; col <= col_end; col++) {
            clks_tty_draw_cell(tty_index, row, col);
        }
    }

    clks_tty_dirty_reset();
}

