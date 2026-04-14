#include <clks/framebuffer.h>
#include <clks/string.h>
#include <clks/tty.h>
#include <clks/types.h>

#define CLKS_TTY_COUNT 4
#define CLKS_TTY_MAX_ROWS 128
#define CLKS_TTY_MAX_COLS 256

#define CLKS_TTY_FG 0x00E6E6E6U
#define CLKS_TTY_BG 0x00101010U
#define CLKS_TTY_CURSOR_BLINK_INTERVAL_TICKS 5ULL
#define CLKS_TTY_BLINK_TICK_UNSET 0xFFFFFFFFFFFFFFFFULL
#define CLKS_TTY_DESKTOP_INDEX 1U
#define CLKS_TTY_ANSI_MAX_LEN 31U
#define CLKS_TTY_SCROLLBACK_LINES 256U

typedef struct clks_tty_ansi_state {
    clks_bool in_escape;
    clks_bool saw_csi;
    clks_bool bold;
    u32 len;
    char params[CLKS_TTY_ANSI_MAX_LEN + 1U];
} clks_tty_ansi_state;

static char clks_tty_cells[CLKS_TTY_COUNT][CLKS_TTY_MAX_ROWS][CLKS_TTY_MAX_COLS];
static u32 clks_tty_cell_fg[CLKS_TTY_COUNT][CLKS_TTY_MAX_ROWS][CLKS_TTY_MAX_COLS];
static u32 clks_tty_cell_bg[CLKS_TTY_COUNT][CLKS_TTY_MAX_ROWS][CLKS_TTY_MAX_COLS];
static u32 clks_tty_cursor_row[CLKS_TTY_COUNT];
static u32 clks_tty_cursor_col[CLKS_TTY_COUNT];
static u32 clks_tty_current_fg[CLKS_TTY_COUNT];
static u32 clks_tty_current_bg[CLKS_TTY_COUNT];
static clks_tty_ansi_state clks_tty_ansi[CLKS_TTY_COUNT];
static char clks_tty_scrollback_cells[CLKS_TTY_COUNT][CLKS_TTY_SCROLLBACK_LINES][CLKS_TTY_MAX_COLS];
static u32 clks_tty_scrollback_fg[CLKS_TTY_COUNT][CLKS_TTY_SCROLLBACK_LINES][CLKS_TTY_MAX_COLS];
static u32 clks_tty_scrollback_bg[CLKS_TTY_COUNT][CLKS_TTY_SCROLLBACK_LINES][CLKS_TTY_MAX_COLS];
static u32 clks_tty_scrollback_head[CLKS_TTY_COUNT];
static u32 clks_tty_scrollback_count[CLKS_TTY_COUNT];
static u32 clks_tty_scrollback_offset[CLKS_TTY_COUNT];

static u32 clks_tty_rows = 0;
static u32 clks_tty_cols = 0;
static u32 clks_tty_active_index = 0;
static u32 clks_tty_cell_width = 8U;
static u32 clks_tty_cell_height = 8U;
static clks_bool clks_tty_is_ready = CLKS_FALSE;
static clks_bool clks_tty_cursor_visible = CLKS_FALSE;
static clks_bool clks_tty_blink_enabled = CLKS_TRUE;
static u64 clks_tty_blink_last_tick = CLKS_TTY_BLINK_TICK_UNSET;

static u32 clks_tty_ansi_palette(u32 index) {
    static const u32 palette[16] = {
        0x00000000U, 0x00CD3131U, 0x000DBC79U, 0x00E5E510U,
        0x002472C8U, 0x00BC3FBCU, 0x0011A8CDU, 0x00E5E5E5U,
        0x00666666U, 0x00F14C4CU, 0x0023D18BU, 0x00F5F543U,
        0x003B8EEAU, 0x00D670D6U, 0x0029B8DBU, 0x00FFFFFFU
    };

    if (index < 16U) {
        return palette[index];
    }

    return CLKS_TTY_FG;
}

static void clks_tty_reset_blink_timer(void) {
    clks_tty_blink_last_tick = CLKS_TTY_BLINK_TICK_UNSET;
}

static void clks_tty_draw_cell_with_colors(u32 row, u32 col, char ch, u32 fg, u32 bg) {
    clks_fb_draw_char(col * clks_tty_cell_width, row * clks_tty_cell_height, ch, fg, bg);
}

