#include <clks/cpu.h>
#include <clks/exec.h>
#include <clks/framebuffer.h>
#include <clks/fs.h>
#include <clks/locale.h>
#include <clks/log.h>
#include <clks/panic.h>
#include <clks/panic_qr.h>
#include <clks/serial.h>
#include <clks/string.h>
#include <clks/types.h>

#define CLKS_PANIC_BG 0x00200000U
#define CLKS_PANIC_FG 0x00FFE0E0U
#define CLKS_PANIC_UI_BG 0x000B1018U
#define CLKS_PANIC_UI_BG_2 0x00101824U
#define CLKS_PANIC_UI_PANEL 0x00151C28U
#define CLKS_PANIC_UI_PANEL_ALT 0x001A2330U
#define CLKS_PANIC_UI_PANEL_EDGE 0x00364757U
#define CLKS_PANIC_UI_ACCENT 0x00FF3B58U
#define CLKS_PANIC_UI_ACCENT_DARK 0x009C1F34U
#define CLKS_PANIC_UI_WARN 0x00FFD166U
#define CLKS_PANIC_UI_GOOD 0x0057D68DU
#define CLKS_PANIC_UI_TEXT 0x00F5F7FAU
#define CLKS_PANIC_UI_MUTED 0x009AA7B7U
#define CLKS_PANIC_UI_DIM 0x006D7B8DU

#define CLKS_PANIC_BACKTRACE_MAX 20U
#define CLKS_PANIC_STACK_WINDOW_BYTES (128ULL * 1024ULL)
#define CLKS_PANIC_SYMBOL_FILE "/system/kernel.sym"
#define CLKS_PANIC_KERNEL_ADDR_BASE 0xFFFF800000000000ULL
#define CLKS_PANIC_QR_HINT "\nSPACE toggles panic log QR. Full dump is also on serial.\n"
#define CLKS_PANIC_QR_HINT_ZH "\nSPACE 切换 panic 日志 QR。完整转储也会输出到串口。\n"
#define CLKS_PANIC_REASON_MAX 192U
#define CLKS_PANIC_NAME_MAX 64U
#define CLKS_PANIC_RECENT_LOG_LINES 10ULL
#define CLKS_PANIC_UI_MIN_WIDTH 640U
#define CLKS_PANIC_UI_MIN_HEIGHT 560U

#define CLKS_PANIC_PS2_DATA_PORT 0x60U
#define CLKS_PANIC_PS2_STATUS_PORT 0x64U
#define CLKS_PANIC_PS2_STATUS_OBF 0x01U
#define CLKS_PANIC_SC_SPACE_MAKE 0x39U
#define CLKS_PANIC_SC_SPACE_BREAK 0xB9U
#define CLKS_PANIC_SC_EXT_PREFIX 0xE0U
#define CLKS_PANIC_SC_RELEASE_MASK 0x80U
#define CLKS_PANIC_SC_TAB_MAKE 0x0FU
#define CLKS_PANIC_SC_EXT_HOME 0x47U
#define CLKS_PANIC_SC_EXT_UP 0x48U
#define CLKS_PANIC_SC_EXT_PAGE_UP 0x49U
#define CLKS_PANIC_SC_EXT_LEFT 0x4BU
#define CLKS_PANIC_SC_EXT_RIGHT 0x4DU
#define CLKS_PANIC_SC_EXT_END 0x4FU
#define CLKS_PANIC_SC_EXT_DOWN 0x50U
#define CLKS_PANIC_SC_EXT_PAGE_DOWN 0x51U

#define CLKS_PANIC_TEXT(en, zh) (clks_panic_locale_is_zh() == CLKS_TRUE ? (zh) : (en))

struct clks_panic_console {
    u32 cols;
    u32 rows;
    u32 row;
    u32 col;
    u32 cell_w;
    u32 cell_h;
};

static clks_bool clks_panic_active = CLKS_FALSE;
static clks_bool clks_panic_symbols_checked = CLKS_FALSE;
static const char *clks_panic_symbols_data = CLKS_NULL;
static u64 clks_panic_symbols_size = 0ULL;

enum clks_panic_screen_kind {
    CLKS_PANIC_SCREEN_NONE = 0,
    CLKS_PANIC_SCREEN_REASON = 1,
    CLKS_PANIC_SCREEN_EXCEPTION = 2
};

struct clks_panic_screen_snapshot {
    enum clks_panic_screen_kind kind;
    char reason[CLKS_PANIC_REASON_MAX];
    char name[CLKS_PANIC_NAME_MAX];
    u64 vector;
    u64 error_code;
    u64 rip;
    u64 rbp;
    u64 rsp;
    u64 cr2;
    clks_bool has_reason;
    clks_bool has_name;
};

enum clks_panic_key_action {
    CLKS_PANIC_KEY_NONE = 0,
    CLKS_PANIC_KEY_SPACE = 1,
    CLKS_PANIC_KEY_FOCUS_PREV = 2,
    CLKS_PANIC_KEY_FOCUS_NEXT = 3,
    CLKS_PANIC_KEY_SCROLL_UP = 4,
    CLKS_PANIC_KEY_SCROLL_DOWN = 5,
    CLKS_PANIC_KEY_PAGE_UP = 6,
    CLKS_PANIC_KEY_PAGE_DOWN = 7,
    CLKS_PANIC_KEY_HOME = 8,
    CLKS_PANIC_KEY_END = 9
};

enum clks_panic_ui_pane { CLKS_PANIC_UI_PANE_LOGS = 0, CLKS_PANIC_UI_PANE_BACKTRACE = 1 };

struct clks_panic_input_state {
    clks_bool space_down;
    clks_bool extended_prefix;
};

static struct clks_panic_screen_snapshot clks_panic_screen = {
    CLKS_PANIC_SCREEN_NONE, {0}, {0}, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, CLKS_FALSE, CLKS_FALSE};
static enum clks_panic_ui_pane clks_panic_ui_active_pane = CLKS_PANIC_UI_PANE_BACKTRACE;
static u32 clks_panic_ui_log_scroll = 0U;
static u32 clks_panic_ui_bt_scroll = 0U;
static u32 clks_panic_ui_log_total = 0U;
static u32 clks_panic_ui_bt_total = 0U;
static u32 clks_panic_ui_log_visible = 0U;
static u32 clks_panic_ui_bt_visible = 0U;
static clks_bool clks_panic_ui_log_follow_tail = CLKS_TRUE;

static clks_bool clks_panic_locale_is_zh(void);
static u32 clks_panic_codepoint_width(u32 codepoint);
static clks_bool clks_panic_utf8_next(const char *text, usize text_len, usize *io_index, u32 *out_codepoint);
static void clks_panic_console_put_codepoint(struct clks_panic_console *console, u32 codepoint);

static inline void clks_panic_disable_interrupts(void) {
#if defined(CLKS_ARCH_X86_64)
    __asm__ volatile("cli");
#elif defined(CLKS_ARCH_AARCH64)
    __asm__ volatile("msr daifset, #0xf");
#endif
}

