static u32 clks_tty_scrollback_logical_to_physical(u32 tty_index, u32 logical_index) {
    u32 count = clks_tty_scrollback_count[tty_index];

    if (count < CLKS_TTY_SCROLLBACK_LINES) {
        return logical_index;
    }

    return (clks_tty_scrollback_head[tty_index] + logical_index) % CLKS_TTY_SCROLLBACK_LINES;
}

static u32 clks_tty_scrollback_max_offset(u32 tty_index) {
    return clks_tty_scrollback_count[tty_index];
}

static u32 clks_tty_scrollback_clamped_offset(u32 tty_index) {
    u32 max_offset = clks_tty_scrollback_max_offset(tty_index);

    if (clks_tty_scrollback_offset[tty_index] > max_offset) {
        clks_tty_scrollback_offset[tty_index] = max_offset;
    }

    return clks_tty_scrollback_offset[tty_index];
}

static void clks_tty_scrollback_push_row(u32 tty_index, u32 row) {
    u32 slot = clks_tty_scrollback_head[tty_index];

    /* Save the line before scroll nukes it, because users always want it back. */
    clks_memcpy(clks_tty_scrollback_cells[tty_index][slot], clks_tty_cells[tty_index][row],
                (usize)clks_tty_cols * sizeof(u32));
    clks_memcpy(clks_tty_scrollback_fg[tty_index][slot], clks_tty_cell_fg[tty_index][row],
                (usize)clks_tty_cols * sizeof(u32));
    clks_memcpy(clks_tty_scrollback_bg[tty_index][slot], clks_tty_cell_bg[tty_index][row],
                (usize)clks_tty_cols * sizeof(u32));
    clks_memcpy(clks_tty_scrollback_style[tty_index][slot], clks_tty_cell_style[tty_index][row], clks_tty_cols);
    clks_memcpy(clks_tty_scrollback_scale[tty_index][slot], clks_tty_cell_scale[tty_index][row], clks_tty_cols);

    clks_tty_scrollback_head[tty_index] = (slot + 1U) % CLKS_TTY_SCROLLBACK_LINES;

    if (clks_tty_scrollback_count[tty_index] < CLKS_TTY_SCROLLBACK_LINES) {
        clks_tty_scrollback_count[tty_index]++;
    }
}

static clks_bool clks_tty_scrollback_is_active(u32 tty_index) {
    return (clks_tty_scrollback_offset[tty_index] > 0U) ? CLKS_TRUE : CLKS_FALSE;
}

static void clks_tty_scrollback_follow_tail(u32 tty_index) {
    clks_tty_scrollback_offset[tty_index] = 0U;
}

static u32 clks_tty_content_rows(void) {
    if (clks_tty_rows > 1U) {
        return clks_tty_rows - 1U;
    }

    return clks_tty_rows;
}

static u32 clks_tty_scrollback_page_step(void) {
    u32 rows = clks_tty_content_rows();

    if (rows > 2U) {
        return rows - 2U;
    }

    return 1U;
}

static clks_bool clks_tty_status_bar_enabled(void) {
    return (clks_tty_rows > 1U) ? CLKS_TRUE : CLKS_FALSE;
}

static void clks_tty_status_append_char(char *line, u32 line_size, u32 *cursor, char ch) {
    if (line == CLKS_NULL || cursor == CLKS_NULL || *cursor >= line_size) {
        return;
    }

    line[*cursor] = ch;
    (*cursor)++;
}

static void clks_tty_status_append_text(char *line, u32 line_size, u32 *cursor, const char *text) {
    u32 i = 0U;

    if (text == CLKS_NULL) {
        return;
    }

    while (text[i] != '\0') {
        clks_tty_status_append_char(line, line_size, cursor, text[i]);
        i++;
    }
}