static void clks_tty_draw_cell(u32 tty_index, u32 row, u32 col) {
    clks_tty_draw_cell_with_colors(
        row,
        col,
        clks_tty_cells[tty_index][row][col],
        clks_tty_cell_fg[tty_index][row][col],
        clks_tty_cell_bg[tty_index][row][col]
    );
}

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

    clks_memcpy(clks_tty_scrollback_cells[tty_index][slot], clks_tty_cells[tty_index][row], clks_tty_cols);
    clks_memcpy(
        clks_tty_scrollback_fg[tty_index][slot],
        clks_tty_cell_fg[tty_index][row],
        (usize)clks_tty_cols * sizeof(u32)
    );
    clks_memcpy(
        clks_tty_scrollback_bg[tty_index][slot],
        clks_tty_cell_bg[tty_index][row],
        (usize)clks_tty_cols * sizeof(u32)
    );

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


static void clks_tty_reset_color_state(u32 tty_index) {
    clks_tty_current_fg[tty_index] = CLKS_TTY_FG;
    clks_tty_current_bg[tty_index] = CLKS_TTY_BG;
    clks_tty_ansi[tty_index].bold = CLKS_FALSE;
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
    }
}

static void clks_tty_clear_tty(u32 tty_index) {
    u32 row;

    for (row = 0; row < clks_tty_rows; row++) {
        clks_tty_fill_row(tty_index, row, ' ');
    }

    clks_tty_cursor_row[tty_index] = 0U;
    clks_tty_cursor_col[tty_index] = 0U;
    clks_tty_scrollback_head[tty_index] = 0U;
    clks_tty_scrollback_count[tty_index] = 0U;
    clks_tty_scrollback_offset[tty_index] = 0U;
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

    if (row < clks_tty_rows && col < clks_tty_cols) {
        clks_tty_draw_cell(clks_tty_active_index, row, col);
    }

    clks_tty_cursor_visible = CLKS_FALSE;
}

static void clks_tty_draw_cursor(void) {
    u32 row;
    u32 col;
    u32 fg;
    u32 bg;
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

    if (row >= clks_tty_rows || col >= clks_tty_cols) {
        clks_tty_cursor_visible = CLKS_FALSE;
        return;
    }

    ch = clks_tty_cells[clks_tty_active_index][row][col];
    fg = clks_tty_cell_fg[clks_tty_active_index][row][col];
    bg = clks_tty_cell_bg[clks_tty_active_index][row][col];

    clks_tty_draw_cell_with_colors(row, col, ch, bg, fg);
    clks_tty_cursor_visible = CLKS_TRUE;
}

static void clks_tty_redraw_active(void) {
    u32 row;
    u32 col;
    u32 tty_index = clks_tty_active_index;
    u32 scroll_count = clks_tty_scrollback_count[tty_index];
    u32 scroll_offset = clks_tty_scrollback_clamped_offset(tty_index);
    u32 start_doc = (scroll_count >= scroll_offset) ? (scroll_count - scroll_offset) : 0U;

    clks_fb_clear(CLKS_TTY_BG);
    clks_tty_cursor_visible = CLKS_FALSE;

    for (row = 0; row < clks_tty_rows; row++) {
        u32 doc_index = start_doc + row;

        if (doc_index < scroll_count) {
            u32 phys = clks_tty_scrollback_logical_to_physical(tty_index, doc_index);

            for (col = 0; col < clks_tty_cols; col++) {
                clks_tty_draw_cell_with_colors(
                    row,
                    col,
                    clks_tty_scrollback_cells[tty_index][phys][col],
                    clks_tty_scrollback_fg[tty_index][phys][col],
                    clks_tty_scrollback_bg[tty_index][phys][col]
                );
            }

            continue;
        }

        {
            u32 src_row = doc_index - scroll_count;

            if (src_row >= clks_tty_rows) {
                continue;
            }

            for (col = 0; col < clks_tty_cols; col++) {
                clks_tty_draw_cell_with_colors(
                    row,
                    col,
                    clks_tty_cells[tty_index][src_row][col],
                    clks_tty_cell_fg[tty_index][src_row][col],
                    clks_tty_cell_bg[tty_index][src_row][col]
                );
            }
        }
    }

    if (scroll_offset == 0U) {
        clks_tty_draw_cursor();
    }
}
static void clks_tty_scroll_up(u32 tty_index) {
    u32 row;

    clks_tty_scrollback_push_row(tty_index, 0U);

    for (row = 1; row < clks_tty_rows; row++) {
        clks_memcpy(clks_tty_cells[tty_index][row - 1U], clks_tty_cells[tty_index][row], clks_tty_cols);
        clks_memcpy(
            clks_tty_cell_fg[tty_index][row - 1U],
            clks_tty_cell_fg[tty_index][row],
            (usize)clks_tty_cols * sizeof(u32)
        );
        clks_memcpy(
            clks_tty_cell_bg[tty_index][row - 1U],
            clks_tty_cell_bg[tty_index][row],
            (usize)clks_tty_cols * sizeof(u32)
        );
    }

    clks_tty_fill_row(tty_index, clks_tty_rows - 1U, ' ');

    if (tty_index == clks_tty_active_index) {
        if (clks_tty_scrollback_is_active(tty_index) == CLKS_TRUE) {
            clks_tty_redraw_active();
        } else {
            u32 col;

            clks_fb_scroll_up(clks_tty_cell_height, CLKS_TTY_BG);

            for (col = 0U; col < clks_tty_cols; col++) {
                clks_tty_draw_cell(tty_index, clks_tty_rows - 1U, col);
            }
        }
    }
}
static void clks_tty_put_visible(u32 tty_index, u32 row, u32 col, char ch) {
    clks_tty_cells[tty_index][row][col] = ch;
    clks_tty_cell_fg[tty_index][row][col] = clks_tty_current_fg[tty_index];
    clks_tty_cell_bg[tty_index][row][col] = clks_tty_current_bg[tty_index];

    if (tty_index == clks_tty_active_index) {
        clks_tty_draw_cell(tty_index, row, col);
    }
}