#if defined(CLKS_ARCH_X86_64)
static inline u8 clks_panic_inb(u16 port) {
    u8 value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static clks_bool clks_panic_ps2_has_output(void) {
    return ((clks_panic_inb(CLKS_PANIC_PS2_STATUS_PORT) & CLKS_PANIC_PS2_STATUS_OBF) != 0U) ? CLKS_TRUE : CLKS_FALSE;
}
#endif

static enum clks_panic_key_action clks_panic_action_from_scancode(u8 scancode, clks_bool extended) {
#if defined(CLKS_ARCH_X86_64)
    if (extended == CLKS_TRUE) {
        switch (scancode) {
        case CLKS_PANIC_SC_EXT_UP:
            return CLKS_PANIC_KEY_SCROLL_UP;
        case CLKS_PANIC_SC_EXT_DOWN:
            return CLKS_PANIC_KEY_SCROLL_DOWN;
        case CLKS_PANIC_SC_EXT_PAGE_UP:
            return CLKS_PANIC_KEY_PAGE_UP;
        case CLKS_PANIC_SC_EXT_PAGE_DOWN:
            return CLKS_PANIC_KEY_PAGE_DOWN;
        case CLKS_PANIC_SC_EXT_HOME:
            return CLKS_PANIC_KEY_HOME;
        case CLKS_PANIC_SC_EXT_END:
            return CLKS_PANIC_KEY_END;
        case CLKS_PANIC_SC_EXT_LEFT:
            return CLKS_PANIC_KEY_FOCUS_PREV;
        case CLKS_PANIC_SC_EXT_RIGHT:
            return CLKS_PANIC_KEY_FOCUS_NEXT;
        default:
            break;
        }
    } else if (scancode == CLKS_PANIC_SC_TAB_MAKE) {
        return CLKS_PANIC_KEY_FOCUS_NEXT;
    }
#else
    (void)scancode;
    (void)extended;
#endif

    return CLKS_PANIC_KEY_NONE;
}

static enum clks_panic_key_action clks_panic_poll_key_action(struct clks_panic_input_state *state) {
#if defined(CLKS_ARCH_X86_64)
    enum clks_panic_key_action action = CLKS_PANIC_KEY_NONE;

    if (state == CLKS_NULL) {
        return CLKS_PANIC_KEY_NONE;
    }

    while (clks_panic_ps2_has_output() == CLKS_TRUE) {
        u8 scancode = clks_panic_inb(CLKS_PANIC_PS2_DATA_PORT);
        clks_bool extended = state->extended_prefix;

        if (scancode == CLKS_PANIC_SC_EXT_PREFIX) {
            state->extended_prefix = CLKS_TRUE;
            continue;
        }

        state->extended_prefix = CLKS_FALSE;

        if (scancode == CLKS_PANIC_SC_SPACE_BREAK) {
            state->space_down = CLKS_FALSE;
            continue;
        }

        if ((scancode & CLKS_PANIC_SC_RELEASE_MASK) != 0U) {
            continue;
        }

        if (scancode == CLKS_PANIC_SC_SPACE_MAKE) {
            if (state->space_down == CLKS_FALSE) {
                state->space_down = CLKS_TRUE;
                action = CLKS_PANIC_KEY_SPACE;
                break;
            }
            continue;
        }

        action = clks_panic_action_from_scancode(scancode, extended);
        if (action != CLKS_PANIC_KEY_NONE) {
            break;
        }
    }

    return action;
#else
    (void)state;
    return CLKS_PANIC_KEY_NONE;
#endif
}

static void clks_panic_u64_to_hex(u64 value, char out[19]) {
    int nibble;

    out[0] = '0';
    out[1] = 'X';

    for (nibble = 0; nibble < 16; nibble++) {
        u8 current = (u8)((value >> ((15 - nibble) * 4)) & 0x0FULL);
        out[2 + nibble] = (current < 10U) ? (char)('0' + current) : (char)('A' + (current - 10U));
    }

    out[18] = '\0';
}

static u64 clks_panic_read_cr2(void) {
#if defined(CLKS_ARCH_X86_64)
    u64 value;

    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
#else
    return 0ULL;
#endif
}

static void clks_panic_u32_to_dec(u32 value, char *out, usize out_size) {
    char tmp[11];
    usize len = 0U;
    usize i;

    if (out == CLKS_NULL || out_size == 0U) {
        return;
    }

    if (value == 0U) {
        if (out_size >= 2U) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';
        }
        return;
    }

    while (value != 0U && len < sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    if (len + 1U > out_size) {
        len = out_size - 1U;
    }

    for (i = 0U; i < len; i++) {
        out[i] = tmp[len - 1U - i];
    }

    out[len] = '\0';
}

static clks_bool clks_panic_console_init(struct clks_panic_console *console) {
    struct clks_framebuffer_info info;

    if (console == CLKS_NULL || clks_fb_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    info = clks_fb_info();

    console->cell_w = clks_fb_cell_width();
    console->cell_h = clks_fb_cell_height();

    if (console->cell_w == 0U) {
        console->cell_w = 8U;
    }

    if (console->cell_h == 0U) {
        console->cell_h = 8U;
    }

    console->cols = info.width / console->cell_w;
    console->rows = info.height / console->cell_h;
    console->row = 0U;
    console->col = 0U;

    if (console->cols == 0U || console->rows == 0U) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static void clks_panic_console_newline(struct clks_panic_console *console) {
    if (console == CLKS_NULL) {
        return;
    }

    console->col = 0U;

    if (console->row + 1U < console->rows) {
        console->row++;
    }
}

static void clks_panic_console_put_codepoint(struct clks_panic_console *console, u32 codepoint) {
    u32 x;
    u32 y;
    u32 width_cells;

    if (console == CLKS_NULL) {
        return;
    }

    if (codepoint == (u32)'\n') {
        clks_panic_console_newline(console);
        return;
    }

    if (codepoint == (u32)'\r') {
        console->col = 0U;
        return;
    }

    width_cells = clks_panic_codepoint_width(codepoint);
    if (width_cells == 0U) {
        width_cells = 1U;
    }

    if (console->row >= console->rows) {
        return;
    }

    if (console->col + width_cells > console->cols) {
        clks_panic_console_newline(console);
    }

    if (console->row >= console->rows || console->col >= console->cols) {
        return;
    }

    x = console->col * console->cell_w;
    y = console->row * console->cell_h;
    clks_fb_draw_codepoint_scaled(x, y, codepoint, CLKS_PANIC_FG, CLKS_PANIC_BG, 0U, 1U);

    console->col += width_cells;

    if (console->col >= console->cols) {
        clks_panic_console_newline(console);
    }
}

static void clks_panic_console_write_n(struct clks_panic_console *console, const char *text, usize len) {
    usize i = 0U;

    if (console == CLKS_NULL || text == CLKS_NULL) {
        return;
    }

    while (i < len && text[i] != '\0') {
        u32 codepoint;

        if (clks_panic_utf8_next(text, len, &i, &codepoint) == CLKS_FALSE) {
            break;
        }
        clks_panic_console_put_codepoint(console, codepoint);
    }
}

static void clks_panic_console_write(struct clks_panic_console *console, const char *text) {
    if (console == CLKS_NULL || text == CLKS_NULL) {
        return;
    }

    clks_panic_console_write_n(console, text, clks_strlen(text));
}

static void clks_panic_serial_write_n(const char *text, usize len) {
    usize i = 0U;

    if (text == CLKS_NULL) {
        return;
    }

    while (i < len) {
        clks_serial_write_char(text[i]);
        i++;
    }
}

static void clks_panic_serial_write_line(const char *line) {
    if (line == CLKS_NULL) {
        return;
    }

    clks_serial_write(line);
    clks_serial_write("\n");
}

static clks_bool clks_panic_is_hex(char ch) {
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

static u8 clks_panic_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return (u8)(ch - '0');
    }

    if (ch >= 'a' && ch <= 'f') {
        return (u8)(10 + (ch - 'a'));
    }

    return (u8)(10 + (ch - 'A'));
}

static clks_bool clks_panic_parse_symbol_line(const char *line, usize len, u64 *out_addr, const char **out_name,
                                              usize *out_name_len, const char **out_source, usize *out_source_len) {
    usize i = 0U;
    u64 addr = 0ULL;
    u32 digits = 0U;
    usize name_start;
    usize name_end;
    usize source_start;
    usize source_end;

    if (line == CLKS_NULL || out_addr == CLKS_NULL || out_name == CLKS_NULL || out_name_len == CLKS_NULL ||
        out_source == CLKS_NULL || out_source_len == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (len == 0U) {
        return CLKS_FALSE;
    }

    if (len >= 2U && line[0] == '0' && (line[1] == 'X' || line[1] == 'x')) {
        i = 2U;
    }

    while (i < len && clks_panic_is_hex(line[i]) == CLKS_TRUE) {
        addr = (addr << 4) | (u64)clks_panic_hex_value(line[i]);
        digits++;
        i++;
    }

    if (digits == 0U) {
        return CLKS_FALSE;
    }

    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }

    if (i >= len) {
        return CLKS_FALSE;
    }

    name_start = i;

    while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
        i++;
    }

    name_end = i;

    if (name_end <= name_start) {
        return CLKS_FALSE;
    }

    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }

    source_start = i;
    source_end = len;

    while (source_end > source_start &&
           (line[source_end - 1U] == ' ' || line[source_end - 1U] == '\t' || line[source_end - 1U] == '\r')) {
        source_end--;
    }

    *out_addr = addr;
    *out_name = &line[name_start];
    *out_name_len = name_end - name_start;
    *out_source = (source_end > source_start) ? &line[source_start] : CLKS_NULL;
    *out_source_len = (source_end > source_start) ? (source_end - source_start) : 0U;
    return CLKS_TRUE;
}