static void clks_tty_status_append_u32_dec(char *line, u32 line_size, u32 *cursor, u32 value) {
    char tmp[10];
    u32 len = 0U;

    if (value == 0U) {
        clks_tty_status_append_char(line, line_size, cursor, '0');
        return;
    }

    while (value > 0U && len < (u32)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (len > 0U) {
        len--;
        clks_tty_status_append_char(line, line_size, cursor, tmp[len]);
    }
}

static void clks_tty_status_invalidate(void) {
    clks_tty_status_cache_valid = CLKS_FALSE;
}

static u32 clks_tty_ascii_tolower(u32 codepoint) {
    if (codepoint >= (u32)'A' && codepoint <= (u32)'Z') {
        return codepoint + ((u32)'a' - (u32)'A');
    }

    return codepoint;
}

static u32 clks_tty_doc_char_at(u32 tty_index, u32 doc_index, u32 col) {
    u32 scroll_count;
    u32 phys;
    u32 row;

    if (tty_index >= CLKS_TTY_COUNT || col >= clks_tty_cols) {
        return (u32)' ';
    }

    scroll_count = clks_tty_scrollback_count[tty_index];
    if (doc_index < scroll_count) {
        phys = clks_tty_scrollback_logical_to_physical(tty_index, doc_index);
        if (clks_tty_scrollback_cells[tty_index][phys][col] == CLKS_TTY_CELL_CONTINUATION) {
            return (u32)' ';
        }
        return clks_tty_scrollback_cells[tty_index][phys][col];
    }

    row = doc_index - scroll_count;
    if (row < clks_tty_content_rows()) {
        if (clks_tty_cells[tty_index][row][col] == CLKS_TTY_CELL_CONTINUATION) {
            return (u32)' ';
        }
        return clks_tty_cells[tty_index][row][col];
    }

    return (u32)' ';
}

static clks_bool clks_tty_doc_row_contains_query(u32 tty_index, u32 doc_index) {
    u32 query_len;
    u32 col;
    u32 q;

    if (tty_index >= CLKS_TTY_COUNT || clks_tty_cols == 0U) {
        return CLKS_FALSE;
    }

    query_len = clks_tty_search_len[tty_index];
    if (query_len == 0U || query_len >= CLKS_TTY_SEARCH_MAX || query_len > clks_tty_cols) {
        return CLKS_FALSE;
    }

    for (col = 0U; col + query_len <= clks_tty_cols; col++) {
        clks_bool same = CLKS_TRUE;

        for (q = 0U; q < query_len; q++) {
            u32 left = clks_tty_ascii_tolower(clks_tty_doc_char_at(tty_index, doc_index, col + q));
            u32 right = clks_tty_ascii_tolower((u32)(u8)clks_tty_search_query[tty_index][q]);

            if (left != right) {
                same = CLKS_FALSE;
                break;
            }
        }

        if (same == CLKS_TRUE) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static void clks_tty_scroll_to_doc_row(u32 tty_index, u32 doc_index) {
    u32 scroll_count;
    u32 content_rows;
    u32 max_offset;
    u32 offset;

    if (tty_index >= CLKS_TTY_COUNT) {
        return;
    }

    scroll_count = clks_tty_scrollback_count[tty_index];
    content_rows = clks_tty_content_rows();
    max_offset = clks_tty_scrollback_max_offset(tty_index);

    if (doc_index >= scroll_count) {
        offset = 0U;
    } else if (scroll_count - doc_index < content_rows) {
        offset = scroll_count - doc_index;
    } else {
        offset = scroll_count - doc_index;
    }

    if (offset > max_offset) {
        offset = max_offset;
    }

    clks_tty_scrollback_offset[tty_index] = offset;
}

static clks_bool clks_tty_search_find(u32 tty_index, clks_bool reverse) {
    u32 scroll_count;
    u32 doc_rows;
    u32 start_doc;
    u32 i;

    if (tty_index >= CLKS_TTY_COUNT || clks_tty_search_len[tty_index] == 0U) {
        return CLKS_FALSE;
    }

    scroll_count = clks_tty_scrollback_count[tty_index];
    doc_rows = scroll_count;
    if (doc_rows == 0U) {
        return CLKS_FALSE;
    }

    if (clks_tty_search_has_match[tty_index] == CLKS_TRUE && clks_tty_search_match_doc[tty_index] < doc_rows) {
        start_doc = clks_tty_search_match_doc[tty_index];
    } else {
        u32 offset = clks_tty_scrollback_clamped_offset(tty_index);
        start_doc = (scroll_count >= offset) ? (scroll_count - offset) : 0U;
    }

    for (i = 1U; i <= doc_rows; i++) {
        u32 candidate;

        if (reverse == CLKS_TRUE) {
            candidate = (start_doc + doc_rows - i) % doc_rows;
        } else {
            candidate = (start_doc + i) % doc_rows;
        }

        if (clks_tty_doc_row_contains_query(tty_index, candidate) == CLKS_TRUE) {
            clks_tty_search_has_match[tty_index] = CLKS_TRUE;
            clks_tty_search_not_found[tty_index] = CLKS_FALSE;
            clks_tty_search_match_doc[tty_index] = candidate;
            clks_tty_scroll_to_doc_row(tty_index, candidate);
            return CLKS_TRUE;
        }
    }

    clks_tty_search_not_found[tty_index] = CLKS_TRUE;
    return CLKS_FALSE;
}
