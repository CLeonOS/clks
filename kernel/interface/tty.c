#include <clks/display.h>
#include <clks/exec.h>
#include <clks/framebuffer.h>
#include <clks/inputm.h>
#include <clks/string.h>
#include <clks/tty.h>
#include <clks/types.h>

/* Terminal code: where simple text output turns into a whole damn life choice. */

#define CLKS_TTY_COUNT 4
#define CLKS_TTY_MAX_ROWS 128
#define CLKS_TTY_MAX_COLS 256

#define CLKS_TTY_FG 0x00E6E6E6U
#define CLKS_TTY_BG 0x00101010U
#define CLKS_TTY_CURSOR_BLINK_INTERVAL_TICKS 5ULL
#define CLKS_TTY_BLINK_TICK_UNSET 0xFFFFFFFFFFFFFFFFULL
#define CLKS_TTY_DESKTOP_INDEX 1U
#define CLKS_TTY_ANSI_MAX_LEN 95U
#define CLKS_TTY_SCROLLBACK_LINES 256U
#define CLKS_TTY_STYLE_NONE 0U
#define CLKS_TTY_STYLE_BOLD ((u8)CLKS_FB_STYLE_BOLD)
#define CLKS_TTY_STYLE_UNDERLINE ((u8)CLKS_FB_STYLE_UNDERLINE)
#define CLKS_TTY_CELL_CONTINUATION 0U
#define CLKS_TTY_UTF8_REPLACEMENT 0xFFFDU
#define CLKS_TTY_FONT_SCALE_MIN 1U
#define CLKS_TTY_FONT_SCALE_MAX 3U
#define CLKS_TTY_STATUS_BG 0x00202020U
#define CLKS_TTY_STATUS_FG 0x00E6E6E6U
#define CLKS_TTY_STATUS_STYLE CLKS_TTY_STYLE_BOLD
#define CLKS_TTY_SCROLLBAR_TRACK 0x002A2A2AU
#define CLKS_TTY_SCROLLBAR_THUMB 0x00A8A8A8U
#define CLKS_TTY_SCROLLBAR_W 4U
#define CLKS_TTY_SEARCH_MAX 32U

typedef struct clks_tty_ansi_state {
    clks_bool in_escape;
    clks_bool saw_csi;
    clks_bool bold;
    clks_bool underline;
    clks_bool inverse;
    u8 font_scale;
    u32 saved_row;
    u32 saved_col;
    u32 len;
    char params[CLKS_TTY_ANSI_MAX_LEN + 1U];
} clks_tty_ansi_state;

typedef struct clks_tty_utf8_state {
    u32 codepoint;
    u8 remaining;
    u8 expected;
} clks_tty_utf8_state;