static clks_bool clks_panic_symbols_ready(void) {
    u64 size = 0ULL;
    const void *data;

    if (clks_panic_symbols_checked == CLKS_TRUE) {
        return (clks_panic_symbols_data != CLKS_NULL && clks_panic_symbols_size > 0ULL) ? CLKS_TRUE : CLKS_FALSE;
    }

    clks_panic_symbols_checked = CLKS_TRUE;

    if (clks_fs_is_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    data = clks_fs_read_all(CLKS_PANIC_SYMBOL_FILE, &size);

    if (data == CLKS_NULL || size == 0ULL) {
        return CLKS_FALSE;
    }

    clks_panic_symbols_data = (const char *)data;
    clks_panic_symbols_size = size;
    return CLKS_TRUE;
}

static clks_bool clks_panic_lookup_symbol(u64 addr, const char **out_name, usize *out_name_len, u64 *out_base,
                                          const char **out_source, usize *out_source_len) {
    const char *data;
    const char *end;
    const char *line;
    const char *best_name = CLKS_NULL;
    const char *best_source = CLKS_NULL;
    usize best_len = 0U;
    usize best_source_len = 0U;
    u64 best_addr = 0ULL;
    clks_bool found = CLKS_FALSE;

    if (out_name == CLKS_NULL || out_name_len == CLKS_NULL || out_base == CLKS_NULL || out_source == CLKS_NULL ||
        out_source_len == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_name = CLKS_NULL;
    *out_name_len = 0U;
    *out_base = 0ULL;
    *out_source = CLKS_NULL;
    *out_source_len = 0U;

    if (clks_panic_symbols_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    data = clks_panic_symbols_data;
    end = clks_panic_symbols_data + clks_panic_symbols_size;

    while (data < end) {
        u64 line_addr;
        const char *line_name;
        usize line_name_len;
        const char *line_source;
        usize line_source_len;
        usize line_len = 0U;

        line = data;

        while (data < end && *data != '\n') {
            data++;
            line_len++;
        }

        if (data < end && *data == '\n') {
            data++;
        }

        if (clks_panic_parse_symbol_line(line, line_len, &line_addr, &line_name, &line_name_len, &line_source,
                                         &line_source_len) == CLKS_FALSE) {
            continue;
        }

        if (line_addr <= addr && (found == CLKS_FALSE || line_addr >= best_addr)) {
            best_addr = line_addr;
            best_name = line_name;
            best_len = line_name_len;
            best_source = line_source;
            best_source_len = line_source_len;
            found = CLKS_TRUE;
        }
    }

    if (found == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_name = best_name;
    *out_name_len = best_len;
    *out_base = best_addr;
    *out_source = best_source;
    *out_source_len = best_source_len;
    return CLKS_TRUE;
}

static void clks_panic_emit_bt_entry(struct clks_panic_console *console, u32 index, u64 rip, clks_bool serial_enabled) {
    char index_dec[12];
    char rip_hex[19];
    const char *sym_name = CLKS_NULL;
    const char *sym_source = CLKS_NULL;
    usize sym_name_len = 0U;
    usize sym_source_len = 0U;
    u64 sym_base = 0ULL;
    clks_bool has_symbol;

    clks_panic_u32_to_dec(index, index_dec, sizeof(index_dec));
    clks_panic_u64_to_hex(rip, rip_hex);
    has_symbol = clks_panic_lookup_symbol(rip, &sym_name, &sym_name_len, &sym_base, &sym_source, &sym_source_len);

    if (serial_enabled == CLKS_TRUE) {
        clks_serial_write("[PANIC][BT] #");
        clks_serial_write(index_dec);
        clks_serial_write(" ");
        clks_serial_write(rip_hex);

        if (has_symbol == CLKS_TRUE) {
            char off_hex[19];
            u64 off = rip - sym_base;

            clks_panic_u64_to_hex(off, off_hex);
            clks_serial_write(" ");
            clks_panic_serial_write_n(sym_name, sym_name_len);
            clks_serial_write("+");
            clks_serial_write(off_hex);

            if (sym_source != CLKS_NULL && sym_source_len > 0U) {
                clks_serial_write(" @ ");
                clks_panic_serial_write_n(sym_source, sym_source_len);
            }
        }

        clks_serial_write("\n");
    }

    if (console == CLKS_NULL) {
        return;
    }

    clks_panic_console_write(console, "BT#");
    clks_panic_console_write(console, index_dec);
    clks_panic_console_write(console, " ");
    clks_panic_console_write(console, rip_hex);

    if (has_symbol == CLKS_TRUE) {
        char off_hex[19];
        u64 off = rip - sym_base;

        clks_panic_u64_to_hex(off, off_hex);
        clks_panic_console_write(console, " ");
        clks_panic_console_write_n(console, sym_name, sym_name_len);
        clks_panic_console_write(console, "+");
        clks_panic_console_write(console, off_hex);

        if (sym_source != CLKS_NULL && sym_source_len > 0U) {
            clks_panic_console_write(console, " @ ");
            clks_panic_console_write_n(console, sym_source, sym_source_len);
        }
    }

    clks_panic_console_write(console, "\n");
}

static clks_bool clks_panic_stack_ptr_valid(u64 ptr, u64 stack_low, u64 stack_high) {
    if ((ptr & 0x7ULL) != 0ULL) {
        return CLKS_FALSE;
    }

    if (ptr < stack_low || ptr + 16ULL > stack_high) {
        return CLKS_FALSE;
    }

    if (ptr < CLKS_PANIC_KERNEL_ADDR_BASE) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static void clks_panic_emit_backtrace(struct clks_panic_console *console, u64 rip, u64 rbp, u64 rsp,
                                      clks_bool serial_enabled) {
    u64 current_rbp;
    u64 stack_low;
    u64 stack_high;
    u32 frame = 0U;

    if (rip == 0ULL) {
        return;
    }

    if (serial_enabled == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][BT] BEGIN");
    }

    if (console != CLKS_NULL) {
        clks_panic_console_write(console, "\nBACKTRACE:\n");
    }

    clks_panic_emit_bt_entry(console, frame, rip, serial_enabled);
    frame++;

    if (rbp == 0ULL || rsp == 0ULL || frame >= CLKS_PANIC_BACKTRACE_MAX) {
        if (serial_enabled == CLKS_TRUE) {
            clks_panic_serial_write_line("[PANIC][BT] END");
        }
        return;
    }

    stack_low = rsp;
    stack_high = rsp + CLKS_PANIC_STACK_WINDOW_BYTES;

    if (stack_high <= stack_low) {
        if (serial_enabled == CLKS_TRUE) {
            clks_panic_serial_write_line("[PANIC][BT] END");
        }
        return;
    }

    current_rbp = rbp;

    while (frame < CLKS_PANIC_BACKTRACE_MAX) {
        const u64 *frame_ptr;
        u64 next_rbp;
        u64 ret_rip;

        if (clks_panic_stack_ptr_valid(current_rbp, stack_low, stack_high) == CLKS_FALSE) {
            break;
        }

        frame_ptr = (const u64 *)(usize)current_rbp;
        next_rbp = frame_ptr[0];
        ret_rip = frame_ptr[1];

        if (ret_rip == 0ULL) {
            break;
        }

        clks_panic_emit_bt_entry(console, frame, ret_rip, serial_enabled);
        frame++;

        if (next_rbp <= current_rbp) {
            break;
        }

        current_rbp = next_rbp;
    }

    if (serial_enabled == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][BT] END");
    }
}

static void clks_panic_capture_context(u64 *out_rip, u64 *out_rbp, u64 *out_rsp) {
    if (out_rip != CLKS_NULL) {
        *out_rip = 0ULL;
    }

    if (out_rbp != CLKS_NULL) {
        *out_rbp = 0ULL;
    }

    if (out_rsp != CLKS_NULL) {
        *out_rsp = 0ULL;
    }

#if defined(CLKS_ARCH_X86_64)
    if (out_rbp != CLKS_NULL) {
        __asm__ volatile("mov %%rbp, %0" : "=r"(*out_rbp));
    }

    if (out_rsp != CLKS_NULL) {
        __asm__ volatile("mov %%rsp, %0" : "=r"(*out_rsp));
    }

    if (out_rip != CLKS_NULL) {
        *out_rip = (u64)(usize)__builtin_return_address(0);
    }
#endif
}

static void clks_panic_copy_text(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || dst_size == 0U) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void clks_panic_write_hex_field(struct clks_panic_console *console, const char *label, u64 value) {
    char hex_buf[19];

    if (console == CLKS_NULL || label == CLKS_NULL) {
        return;
    }

    clks_panic_u64_to_hex(value, hex_buf);
    clks_panic_console_write(console, label);
    clks_panic_console_write(console, hex_buf);
    clks_panic_console_write(console, "\n");
}

static void clks_panic_serial_hex_field(const char *label, u64 value) {
    char hex_buf[19];

    if (label == CLKS_NULL) {
        return;
    }

    clks_panic_u64_to_hex(value, hex_buf);
    clks_serial_write(label);
    clks_serial_write(hex_buf);
    clks_serial_write("\n");
}

static u32 clks_panic_ui_min_u32(u32 a, u32 b) {
    return (a < b) ? a : b;
}

static u32 clks_panic_ui_sub_floor_u32(u32 value, u32 sub) {
    return (value > sub) ? (value - sub) : 0U;
}

static u32 clks_panic_ui_scroll_max(u32 total, u32 visible) {
    return (total > visible) ? (total - visible) : 0U;
}

static u32 clks_panic_ui_clamp_scroll(u32 value, u32 total, u32 visible) {
    u32 max_scroll = clks_panic_ui_scroll_max(total, visible);

    return (value > max_scroll) ? max_scroll : value;
}

static void clks_panic_ui_reset_scroll(void) {
    clks_panic_ui_active_pane = CLKS_PANIC_UI_PANE_BACKTRACE;
    clks_panic_ui_log_scroll = 0U;
    clks_panic_ui_bt_scroll = 0U;
    clks_panic_ui_log_total = 0U;
    clks_panic_ui_bt_total = 0U;
    clks_panic_ui_log_visible = 0U;
    clks_panic_ui_bt_visible = 0U;
    clks_panic_ui_log_follow_tail = CLKS_TRUE;
}

static void clks_panic_ui_apply_scroll_action(enum clks_panic_key_action action) {
    u32 *target_scroll;
    u32 total;
    u32 visible;
    u32 page;

    if (action == CLKS_PANIC_KEY_FOCUS_PREV || action == CLKS_PANIC_KEY_FOCUS_NEXT) {
        clks_panic_ui_active_pane = (clks_panic_ui_active_pane == CLKS_PANIC_UI_PANE_LOGS)
                                        ? CLKS_PANIC_UI_PANE_BACKTRACE
                                        : CLKS_PANIC_UI_PANE_LOGS;
        return;
    }

    if (clks_panic_ui_active_pane == CLKS_PANIC_UI_PANE_LOGS) {
        target_scroll = &clks_panic_ui_log_scroll;
        total = clks_panic_ui_log_total;
        visible = clks_panic_ui_log_visible;
        clks_panic_ui_log_follow_tail = CLKS_FALSE;
    } else {
        target_scroll = &clks_panic_ui_bt_scroll;
        total = clks_panic_ui_bt_total;
        visible = clks_panic_ui_bt_visible;
    }

    page = (visible > 1U) ? (visible - 1U) : 1U;

    switch (action) {
    case CLKS_PANIC_KEY_SCROLL_UP:
        if (*target_scroll > 0U) {
            (*target_scroll)--;
        }
        break;
    case CLKS_PANIC_KEY_SCROLL_DOWN:
        if (*target_scroll < clks_panic_ui_scroll_max(total, visible)) {
            (*target_scroll)++;
        }
        break;
    case CLKS_PANIC_KEY_PAGE_UP:
        *target_scroll = (*target_scroll > page) ? (*target_scroll - page) : 0U;
        break;
    case CLKS_PANIC_KEY_PAGE_DOWN:
        if (*target_scroll < clks_panic_ui_scroll_max(total, visible)) {
            *target_scroll += page;
        }
        break;
    case CLKS_PANIC_KEY_HOME:
        *target_scroll = 0U;
        break;
    case CLKS_PANIC_KEY_END:
        *target_scroll = clks_panic_ui_scroll_max(total, visible);
        break;
    default:
        break;
    }

    *target_scroll = clks_panic_ui_clamp_scroll(*target_scroll, total, visible);
}

static clks_bool clks_panic_locale_is_zh(void) {
    const char *locale = clks_locale_current();

    if (locale == CLKS_NULL) {
        return CLKS_FALSE;
    }

    return (locale[0] == 'z' && locale[1] == 'h') ? CLKS_TRUE : CLKS_FALSE;
}

static u32 clks_panic_codepoint_width(u32 codepoint) {
    if (codepoint == 0U) {
        return 0U;
    }

    if (codepoint < 0x1100U) {
        return 1U;
    }

    if ((codepoint >= 0x1100U && codepoint <= 0x115FU) || codepoint == 0x2329U || codepoint == 0x232AU ||
        (codepoint >= 0x2E80U && codepoint <= 0xA4CFU) || (codepoint >= 0xAC00U && codepoint <= 0xD7A3U) ||
        (codepoint >= 0xF900U && codepoint <= 0xFAFFU) || (codepoint >= 0xFE10U && codepoint <= 0xFE19U) ||
        (codepoint >= 0xFE30U && codepoint <= 0xFE6FU) || (codepoint >= 0xFF00U && codepoint <= 0xFF60U) ||
        (codepoint >= 0xFFE0U && codepoint <= 0xFFE6U) || (codepoint >= 0x20000U && codepoint <= 0x3FFFDUL)) {
        return 2U;
    }

    return 1U;
}

static clks_bool clks_panic_utf8_next(const char *text, usize text_len, usize *io_index, u32 *out_codepoint) {
    u8 b0;
    u32 value;
    u32 need;
    usize index;
    u32 i;

    if (text == CLKS_NULL || io_index == CLKS_NULL || out_codepoint == CLKS_NULL || *io_index >= text_len) {
        return CLKS_FALSE;
    }

    index = *io_index;
    b0 = (u8)text[index++];

    if (b0 < 0x80U) {
        *io_index = index;
        *out_codepoint = (u32)b0;
        return CLKS_TRUE;
    }

    if ((b0 & 0xE0U) == 0xC0U) {
        value = (u32)(b0 & 0x1FU);
        need = 1U;
    } else if ((b0 & 0xF0U) == 0xE0U) {
        value = (u32)(b0 & 0x0FU);
        need = 2U;
    } else if ((b0 & 0xF8U) == 0xF0U) {
        value = (u32)(b0 & 0x07U);
        need = 3U;
    } else {
        *io_index = index;
        *out_codepoint = 0xFFFDU;
        return CLKS_TRUE;
    }

    if (index + (usize)need > text_len) {
        *io_index = text_len;
        *out_codepoint = 0xFFFDU;
        return CLKS_TRUE;
    }

    for (i = 0U; i < need; i++) {
        u8 bx = (u8)text[index++];
        if ((bx & 0xC0U) != 0x80U) {
            *io_index = index;
            *out_codepoint = 0xFFFDU;
            return CLKS_TRUE;
        }
        value = (value << 6U) | (u32)(bx & 0x3FU);
    }

    if ((need == 1U && value < 0x80U) || (need == 2U && value < 0x800U) || (need == 3U && value < 0x10000U) ||
        value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
        value = 0xFFFDU;
    }

    *io_index = index;
    *out_codepoint = value;
    return CLKS_TRUE;
}

static void clks_panic_ui_text_at(u32 x, u32 y, const char *text, u32 fg, u32 bg, u32 style) {
    u32 cell_w = clks_fb_cell_width();
    u32 draw_x = x;
    usize len;
    usize i = 0U;

    if (text == CLKS_NULL) {
        return;
    }

    if (cell_w == 0U) {
        cell_w = 8U;
    }

    len = clks_strlen(text);
    while (i < len && text[i] != '\0') {
        u32 codepoint;
        u32 cells;

        if (clks_panic_utf8_next(text, len, &i, &codepoint) == CLKS_FALSE) {
            break;
        }

        cells = clks_panic_codepoint_width(codepoint);
        clks_fb_draw_codepoint_scaled(x + (draw_x - x), y, codepoint, fg, bg, style, 1U);
        draw_x += cell_w * ((cells == 0U) ? 1U : cells);
    }
}

static void clks_panic_ui_text_clip(u32 x, u32 y, u32 max_width, const char *text, u32 fg, u32 bg, u32 style) {
    u32 cell_w = clks_fb_cell_width();
    u32 used_width = 0U;
    usize len;
    usize i = 0U;

    if (text == CLKS_NULL || max_width == 0U) {
        return;
    }

    if (cell_w == 0U) {
        cell_w = 8U;
    }

    len = clks_strlen(text);
    while (i < len && text[i] != '\0') {
        u32 codepoint;
        u32 cells;
        u32 advance;

        if (clks_panic_utf8_next(text, len, &i, &codepoint) == CLKS_FALSE) {
            break;
        }

        cells = clks_panic_codepoint_width(codepoint);
        advance = cell_w * ((cells == 0U) ? 1U : cells);
        if (used_width + advance > max_width) {
            break;
        }

        clks_fb_draw_codepoint_scaled(x + used_width, y, codepoint, fg, bg, style, 1U);
        used_width += advance;
    }
}

static void clks_panic_ui_text_n_clip(u32 x, u32 y, u32 max_width, const char *text, usize len, u32 fg, u32 bg,
                                      u32 style) {
    u32 cell_w = clks_fb_cell_width();
    u32 used_width = 0U;
    usize i = 0U;

    if (text == CLKS_NULL || max_width == 0U) {
        return;
    }

    if (cell_w == 0U) {
        cell_w = 8U;
    }

    while (i < len && text[i] != '\0') {
        u32 codepoint;
        u32 cells;
        u32 advance;

        if (clks_panic_utf8_next(text, len, &i, &codepoint) == CLKS_FALSE) {
            break;
        }

        cells = clks_panic_codepoint_width(codepoint);
        advance = cell_w * ((cells == 0U) ? 1U : cells);
        if (used_width + advance > max_width) {
            break;
        }

        clks_fb_draw_codepoint_scaled(x + used_width, y, codepoint, fg, bg, style, 1U);
        used_width += advance;
    }
}

static void clks_panic_ui_rect_outline(u32 x, u32 y, u32 width, u32 height, u32 color) {
    if (width < 2U || height < 2U) {
        return;
    }

    clks_fb_fill_rect(x, y, width, 1U, color);
    clks_fb_fill_rect(x, y + height - 1U, width, 1U, color);
    clks_fb_fill_rect(x, y, 1U, height, color);
    clks_fb_fill_rect(x + width - 1U, y, 1U, height, color);
}

static void clks_panic_ui_panel(u32 x, u32 y, u32 width, u32 height, const char *title) {
    u32 title_x;
    u32 title_bg_w;

    if (width == 0U || height == 0U) {
        return;
    }

    if (width > 4U && height > 5U) {
        clks_fb_fill_rect(x + 4U, y + 5U, width - 4U, height - 5U, 0x0006070CU);
    }
    clks_fb_fill_rect(x, y, width, height, CLKS_PANIC_UI_PANEL);
    clks_fb_fill_rect(x, y, width, 3U, CLKS_PANIC_UI_ACCENT_DARK);
    clks_panic_ui_rect_outline(x, y, width, height, CLKS_PANIC_UI_PANEL_EDGE);

    if (title != CLKS_NULL && width > 24U) {
        title_x = x + 14U;
        title_bg_w = clks_panic_ui_min_u32(150U, width - 12U);
        clks_fb_fill_rect(title_x - 6U, y + 3U, title_bg_w, 18U, CLKS_PANIC_UI_PANEL_ALT);
        clks_panic_ui_text_clip(title_x, y + 6U, width - 24U, title, CLKS_PANIC_UI_WARN, CLKS_PANIC_UI_PANEL_ALT,
                                CLKS_FB_STYLE_BOLD);
    }
}

static u32 clks_panic_ui_write_pair(u32 x, u32 y, u32 width, const char *label, const char *value, u32 value_color) {
    u32 label_w;

    if (width < 16U) {
        return y;
    }

    label_w = clks_panic_ui_min_u32(width / 3U, 96U);
    clks_panic_ui_text_clip(x, y, label_w, label, CLKS_PANIC_UI_MUTED, CLKS_PANIC_UI_PANEL, 0U);
    clks_panic_ui_text_clip(x + label_w, y, width - label_w, value, value_color, CLKS_PANIC_UI_PANEL, 0U);
    return y + clks_fb_cell_height() + 4U;
}

static u32 clks_panic_ui_write_hex_pair(u32 x, u32 y, u32 width, const char *label, u64 value, u32 value_color) {
    char hex_buf[19];

    clks_panic_u64_to_hex(value, hex_buf);
    return clks_panic_ui_write_pair(x, y, width, label, hex_buf, value_color);
}

static void clks_panic_ui_draw_scroll_status(u32 x, u32 y, u32 width, u32 scroll, u32 total, u32 visible,
                                             clks_bool active) {
    char first_dec[12];
    char total_dec[12];
    u32 first = (total == 0U) ? 0U : (scroll + 1U);
    u32 color = (active == CLKS_TRUE) ? CLKS_PANIC_UI_WARN : CLKS_PANIC_UI_DIM;

    clks_panic_u32_to_dec(first, first_dec, sizeof(first_dec));
    clks_panic_u32_to_dec(total, total_dec, sizeof(total_dec));

    clks_panic_ui_text_clip(x, y, width,
                            active == CLKS_TRUE ? CLKS_PANIC_TEXT("[ACTIVE] ", "[活动] ")
                                                : CLKS_PANIC_TEXT("[pane]   ", "[面板] "),
                            color, CLKS_PANIC_UI_PANEL, CLKS_FB_STYLE_BOLD);
    clks_panic_ui_text_clip(x + 72U, y, clks_panic_ui_sub_floor_u32(width, 72U), first_dec, color, CLKS_PANIC_UI_PANEL,
                            0U);
    clks_panic_ui_text_clip(x + 116U, y, clks_panic_ui_sub_floor_u32(width, 116U), "/", color, CLKS_PANIC_UI_PANEL, 0U);
    clks_panic_ui_text_clip(x + 128U, y, clks_panic_ui_sub_floor_u32(width, 128U), total_dec, color,
                            CLKS_PANIC_UI_PANEL, 0U);

    if (total > visible && width > 230U) {
        clks_panic_ui_text_clip(x + 188U, y, width - 188U,
                                CLKS_PANIC_TEXT("use arrows/PgUp/PgDn", "方向键/PgUp/PgDn 滚动"), CLKS_PANIC_UI_DIM,
                                CLKS_PANIC_UI_PANEL, 0U);
    }
}

static void clks_panic_ui_emit_backtrace(u32 x, u32 y, u32 width, u32 height, u64 rip, u64 rbp, u64 rsp, u32 scroll,
                                         clks_bool serial_enabled) {
    u64 current_rbp = 0ULL;
    u64 stack_low = 0ULL;
    u64 stack_high = 0ULL;
    u32 line_h = clks_fb_cell_height() + 3U;
    u32 row_y = y;
    u32 frame = 0U;
    u32 logical_line = 0U;
    u32 visible_lines;
    u32 total_lines = 0U;

    if (serial_enabled == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][BT] BEGIN");
    }

    if (rip == 0ULL || height < line_h) {
        clks_panic_ui_bt_total = 1U;
        clks_panic_ui_bt_visible = 1U;
        clks_panic_ui_bt_scroll = 0U;
        clks_panic_ui_text_clip(x, row_y, width,
                                CLKS_PANIC_TEXT("<no instruction pointer>", "<无指令指针 (no instruction pointer)>"),
                                CLKS_PANIC_UI_DIM, CLKS_PANIC_UI_PANEL, 0U);
        if (serial_enabled == CLKS_TRUE) {
            clks_panic_serial_write_line("[PANIC][BT] END");
        }
        return;
    }

    visible_lines = height / line_h;
    if (visible_lines == 0U) {
        visible_lines = 1U;
    }

    while (frame < CLKS_PANIC_BACKTRACE_MAX) {
        u64 entry_rip;
        char index_dec[12];
        char rip_hex[19];
        const char *sym_name = CLKS_NULL;
        const char *sym_source = CLKS_NULL;
        usize sym_name_len = 0U;
        usize sym_source_len = 0U;
        u64 sym_base = 0ULL;
        clks_bool has_symbol;

        if (frame == 0U) {
            entry_rip = rip;
        } else {
            const u64 *frame_ptr;
            u64 next_rbp;

            if (clks_panic_stack_ptr_valid(current_rbp, stack_low, stack_high) == CLKS_FALSE) {
                break;
            }

            frame_ptr = (const u64 *)(usize)current_rbp;
            next_rbp = frame_ptr[0];
            entry_rip = frame_ptr[1];

            if (entry_rip == 0ULL) {
                break;
            }

            if (next_rbp <= current_rbp) {
                current_rbp = 0ULL;
            } else {
                current_rbp = next_rbp;
            }
        }

        clks_panic_emit_bt_entry(CLKS_NULL, frame, entry_rip, serial_enabled);
        clks_panic_u32_to_dec(frame, index_dec, sizeof(index_dec));
        clks_panic_u64_to_hex(entry_rip, rip_hex);
        has_symbol =
            clks_panic_lookup_symbol(entry_rip, &sym_name, &sym_name_len, &sym_base, &sym_source, &sym_source_len);

        if (logical_line >= scroll && row_y + line_h <= y + height) {
            clks_panic_ui_text_at(x, row_y, "#", CLKS_PANIC_UI_ACCENT, CLKS_PANIC_UI_PANEL, CLKS_FB_STYLE_BOLD);
            clks_panic_ui_text_clip(x + 10U, row_y, 26U, index_dec, CLKS_PANIC_UI_ACCENT, CLKS_PANIC_UI_PANEL,
                                    CLKS_FB_STYLE_BOLD);
            clks_panic_ui_text_clip(x + 42U, row_y, 158U, rip_hex, CLKS_PANIC_UI_TEXT, CLKS_PANIC_UI_PANEL, 0U);

            if (has_symbol == CLKS_TRUE) {
                u32 sym_x = x + 208U;
                u32 sym_w = (width > 216U) ? (width - 216U) : 0U;

                clks_panic_ui_text_n_clip(sym_x, row_y, sym_w, sym_name, sym_name_len, CLKS_PANIC_UI_GOOD,
                                          CLKS_PANIC_UI_PANEL, 0U);
            } else {
                clks_panic_ui_text_clip(x + 208U, row_y, width > 216U ? width - 216U : 0U,
                                        CLKS_PANIC_TEXT("<no symbol>", "<无符号 (no symbol)>"), CLKS_PANIC_UI_DIM,
                                        CLKS_PANIC_UI_PANEL, 0U);
            }
            row_y += line_h;
        }
        logical_line++;
        total_lines++;

        if (has_symbol == CLKS_TRUE && sym_source != CLKS_NULL && sym_source_len > 0U) {
            u32 sym_x = x + 208U;
            u32 sym_w = (width > 216U) ? (width - 216U) : 0U;

            if (logical_line >= scroll && row_y + line_h <= y + height) {
                clks_panic_ui_text_n_clip(sym_x, row_y, sym_w, sym_source, sym_source_len, CLKS_PANIC_UI_DIM,
                                          CLKS_PANIC_UI_PANEL, 0U);
                row_y += line_h;
            }
            logical_line++;
            total_lines++;
        }

        frame++;

        if (frame == 1U) {
            if (rbp == 0ULL || rsp == 0ULL) {
                break;
            }

            stack_low = rsp;
            stack_high = rsp + CLKS_PANIC_STACK_WINDOW_BYTES;
            if (stack_high <= stack_low) {
                break;
            }
            current_rbp = rbp;
        } else if (current_rbp == 0ULL) {
            break;
        }
    }

    clks_panic_ui_bt_total = total_lines;
    clks_panic_ui_bt_visible = visible_lines;
    clks_panic_ui_bt_scroll = clks_panic_ui_clamp_scroll(scroll, total_lines, visible_lines);

    if (serial_enabled == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][BT] END");
    }
}

static void clks_panic_ui_emit_logs(u32 x, u32 y, u32 width, u32 height, u32 scroll, clks_bool serial_enabled) {
    u64 count = clks_log_journal_count();
    u64 total;
    u64 start;
    u64 visible;
    u64 i;
    u32 line_h = clks_fb_cell_height() + 3U;
    u32 row_y = y;

    if (serial_enabled == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][LOG] BEGIN");
    }

    if (line_h == 0U) {
        line_h = 11U;
    }

    visible = height / line_h;
    if (visible == 0ULL) {
        if (serial_enabled == CLKS_TRUE) {
            clks_panic_serial_write_line("[PANIC][LOG] END");
        }
        return;
    }

    total = (count == 0ULL) ? 1ULL : count;
    if ((u64)scroll > total) {
        scroll = (u32)total;
    }
    if (count == 0ULL) {
        scroll = 0U;
    }
    if ((u64)scroll + visible > total) {
        start = (total > visible) ? (total - visible) : 0ULL;
    } else {
        start = (u64)scroll;
    }

    clks_panic_ui_log_total = (u32)total;
    clks_panic_ui_log_visible = (u32)visible;
    clks_panic_ui_log_scroll = clks_panic_ui_clamp_scroll((u32)start, (u32)total, (u32)visible);

    for (i = start; i < count && row_y + line_h <= y + height && i < start + visible; i++) {
        char line[256];

        if (clks_log_journal_read(i, line, sizeof(line)) == CLKS_FALSE) {
            continue;
        }

        if (serial_enabled == CLKS_TRUE) {
            clks_serial_write("[PANIC][LOG] ");
            clks_serial_write(line);
            clks_serial_write("\n");
        }
        clks_panic_ui_text_clip(x, row_y, width, line, CLKS_PANIC_UI_TEXT, CLKS_PANIC_UI_PANEL, 0U);
        row_y += line_h;
    }

    if (count == 0ULL) {
        clks_panic_ui_text_clip(x, row_y, width, CLKS_PANIC_TEXT("<journal empty>", "<日志为空 (journal empty)>"),
                                CLKS_PANIC_UI_DIM, CLKS_PANIC_UI_PANEL, 0U);
    }

    if (serial_enabled == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][LOG] END");
    }
}