static void clks_tty_put_char_raw(u32 tty_index, char ch) {
    u32 row = clks_tty_cursor_row[tty_index];
    u32 col = clks_tty_cursor_col[tty_index];

    if (ch == '\r') {
        clks_tty_cursor_col[tty_index] = 0U;
        return;
    }

    if (ch == '\n') {
        clks_tty_cursor_col[tty_index] = 0U;
        clks_tty_cursor_row[tty_index]++;

        if (clks_tty_cursor_row[tty_index] >= clks_tty_rows) {
            clks_tty_scroll_up(tty_index);
            clks_tty_cursor_row[tty_index] = clks_tty_rows - 1U;
        }

        return;
    }

    if (ch == '\b') {
        if (col == 0U && row == 0U) {
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
        return;
    }

    if (ch == '\t') {
        clks_tty_put_char_raw(tty_index, ' ');
        clks_tty_put_char_raw(tty_index, ' ');
        clks_tty_put_char_raw(tty_index, ' ');
        clks_tty_put_char_raw(tty_index, ' ');
        return;
    }

    clks_tty_put_visible(tty_index, row, col, ch);
    clks_tty_cursor_col[tty_index]++;

    if (clks_tty_cursor_col[tty_index] >= clks_tty_cols) {
        clks_tty_cursor_col[tty_index] = 0U;
        clks_tty_cursor_row[tty_index]++;

        if (clks_tty_cursor_row[tty_index] >= clks_tty_rows) {
            clks_tty_scroll_up(tty_index);
            clks_tty_cursor_row[tty_index] = clks_tty_rows - 1U;
        }
    }
}

static void clks_tty_ansi_apply_sgr_code(u32 tty_index, u32 code) {
    if (code == 0U) {
        clks_tty_reset_color_state(tty_index);
        return;
    }

    if (code == 1U) {
        clks_tty_ansi[tty_index].bold = CLKS_TRUE;
        return;
    }

    if (code == 22U) {
        clks_tty_ansi[tty_index].bold = CLKS_FALSE;
        return;
    }

    if (code == 39U) {
        clks_tty_current_fg[tty_index] = CLKS_TTY_FG;
        return;
    }

    if (code == 49U) {
        clks_tty_current_bg[tty_index] = CLKS_TTY_BG;
        return;
    }

    if (code >= 30U && code <= 37U) {
        u32 idx = code - 30U;

        if (clks_tty_ansi[tty_index].bold == CLKS_TRUE) {
            idx += 8U;
        }

        clks_tty_current_fg[tty_index] = clks_tty_ansi_palette(idx);
        return;
    }

    if (code >= 90U && code <= 97U) {
        clks_tty_current_fg[tty_index] = clks_tty_ansi_palette((code - 90U) + 8U);
        return;
    }

    if (code >= 40U && code <= 47U) {
        clks_tty_current_bg[tty_index] = clks_tty_ansi_palette(code - 40U);
        return;
    }

    if (code >= 100U && code <= 107U) {
        clks_tty_current_bg[tty_index] = clks_tty_ansi_palette((code - 100U) + 8U);
        return;
    }
}

static void clks_tty_ansi_apply_sgr_params(u32 tty_index, const char *params, u32 len) {
    u32 i;
    u32 value = 0U;
    clks_bool has_digit = CLKS_FALSE;

    if (len == 0U) {
        clks_tty_ansi_apply_sgr_code(tty_index, 0U);
        return;
    }

    for (i = 0U; i <= len; i++) {
        char ch = (i < len) ? params[i] : ';';

        if (ch >= '0' && ch <= '9') {
            has_digit = CLKS_TRUE;
            value = (value * 10U) + (u32)(ch - '0');
            continue;
        }

        if (ch == ';') {
            if (has_digit == CLKS_TRUE) {
                clks_tty_ansi_apply_sgr_code(tty_index, value);
            } else {
                clks_tty_ansi_apply_sgr_code(tty_index, 0U);
            }

            value = 0U;
            has_digit = CLKS_FALSE;
            continue;
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

        clks_tty_reset_ansi_state(tty_index);
        return CLKS_FALSE;
    }

    if ((ch >= '0' && ch <= '9') || ch == ';') {
        if (state->len < CLKS_TTY_ANSI_MAX_LEN) {
            state->params[state->len++] = ch;
            state->params[state->len] = '\0';
        } else {
            clks_tty_reset_ansi_state(tty_index);
        }

        return CLKS_TRUE;
    }

    if (ch == 'm') {
        clks_tty_ansi_apply_sgr_params(tty_index, state->params, state->len);
        clks_tty_reset_ansi_state(tty_index);
        return CLKS_TRUE;
    }

    if (ch == 'J') {
        if (state->len == 0U || (state->len == 1U && state->params[0] == '2')) {
            clks_tty_clear_tty(tty_index);

            if (tty_index == clks_tty_active_index) {
                clks_tty_redraw_active();
            }
        }

        clks_tty_reset_ansi_state(tty_index);
        return CLKS_TRUE;
    }

    if (ch == 'H') {
        clks_tty_cursor_row[tty_index] = 0U;
        clks_tty_cursor_col[tty_index] = 0U;
        clks_tty_reset_ansi_state(tty_index);
        return CLKS_TRUE;
    }

    clks_tty_reset_ansi_state(tty_index);
    return CLKS_TRUE;
}

void clks_tty_init(void) {
    struct clks_framebuffer_info info;
    u32 tty;

    if (clks_fb_ready() == CLKS_FALSE) {
        clks_tty_is_ready = CLKS_FALSE;
        return;
    }

    info = clks_fb_info();
    clks_tty_cell_width = clks_fb_cell_width();
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
    clks_tty_reset_blink_timer();
    clks_tty_redraw_active();
}

void clks_tty_write_char(char ch) {
    u32 tty_index;

    if (clks_tty_is_ready == CLKS_FALSE) {
        return;
    }

    clks_tty_hide_cursor();

    tty_index = clks_tty_active_index;

    if (clks_tty_scrollback_is_active(tty_index) == CLKS_TRUE) {
        clks_tty_scrollback_follow_tail(tty_index);
        clks_tty_redraw_active();
    }

    if (clks_tty_ansi_process_byte(tty_index, ch) == CLKS_FALSE) {
        clks_tty_put_char_raw(tty_index, ch);
    }

    clks_tty_draw_cursor();
    clks_tty_reset_blink_timer();
}

void clks_tty_write(const char *text) {
    usize i = 0U;
    u32 tty_index;

    if (clks_tty_is_ready == CLKS_FALSE || text == CLKS_NULL) {
        return;
    }

    clks_tty_hide_cursor();
    tty_index = clks_tty_active_index;

    if (clks_tty_scrollback_is_active(tty_index) == CLKS_TRUE) {
        clks_tty_scrollback_follow_tail(tty_index);
        clks_tty_redraw_active();
    }

    while (text[i] != '\0') {
        if (clks_tty_ansi_process_byte(tty_index, text[i]) == CLKS_FALSE) {
            clks_tty_put_char_raw(tty_index, text[i]);
        }

        i++;
    }

    clks_tty_draw_cursor();
    clks_tty_reset_blink_timer();
}

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
    next_offset = clks_tty_scrollback_clamped_offset(tty_index) + clks_tty_rows;

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

    if (current_offset > clks_tty_rows) {
        next_offset = current_offset - clks_tty_rows;
    } else {
        next_offset = 0U;
    }

    if (next_offset != clks_tty_scrollback_offset[tty_index]) {
        clks_tty_scrollback_offset[tty_index] = next_offset;
        clks_tty_redraw_active();
        clks_tty_reset_blink_timer();
    }
}


u32 clks_tty_active(void) {
    return clks_tty_active_index;
}

u32 clks_tty_count(void) {
    return CLKS_TTY_COUNT;
}

clks_bool clks_tty_ready(void) {
    return clks_tty_is_ready;
}