static u32 clks_tty_cells[CLKS_TTY_COUNT][CLKS_TTY_MAX_ROWS][CLKS_TTY_MAX_COLS];
static u32 clks_tty_cell_fg[CLKS_TTY_COUNT][CLKS_TTY_MAX_ROWS][CLKS_TTY_MAX_COLS];
static u32 clks_tty_cell_bg[CLKS_TTY_COUNT][CLKS_TTY_MAX_ROWS][CLKS_TTY_MAX_COLS];
static u8 clks_tty_cell_style[CLKS_TTY_COUNT][CLKS_TTY_MAX_ROWS][CLKS_TTY_MAX_COLS];
static u8 clks_tty_cell_scale[CLKS_TTY_COUNT][CLKS_TTY_MAX_ROWS][CLKS_TTY_MAX_COLS];
static u32 clks_tty_cursor_row[CLKS_TTY_COUNT];
static u32 clks_tty_cursor_col[CLKS_TTY_COUNT];
static u32 clks_tty_current_fg[CLKS_TTY_COUNT];
static u32 clks_tty_current_bg[CLKS_TTY_COUNT];
static clks_tty_ansi_state clks_tty_ansi[CLKS_TTY_COUNT];
static clks_tty_utf8_state clks_tty_utf8[CLKS_TTY_COUNT];
static u8 clks_tty_line_scale[CLKS_TTY_COUNT];
static u32 clks_tty_scrollback_cells[CLKS_TTY_COUNT][CLKS_TTY_SCROLLBACK_LINES][CLKS_TTY_MAX_COLS];
static u32 clks_tty_scrollback_fg[CLKS_TTY_COUNT][CLKS_TTY_SCROLLBACK_LINES][CLKS_TTY_MAX_COLS];
static u32 clks_tty_scrollback_bg[CLKS_TTY_COUNT][CLKS_TTY_SCROLLBACK_LINES][CLKS_TTY_MAX_COLS];
static u8 clks_tty_scrollback_style[CLKS_TTY_COUNT][CLKS_TTY_SCROLLBACK_LINES][CLKS_TTY_MAX_COLS];
static u8 clks_tty_scrollback_scale[CLKS_TTY_COUNT][CLKS_TTY_SCROLLBACK_LINES][CLKS_TTY_MAX_COLS];
static u32 clks_tty_scrollback_head[CLKS_TTY_COUNT];
static u32 clks_tty_scrollback_count[CLKS_TTY_COUNT];
static u32 clks_tty_scrollback_offset[CLKS_TTY_COUNT];
static char clks_tty_search_query[CLKS_TTY_COUNT][CLKS_TTY_SEARCH_MAX];
static u32 clks_tty_search_len[CLKS_TTY_COUNT];
static clks_bool clks_tty_search_editing[CLKS_TTY_COUNT];
static clks_bool clks_tty_search_has_match[CLKS_TTY_COUNT];
static clks_bool clks_tty_search_not_found[CLKS_TTY_COUNT];
static u32 clks_tty_search_match_doc[CLKS_TTY_COUNT];

static u32 clks_tty_rows = 0;
static u32 clks_tty_cols = 0;
static u32 clks_tty_active_index = 0;
static u32 clks_tty_cell_width = 8U;
static u32 clks_tty_cell_height = 8U;
static clks_bool clks_tty_is_ready = CLKS_FALSE;
static clks_bool clks_tty_cursor_visible = CLKS_FALSE;
static clks_bool clks_tty_blink_enabled = CLKS_TRUE;
static clks_bool clks_tty_defer_draw = CLKS_FALSE;
static clks_bool clks_tty_dirty_any = CLKS_FALSE;
static clks_bool clks_tty_dirty_rows[CLKS_TTY_MAX_ROWS];
static u32 clks_tty_dirty_col_min[CLKS_TTY_MAX_ROWS];
static u32 clks_tty_dirty_col_max[CLKS_TTY_MAX_ROWS];
static u32 clks_tty_deferred_scroll_rows = 0U;
static u64 clks_tty_blink_last_tick = CLKS_TTY_BLINK_TICK_UNSET;
static clks_bool clks_tty_status_cache_valid = CLKS_FALSE;
static char clks_tty_status_cache[CLKS_TTY_MAX_COLS];

static u32 clks_tty_content_rows(void);
static clks_bool clks_tty_scrollback_is_active(u32 tty_index);
static void clks_tty_redraw_active(void);
static void clks_tty_write_n_internal(u32 tty_index, const char *text, usize len);
static void clks_tty_status_invalidate(void);
static void clks_tty_put_codepoint_raw(u32 tty_index, u32 codepoint);
static u32 clks_tty_codepoint_width(u32 codepoint);
static void clks_tty_recompute_geometry(void);

#include "tty/draw_dirty.inc"
#include "tty/scrollback_search.inc"
#include "tty/status.inc"
#include "tty/screen.inc"
#include "tty/ansi.inc"
#include "tty/output.inc"
#include "tty/switch_tick.inc"
#include "tty/scrollback_keys.inc"
#include "tty/query.inc"