static void clks_panic_ui_emit_process(u32 x, u32 y, u32 width, u32 height, clks_bool serial_enabled) {
    u64 pid = clks_exec_current_pid();
    struct clks_exec_proc_snapshot snap;
    u32 row_y = y;

    if (serial_enabled == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][PROC] BEGIN");
        clks_panic_serial_hex_field("[PANIC][PROC] PID: ", pid);
    }

    row_y = clks_panic_ui_write_hex_pair(x, row_y, width, CLKS_PANIC_TEXT("PID", "PID"), pid, CLKS_PANIC_UI_TEXT);

    if (pid != 0ULL && clks_exec_proc_snapshot(pid, &snap) == CLKS_TRUE) {
        if (serial_enabled == CLKS_TRUE) {
            clks_panic_serial_hex_field("[PANIC][PROC] PPID: ", snap.ppid);
            clks_panic_serial_hex_field("[PANIC][PROC] STATE: ", snap.state);
            clks_panic_serial_hex_field("[PANIC][PROC] UID: ", snap.uid);
            clks_serial_write("[PANIC][PROC] PATH: ");
            clks_serial_write(snap.path);
            clks_serial_write("\n");
        }

        if (row_y < y + height) {
            row_y = clks_panic_ui_write_hex_pair(x, row_y, width, CLKS_PANIC_TEXT("PPID", "PPID"), snap.ppid,
                                                 CLKS_PANIC_UI_TEXT);
        }
        if (row_y < y + height) {
            row_y = clks_panic_ui_write_hex_pair(x, row_y, width, CLKS_PANIC_TEXT("STATE", "状态 (STATE)"), snap.state,
                                                 CLKS_PANIC_UI_TEXT);
        }
        if (row_y < y + height) {
            row_y = clks_panic_ui_write_hex_pair(x, row_y, width, CLKS_PANIC_TEXT("UID", "UID"), snap.uid,
                                                 CLKS_PANIC_UI_TEXT);
        }
        if (row_y < y + height) {
            (void)clks_panic_ui_write_pair(x, row_y, width, CLKS_PANIC_TEXT("PATH", "路径 (PATH)"), snap.path,
                                           CLKS_PANIC_UI_TEXT);
        }
    } else if (row_y < y + height) {
        if (serial_enabled == CLKS_TRUE) {
            clks_panic_serial_write_line("[PANIC][PROC] no user process context");
        }
        (void)clks_panic_ui_write_pair(x, row_y, width, CLKS_PANIC_TEXT("PATH", "路径 (PATH)"),
                                       CLKS_PANIC_TEXT("<kernel/no process>", "<内核/无进程 (kernel/no process)>"),
                                       CLKS_PANIC_UI_DIM);
    }

    if (serial_enabled == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][PROC] END");
    }
}

static void clks_panic_emit_current_process(struct clks_panic_console *console) {
    u64 pid;
    struct clks_exec_proc_snapshot snap;

    pid = clks_exec_current_pid();
    clks_panic_serial_write_line("[PANIC][PROC] BEGIN");
    clks_panic_serial_hex_field("[PANIC][PROC] PID: ", pid);

    if (console != CLKS_NULL) {
        clks_panic_console_write(console, CLKS_PANIC_TEXT("\nCURRENT PROCESS:\n", "\n当前进程 (CURRENT PROCESS):\n"));
        clks_panic_write_hex_field(console, "PID:    ", pid);
    }

    if (pid != 0ULL && clks_exec_proc_snapshot(pid, &snap) == CLKS_TRUE) {
        clks_panic_serial_hex_field("[PANIC][PROC] PPID: ", snap.ppid);
        clks_panic_serial_hex_field("[PANIC][PROC] STATE: ", snap.state);
        clks_panic_serial_hex_field("[PANIC][PROC] UID: ", snap.uid);
        clks_serial_write("[PANIC][PROC] PATH: ");
        clks_serial_write(snap.path);
        clks_serial_write("\n");

        if (console != CLKS_NULL) {
            clks_panic_write_hex_field(console, "PPID:   ", snap.ppid);
            clks_panic_write_hex_field(console, "STATE:  ", snap.state);
            clks_panic_write_hex_field(console, "UID:    ", snap.uid);
            clks_panic_console_write(console, "PATH:   ");
            clks_panic_console_write(console, snap.path);
            clks_panic_console_write(console, "\n");
        }
    } else {
        clks_panic_serial_write_line("[PANIC][PROC] no user process context");
        if (console != CLKS_NULL) {
            clks_panic_console_write(console, "PATH:   <kernel/no process>\n");
        }
    }

    clks_panic_serial_write_line("[PANIC][PROC] END");
}

static void clks_panic_emit_recent_logs(struct clks_panic_console *console) {
    u64 count = clks_log_journal_count();
    u64 start = 0ULL;
    u64 i;

    clks_panic_serial_write_line("[PANIC][LOG] BEGIN");

    if (count > CLKS_PANIC_RECENT_LOG_LINES) {
        start = count - CLKS_PANIC_RECENT_LOG_LINES;
    }

    if (console != CLKS_NULL) {
        clks_panic_console_write(console, CLKS_PANIC_TEXT("\nRECENT LOGS:\n", "\n最近日志 (RECENT LOGS):\n"));
    }

    for (i = start; i < count; i++) {
        char line[256];

        if (clks_log_journal_read(i, line, sizeof(line)) == CLKS_FALSE) {
            continue;
        }

        clks_serial_write("[PANIC][LOG] ");
        clks_serial_write(line);
        clks_serial_write("\n");

        if (console != CLKS_NULL) {
            clks_panic_console_write(console, line);
            clks_panic_console_write(console, "\n");
        }
    }

    if (count == 0ULL && console != CLKS_NULL) {
        clks_panic_console_write(console, CLKS_PANIC_TEXT("<empty>\n", "<空 (empty)>\n"));
    }

    clks_panic_serial_write_line("[PANIC][LOG] END");
}

static void clks_panic_snapshot_reason(const char *reason, u64 rip, u64 rbp, u64 rsp) {
    clks_panic_screen.kind = CLKS_PANIC_SCREEN_REASON;
    clks_panic_screen.vector = 0ULL;
    clks_panic_screen.error_code = 0ULL;
    clks_panic_screen.rip = rip;
    clks_panic_screen.rbp = rbp;
    clks_panic_screen.rsp = rsp;
    clks_panic_screen.cr2 = clks_panic_read_cr2();
    clks_panic_screen.has_name = CLKS_FALSE;
    clks_panic_screen.name[0] = '\0';
    clks_panic_ui_reset_scroll();

    if (reason != CLKS_NULL && reason[0] != '\0') {
        clks_panic_copy_text(clks_panic_screen.reason, sizeof(clks_panic_screen.reason), reason);
        clks_panic_screen.has_reason = CLKS_TRUE;
    } else {
        clks_panic_screen.reason[0] = '\0';
        clks_panic_screen.has_reason = CLKS_FALSE;
    }
}

static void clks_panic_snapshot_exception(const char *name, u64 vector, u64 error_code, u64 rip, u64 rbp, u64 rsp) {
    clks_panic_screen.kind = CLKS_PANIC_SCREEN_EXCEPTION;
    clks_panic_screen.vector = vector;
    clks_panic_screen.error_code = error_code;
    clks_panic_screen.rip = rip;
    clks_panic_screen.rbp = rbp;
    clks_panic_screen.rsp = rsp;
    clks_panic_screen.cr2 = clks_panic_read_cr2();
    clks_panic_screen.has_reason = CLKS_FALSE;
    clks_panic_screen.reason[0] = '\0';
    clks_panic_ui_reset_scroll();

    if (name != CLKS_NULL && name[0] != '\0') {
        clks_panic_copy_text(clks_panic_screen.name, sizeof(clks_panic_screen.name), name);
        clks_panic_screen.has_name = CLKS_TRUE;
    } else {
        clks_panic_screen.name[0] = '\0';
        clks_panic_screen.has_name = CLKS_FALSE;
    }
}

static void clks_panic_render_snapshot_console(clks_bool serial_backtrace) {
    struct clks_panic_console console;

    if (clks_panic_console_init(&console) == CLKS_TRUE) {
        struct clks_framebuffer_info info = clks_fb_info();
        u32 margin;
        u32 header_h;
        u32 footer_h;
        u32 gap;
        u32 content_y;
        u32 content_h;
        u32 left_w;
        u32 right_w;
        u32 left_x;
        u32 right_x;
        u32 panel_y;
        u32 top_panel_h;
        u32 proc_panel_h;
        u32 log_panel_h;
        u32 bt_panel_h;
        u32 row_y;
        const char *panic_kind = CLKS_PANIC_TEXT("KERNEL PANIC", "内核 Panic (KERNEL PANIC)");

        if (info.width < CLKS_PANIC_UI_MIN_WIDTH || info.height < CLKS_PANIC_UI_MIN_HEIGHT) {
            clks_fb_clear(CLKS_PANIC_BG);
            clks_panic_console_write(&console,
                                     CLKS_PANIC_TEXT("CLeonOS KERNEL PANIC\n", "CLeonOS 内核 Panic (KERNEL PANIC)\n"));
            clks_panic_console_write(&console, "====================\n\n");

            if (clks_panic_screen.kind == CLKS_PANIC_SCREEN_EXCEPTION) {
                clks_panic_console_write(
                    &console, CLKS_PANIC_TEXT("TYPE: CPU EXCEPTION\n", "类型 (TYPE): CPU 异常 (CPU EXCEPTION)\n"));

                if (clks_panic_screen.has_name == CLKS_TRUE) {
                    clks_panic_console_write(&console, CLKS_PANIC_TEXT("NAME: ", "名称 (NAME): "));
                    clks_panic_console_write(&console, clks_panic_screen.name);
                    clks_panic_console_write(&console, "\n");
                }

                clks_panic_write_hex_field(&console, "VECTOR: ", clks_panic_screen.vector);
                clks_panic_write_hex_field(&console, "ERROR:  ", clks_panic_screen.error_code);
            } else if (clks_panic_screen.has_reason == CLKS_TRUE) {
                clks_panic_console_write(&console, CLKS_PANIC_TEXT("REASON: ", "原因 (REASON): "));
                clks_panic_console_write(&console, clks_panic_screen.reason);
                clks_panic_console_write(&console, "\n");
            }

            clks_panic_write_hex_field(&console, "RIP:    ", clks_panic_screen.rip);
            clks_panic_write_hex_field(&console, "CR2:    ", clks_panic_screen.cr2);
            clks_panic_write_hex_field(&console, "RBP:    ", clks_panic_screen.rbp);
            clks_panic_write_hex_field(&console, "RSP:    ", clks_panic_screen.rsp);
            clks_panic_emit_current_process(&console);
            clks_panic_emit_backtrace(&console, clks_panic_screen.rip, clks_panic_screen.rbp, clks_panic_screen.rsp,
                                      serial_backtrace);
            clks_panic_emit_recent_logs(&console);
            clks_panic_console_write(&console, CLKS_PANIC_TEXT("\nSystem halted. Please reboot the computer.\n",
                                                               "\n系统已停止。请重启计算机。\n"));
            clks_panic_console_write(&console, CLKS_PANIC_TEXT(CLKS_PANIC_QR_HINT, CLKS_PANIC_QR_HINT_ZH));
            return;
        }

        clks_fb_clear(CLKS_PANIC_UI_BG);
        margin = 24U;
        header_h = 92U;
        footer_h = 42U;
        gap = 14U;

        if (info.width < 900U) {
            margin = 16U;
            header_h = 82U;
            gap = 10U;
        }

        content_y = margin + header_h + gap;
        content_h = info.height - content_y - footer_h - margin;
        left_x = margin;
        left_w = (info.width - (margin * 2U) - gap) / 2U;
        right_x = left_x + left_w + gap;
        right_w = info.width - right_x - margin;
        top_panel_h = 184U;
        proc_panel_h = 150U;
        log_panel_h = clks_panic_ui_sub_floor_u32(content_h, top_panel_h + proc_panel_h + (2U * gap));
        bt_panel_h = content_h;

        if (log_panel_h < 110U) {
            log_panel_h = 110U;
        }

        clks_fb_fill_rect(0U, 0U, info.width, info.height, CLKS_PANIC_UI_BG);
        clks_fb_fill_rect(0U, 0U, info.width, header_h + margin, CLKS_PANIC_UI_BG_2);
        clks_fb_fill_rect(0U, 0U, 10U, info.height, CLKS_PANIC_UI_ACCENT);
        clks_fb_fill_rect(10U, 0U, 4U, info.height, CLKS_PANIC_UI_ACCENT_DARK);

        if (clks_panic_screen.kind == CLKS_PANIC_SCREEN_EXCEPTION) {
            panic_kind = CLKS_PANIC_TEXT("CPU EXCEPTION", "CPU 异常 (CPU EXCEPTION)");
        }

        clks_panic_ui_text_at(margin + 8U, margin + 8U,
                              CLKS_PANIC_TEXT("CLeonKernelSystem", "CLeonKernelSystem / 内核系统"), CLKS_PANIC_UI_MUTED,
                              CLKS_PANIC_UI_BG_2, CLKS_FB_STYLE_BOLD);
        clks_panic_ui_text_at(margin + 8U, margin + 34U, panic_kind, CLKS_PANIC_UI_TEXT, CLKS_PANIC_UI_BG_2,
                              CLKS_FB_STYLE_BOLD);
        clks_fb_fill_rect(info.width - margin - 210U, margin + 20U, 210U, 34U, CLKS_PANIC_UI_ACCENT_DARK);
        clks_panic_ui_text_at(info.width - margin - 194U, margin + 30U,
                              CLKS_PANIC_TEXT("SYSTEM HALTED", "系统已停止 (HALTED)"), CLKS_PANIC_UI_TEXT,
                              CLKS_PANIC_UI_ACCENT_DARK, CLKS_FB_STYLE_BOLD);

        clks_panic_ui_panel(left_x, content_y, left_w, top_panel_h,
                            CLKS_PANIC_TEXT("FAULT SUMMARY", "故障摘要 (FAULT SUMMARY)"));
        row_y = content_y + 32U;
        if (clks_panic_screen.kind == CLKS_PANIC_SCREEN_EXCEPTION) {
            if (clks_panic_screen.has_name == CLKS_TRUE) {
                row_y = clks_panic_ui_write_pair(left_x + 16U, row_y, clks_panic_ui_sub_floor_u32(left_w, 32U),
                                                 CLKS_PANIC_TEXT("NAME", "名称 (NAME)"), clks_panic_screen.name,
                                                 CLKS_PANIC_UI_WARN);
            }
            row_y = clks_panic_ui_write_hex_pair(left_x + 16U, row_y, clks_panic_ui_sub_floor_u32(left_w, 32U),
                                                 CLKS_PANIC_TEXT("VECTOR", "向量 (VECTOR)"), clks_panic_screen.vector,
                                                 CLKS_PANIC_UI_TEXT);
            row_y = clks_panic_ui_write_hex_pair(left_x + 16U, row_y, clks_panic_ui_sub_floor_u32(left_w, 32U),
                                                 CLKS_PANIC_TEXT("ERROR", "错误码 (ERROR)"),
                                                 clks_panic_screen.error_code, CLKS_PANIC_UI_TEXT);
        } else if (clks_panic_screen.has_reason == CLKS_TRUE) {
            row_y = clks_panic_ui_write_pair(left_x + 16U, row_y, clks_panic_ui_sub_floor_u32(left_w, 32U),
                                             CLKS_PANIC_TEXT("REASON", "原因 (REASON)"), clks_panic_screen.reason,
                                             CLKS_PANIC_UI_WARN);
        } else {
            row_y = clks_panic_ui_write_pair(left_x + 16U, row_y, clks_panic_ui_sub_floor_u32(left_w, 32U),
                                             CLKS_PANIC_TEXT("REASON", "原因 (REASON)"),
                                             CLKS_PANIC_TEXT("<unknown>", "<未知 (unknown)>"), CLKS_PANIC_UI_WARN);
        }
        row_y = clks_panic_ui_write_hex_pair(left_x + 16U, row_y, clks_panic_ui_sub_floor_u32(left_w, 32U), "RIP",
                                             clks_panic_screen.rip, CLKS_PANIC_UI_TEXT);
        row_y = clks_panic_ui_write_hex_pair(left_x + 16U, row_y, clks_panic_ui_sub_floor_u32(left_w, 32U), "CR2",
                                             clks_panic_screen.cr2, CLKS_PANIC_UI_TEXT);
        row_y = clks_panic_ui_write_hex_pair(left_x + 16U, row_y, clks_panic_ui_sub_floor_u32(left_w, 32U), "RBP",
                                             clks_panic_screen.rbp, CLKS_PANIC_UI_TEXT);
        (void)clks_panic_ui_write_hex_pair(left_x + 16U, row_y, clks_panic_ui_sub_floor_u32(left_w, 32U), "RSP",
                                           clks_panic_screen.rsp, CLKS_PANIC_UI_TEXT);

        panel_y = content_y + top_panel_h + gap;
        clks_panic_ui_panel(left_x, panel_y, left_w, proc_panel_h,
                            CLKS_PANIC_TEXT("CURRENT PROCESS", "当前进程 (CURRENT PROCESS)"));
        clks_panic_ui_emit_process(left_x + 16U, panel_y + 32U, clks_panic_ui_sub_floor_u32(left_w, 32U),
                                   clks_panic_ui_sub_floor_u32(proc_panel_h, 44U), serial_backtrace);

        panel_y += proc_panel_h + gap;
        if (panel_y + log_panel_h > content_y + content_h) {
            log_panel_h = (content_y + content_h > panel_y) ? (content_y + content_h - panel_y) : 0U;
        }
        if (log_panel_h > 40U) {
            u32 log_body_h = clks_panic_ui_sub_floor_u32(log_panel_h, 64U);

            clks_panic_ui_panel(left_x, panel_y, left_w, log_panel_h,
                                CLKS_PANIC_TEXT("RECENT LOGS", "最近日志 (RECENT LOGS)"));
            if (clks_panic_ui_log_follow_tail == CLKS_TRUE) {
                u64 log_count = clks_log_journal_count();

                clks_panic_ui_log_total = (log_count == 0ULL) ? 1U : (u32)log_count;
                clks_panic_ui_log_visible = log_body_h / (clks_fb_cell_height() + 3U);
                clks_panic_ui_log_scroll = clks_panic_ui_scroll_max(clks_panic_ui_log_total, clks_panic_ui_log_visible);
            }
            clks_panic_ui_emit_logs(left_x + 16U, panel_y + 32U, clks_panic_ui_sub_floor_u32(left_w, 32U), log_body_h,
                                    clks_panic_ui_log_scroll, serial_backtrace);
            clks_panic_ui_draw_scroll_status(
                left_x + 16U, panel_y + log_panel_h - 24U, clks_panic_ui_sub_floor_u32(left_w, 32U),
                clks_panic_ui_log_scroll, clks_panic_ui_log_total, clks_panic_ui_log_visible,
                clks_panic_ui_active_pane == CLKS_PANIC_UI_PANE_LOGS ? CLKS_TRUE : CLKS_FALSE);
        } else {
            clks_panic_emit_recent_logs(CLKS_NULL);
        }

        clks_panic_ui_panel(right_x, content_y, right_w, bt_panel_h, CLKS_PANIC_TEXT("BACKTRACE", "回溯 (BACKTRACE)"));
        clks_panic_ui_emit_backtrace(right_x + 16U, content_y + 32U, clks_panic_ui_sub_floor_u32(right_w, 32U),
                                     clks_panic_ui_sub_floor_u32(bt_panel_h, 64U), clks_panic_screen.rip,
                                     clks_panic_screen.rbp, clks_panic_screen.rsp, clks_panic_ui_bt_scroll,
                                     serial_backtrace);
        clks_panic_ui_draw_scroll_status(
            right_x + 16U, content_y + bt_panel_h - 24U, clks_panic_ui_sub_floor_u32(right_w, 32U),
            clks_panic_ui_bt_scroll, clks_panic_ui_bt_total, clks_panic_ui_bt_visible,
            clks_panic_ui_active_pane == CLKS_PANIC_UI_PANE_BACKTRACE ? CLKS_TRUE : CLKS_FALSE);

        clks_fb_fill_rect(0U, info.height - footer_h, info.width, footer_h, CLKS_PANIC_UI_BG_2);
        clks_panic_ui_text_at(
            margin + 8U, info.height - footer_h + 12U,
            CLKS_PANIC_TEXT("Left/Right/Tab: pane   Up/Down/PgUp/PgDn/Home/End: scroll   Space: QR",
                            "Left/Right/Tab: 切换面板   Up/Down/PgUp/PgDn/Home/End: 滚动   Space: QR"),
            CLKS_PANIC_UI_MUTED, CLKS_PANIC_UI_BG_2, 0U);
    } else {
        clks_panic_serial_hex_field("[PANIC] RIP: ", clks_panic_screen.rip);
        clks_panic_serial_hex_field("[PANIC] CR2: ", clks_panic_screen.cr2);
        clks_panic_emit_current_process(CLKS_NULL);
        clks_panic_emit_backtrace(CLKS_NULL, clks_panic_screen.rip, clks_panic_screen.rbp, clks_panic_screen.rsp,
                                  serial_backtrace);
        clks_panic_emit_recent_logs(CLKS_NULL);
    }
}

static CLKS_NORETURN void clks_panic_halt_loop(void) {
    struct clks_panic_input_state input = {CLKS_FALSE, CLKS_FALSE};
    clks_bool qr_shown = CLKS_FALSE;

    for (;;) {
        enum clks_panic_key_action action = clks_panic_poll_key_action(&input);

        if (action != CLKS_PANIC_KEY_NONE) {
            if (action == CLKS_PANIC_KEY_SPACE) {
                if (qr_shown == CLKS_FALSE) {
                    if (clks_panic_qr_show() == CLKS_TRUE) {
                        qr_shown = CLKS_TRUE;
                        clks_panic_serial_write_line("[PANIC][QR] DISPLAYED");
                    } else {
                        clks_panic_serial_write_line("[PANIC][QR] PREPARE/SHOW FAILED");
                    }
                } else {
                    clks_panic_render_snapshot_console(CLKS_FALSE);
                    qr_shown = CLKS_FALSE;
                    clks_panic_serial_write_line("[PANIC][QR] RETURNED TO PANIC PAGE");
                }
            } else if (qr_shown == CLKS_FALSE) {
                clks_panic_ui_apply_scroll_action(action);
                clks_panic_render_snapshot_console(CLKS_FALSE);
            }
        }

        clks_cpu_pause();
    }
}

CLKS_NORETURN void clks_panic(const char *reason) {
    u64 rip = 0ULL;
    u64 rbp = 0ULL;
    u64 rsp = 0ULL;

    clks_panic_disable_interrupts();

    if (clks_panic_active == CLKS_TRUE) {
        clks_panic_halt_loop();
    }

    clks_panic_active = CLKS_TRUE;
    clks_panic_capture_context(&rip, &rbp, &rsp);

    clks_panic_serial_write_line("[PANIC] CLeonOS KERNEL PANIC");

    if (reason != CLKS_NULL) {
        clks_panic_serial_write_line(reason);
    }

    clks_panic_snapshot_reason(reason, rip, rbp, rsp);
    clks_panic_render_snapshot_console(CLKS_TRUE);

    if (clks_panic_qr_prepare() == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][QR] READY (PRESS SPACE TO TOGGLE)");
    } else {
        clks_panic_serial_write_line("[PANIC][QR] NOT AVAILABLE");
    }

    clks_panic_halt_loop();
}

CLKS_NORETURN void clks_panic_exception(const char *name, u64 vector, u64 error_code, u64 rip, u64 rbp, u64 rsp) {
    char hex_buf[19];

    clks_panic_disable_interrupts();

    if (clks_panic_active == CLKS_TRUE) {
        clks_panic_halt_loop();
    }

    clks_panic_active = CLKS_TRUE;

    clks_panic_serial_write_line("[PANIC] CPU EXCEPTION");

    if (name != CLKS_NULL) {
        clks_panic_serial_write_line(name);
    }

    clks_panic_u64_to_hex(vector, hex_buf);
    clks_panic_serial_write_line(hex_buf);
    clks_panic_u64_to_hex(error_code, hex_buf);
    clks_panic_serial_write_line(hex_buf);
    clks_panic_u64_to_hex(rip, hex_buf);
    clks_panic_serial_write_line(hex_buf);

    clks_panic_snapshot_exception(name, vector, error_code, rip, rbp, rsp);
    clks_panic_render_snapshot_console(CLKS_TRUE);

    if (clks_panic_qr_prepare() == CLKS_TRUE) {
        clks_panic_serial_write_line("[PANIC][QR] READY (PRESS SPACE TO TOGGLE)");
    } else {
        clks_panic_serial_write_line("[PANIC][QR] NOT AVAILABLE");
    }

    clks_panic_halt_loop();
}
