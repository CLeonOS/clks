#include <clks/log.h>
#include <clks/bootsplash.h>
#include <clks/rust.h>
#include <clks/serial.h>
#include <clks/tty.h>
#include <clks/types.h>

#define CLKS_LOG_LINE_MAX 256

#define CLKS_LOG_ANSI_RESET "\x1B[0m"

#ifndef CLKS_CFG_LOG_LEVEL_DEBUG
#define CLKS_CFG_LOG_LEVEL_DEBUG 1
#endif

#ifndef CLKS_CFG_LOG_LEVEL_INFO
#define CLKS_CFG_LOG_LEVEL_INFO 1
#endif

#ifndef CLKS_CFG_LOG_LEVEL_WARN
#define CLKS_CFG_LOG_LEVEL_WARN 1
#endif

#ifndef CLKS_CFG_LOG_LEVEL_ERROR
#define CLKS_CFG_LOG_LEVEL_ERROR 1
#endif

#ifndef CLKS_CFG_LOG_OUTPUT_SERIAL
#define CLKS_CFG_LOG_OUTPUT_SERIAL 1
#endif

#ifndef CLKS_CFG_LOG_OUTPUT_TTY
#define CLKS_CFG_LOG_OUTPUT_TTY 1
#endif

#ifndef CLKS_CFG_LOG_OUTPUT_JOURNAL
#define CLKS_CFG_LOG_OUTPUT_JOURNAL 1
#endif

static enum clks_log_level clks_log_runtime_min_level = CLKS_LOG_DEBUG;

static void clks_log_build_line(enum clks_log_level level, const char *tag, const char *message, char *line);
static void clks_log_emit_line(enum clks_log_level level, const char *tag, const char *message, const char *line);

static const char *clks_log_level_name(enum clks_log_level level) {
    switch (level) {
    case CLKS_LOG_DEBUG:
        return "DEBUG";
    case CLKS_LOG_INFO:
        return "INFO";
    case CLKS_LOG_WARN:
        return "WARN";
    case CLKS_LOG_ERROR:
        return "ERROR";
    default:
        return "UNK";
    }
}

void clks_log_init(void) {
    clks_rust_klog_init();
}

static clks_bool clks_log_level_enabled(enum clks_log_level level) {
    if (level < clks_log_runtime_min_level) {
        return CLKS_FALSE;
    }

    switch (level) {
    case CLKS_LOG_DEBUG:
        return (CLKS_CFG_LOG_LEVEL_DEBUG != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_LOG_INFO:
        return (CLKS_CFG_LOG_LEVEL_INFO != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_LOG_WARN:
        return (CLKS_CFG_LOG_LEVEL_WARN != 0) ? CLKS_TRUE : CLKS_FALSE;
    case CLKS_LOG_ERROR:
        return (CLKS_CFG_LOG_LEVEL_ERROR != 0) ? CLKS_TRUE : CLKS_FALSE;
    default:
        return CLKS_FALSE;
    }
}

void clks_log_set_min_level(enum clks_log_level level) {
    if (level > CLKS_LOG_ERROR) {
        level = CLKS_LOG_ERROR;
    }

    clks_log_runtime_min_level = level;
}

enum clks_log_level clks_log_min_level(void) {
    return clks_log_runtime_min_level;
}

static void clks_log_append_char(char *buffer, usize *cursor, char ch) {
    if (*cursor >= (CLKS_LOG_LINE_MAX - 1)) {
        return;
    }

    buffer[*cursor] = ch;
    (*cursor)++;
}

static void clks_log_append_text(char *buffer, usize *cursor, const char *text) {
    usize i = 0;

    if (text == CLKS_NULL) {
        return;
    }

    while (text[i] != '\0') {
        clks_log_append_char(buffer, cursor, text[i]);
        i++;
    }
}

static void clks_log_append_hex_u64(char *buffer, usize *cursor, u64 value) {
    int nibble;

    clks_log_append_text(buffer, cursor, "0X");

    for (nibble = 15; nibble >= 0; nibble--) {
        u8 current = (u8)((value >> (nibble * 4)) & 0x0FULL);
        char out = (current < 10) ? (char)('0' + current) : (char)('A' + (current - 10));
        clks_log_append_char(buffer, cursor, out);
    }
}

static void clks_log_append_u64(char *buffer, usize *cursor, u64 value) {
    char digits[20];
    usize count = 0U;

    if (value == 0ULL) {
        clks_log_append_char(buffer, cursor, '0');
        return;
    }

    while (value != 0ULL && count < (usize)sizeof(digits)) {
        digits[count++] = (char)('0' + (char)(value % 10ULL));
        value /= 10ULL;
    }

    while (count > 0U) {
        count--;
        clks_log_append_char(buffer, cursor, digits[count]);
    }
}

static void clks_log_append_labeled_value_prefix(char *message, usize *cursor, const char *label) {
    clks_log_append_text(message, cursor, (label == CLKS_NULL) ? "value" : label);
    clks_log_append_char(message, cursor, ':');
    clks_log_append_char(message, cursor, ' ');
}

static void clks_log_emit_value_message(enum clks_log_level level, const char *tag, const char *message) {
    char line[CLKS_LOG_LINE_MAX];

    clks_log_build_line(level, tag, message, line);
    clks_log_emit_line(level, tag, message, line);
}

#if CLKS_CFG_LOG_OUTPUT_JOURNAL != 0
static void clks_log_journal_push(enum clks_log_level level, const char *tag, const char *line) {
    if (line == CLKS_NULL) {
        return;
    }

    (void)clks_rust_klog_push((u32)level, tag, line);
}
#endif

#if CLKS_CFG_LOG_OUTPUT_TTY != 0
static void clks_log_append_char_cap(char *buffer, usize cap, usize *cursor, char ch) {
    if (buffer == CLKS_NULL || cursor == CLKS_NULL || cap == 0U) {
        return;
    }

    if (*cursor >= (cap - 1U)) {
        return;
    }

    buffer[*cursor] = ch;
    (*cursor)++;
}

static void clks_log_append_text_cap(char *buffer, usize cap, usize *cursor, const char *text) {
    usize i = 0U;

    if (text == CLKS_NULL) {
        return;
    }

    while (text[i] != '\0') {
        clks_log_append_char_cap(buffer, cap, cursor, text[i]);
        i++;
    }
}

static const char *clks_log_level_ansi(enum clks_log_level level) {
    switch (level) {
    case CLKS_LOG_DEBUG:
        return "\x1B[38;5;110m";
    case CLKS_LOG_INFO:
        return "\x1B[38;5;120m";
    case CLKS_LOG_WARN:
        return "\x1B[1;38;5;220m";
    case CLKS_LOG_ERROR:
        return "\x1B[1;38;5;203m";
    default:
        return "\x1B[38;5;250m";
    }
}

static const char *clks_log_tag_ansi(const char *tag) {
    static const char *palette[] = {
        "\x1B[38;5;81m", "\x1B[38;5;117m", "\x1B[38;5;159m", "\x1B[38;5;45m",
        "\x1B[38;5;75m", "\x1B[38;5;141m", "\x1B[38;5;214m", "\x1B[38;5;168m",
    };
    u32 hash = 5381U;
    usize i = 0U;
    usize palette_count = sizeof(palette) / sizeof(palette[0]);

    if (tag == CLKS_NULL || tag[0] == '\0') {
        return palette[0];
    }

    while (tag[i] != '\0') {
        hash = ((hash << 5U) + hash) ^ (u32)(u8)tag[i];
        i++;
    }

    return palette[hash % (u32)palette_count];
}

static void clks_log_emit_tty_colored(enum clks_log_level level, const char *tag, const char *message) {
    const char *safe_tag = (tag == CLKS_NULL) ? "LOG" : tag;
    const char *safe_message = (message == CLKS_NULL) ? "" : message;
    char tty_line[CLKS_LOG_LINE_MAX + 128];
    usize cursor = 0U;

    clks_log_append_text_cap(tty_line, sizeof(tty_line), &cursor, clks_log_level_ansi(level));
    clks_log_append_char_cap(tty_line, sizeof(tty_line), &cursor, '[');
    clks_log_append_text_cap(tty_line, sizeof(tty_line), &cursor, clks_log_level_name(level));
    clks_log_append_char_cap(tty_line, sizeof(tty_line), &cursor, ']');

    clks_log_append_text_cap(tty_line, sizeof(tty_line), &cursor, clks_log_tag_ansi(safe_tag));
    clks_log_append_char_cap(tty_line, sizeof(tty_line), &cursor, '[');
    clks_log_append_text_cap(tty_line, sizeof(tty_line), &cursor, safe_tag);
    clks_log_append_char_cap(tty_line, sizeof(tty_line), &cursor, ']');

    clks_log_append_text_cap(tty_line, sizeof(tty_line), &cursor, CLKS_LOG_ANSI_RESET);
    clks_log_append_char_cap(tty_line, sizeof(tty_line), &cursor, ' ');
    clks_log_append_text_cap(tty_line, sizeof(tty_line), &cursor, safe_message);
    clks_log_append_char_cap(tty_line, sizeof(tty_line), &cursor, '\n');
    clks_log_append_char_cap(tty_line, sizeof(tty_line), &cursor, '\0');

    if (cursor > 0U) {
        clks_tty_write_n(tty_line, cursor - 1U);
    }
}
#endif

static void clks_log_build_line(enum clks_log_level level, const char *tag, const char *message, char *line) {
    const char *safe_tag = (tag == CLKS_NULL) ? "LOG" : tag;
    const char *safe_message = (message == CLKS_NULL) ? "" : message;
    usize cursor = 0U;

    if (line == CLKS_NULL) {
        return;
    }

    clks_log_append_char(line, &cursor, '[');
    clks_log_append_text(line, &cursor, clks_log_level_name(level));
    clks_log_append_char(line, &cursor, ']');
    clks_log_append_char(line, &cursor, '[');
    clks_log_append_text(line, &cursor, safe_tag);
    clks_log_append_char(line, &cursor, ']');
    clks_log_append_char(line, &cursor, ' ');
    clks_log_append_text(line, &cursor, safe_message);
    line[cursor] = '\0';
}

static void clks_log_emit_line(enum clks_log_level level, const char *tag, const char *message, const char *line) {
    if (line == CLKS_NULL) {
        return;
    }

#if CLKS_CFG_LOG_OUTPUT_JOURNAL != 0
    clks_log_journal_push(level, tag, line);
#endif

#if CLKS_CFG_LOG_OUTPUT_SERIAL != 0
    clks_serial_write(line);
    clks_serial_write("\n");
#endif

#if CLKS_CFG_LOG_OUTPUT_TTY != 0
    if (clks_bootsplash_active() == CLKS_FALSE) {
        clks_log_emit_tty_colored(level, tag, message);
    }
#else
    (void)level;
    (void)tag;
    (void)message;
#endif
}

void clks_log(enum clks_log_level level, const char *tag, const char *message) {
    char line[CLKS_LOG_LINE_MAX];

    if (clks_log_level_enabled(level) == CLKS_FALSE) {
        return;
    }

    clks_log_build_line(level, tag, message, line);
    clks_log_emit_line(level, tag, message, line);
}

void clks_log_u64(enum clks_log_level level, const char *tag, const char *label, u64 value) {
    char message[CLKS_LOG_LINE_MAX];
    usize cursor = 0U;

    if (clks_log_level_enabled(level) == CLKS_FALSE) {
        return;
    }

    clks_log_append_labeled_value_prefix(message, &cursor, label);
    clks_log_append_u64(message, &cursor, value);
    message[cursor] = '\0';

    clks_log_emit_value_message(level, tag, message);
}

void clks_log_bytes(enum clks_log_level level, const char *tag, const char *label, u64 value) {
    char message[CLKS_LOG_LINE_MAX];
    usize cursor = 0U;
    u64 mib = value / (1024ULL * 1024ULL);
    u64 kib = value / 1024ULL;

    if (clks_log_level_enabled(level) == CLKS_FALSE) {
        return;
    }

    clks_log_append_labeled_value_prefix(message, &cursor, label);
    clks_log_append_u64(message, &cursor, value);
    clks_log_append_text(message, &cursor, " bytes");

    if (mib != 0ULL) {
        clks_log_append_text(message, &cursor, " (");
        clks_log_append_u64(message, &cursor, mib);
        clks_log_append_text(message, &cursor, " MiB)");
    } else if (kib != 0ULL) {
        clks_log_append_text(message, &cursor, " (");
        clks_log_append_u64(message, &cursor, kib);
        clks_log_append_text(message, &cursor, " KiB)");
    }

    message[cursor] = '\0';

    clks_log_emit_value_message(level, tag, message);
}

void clks_log_bool(enum clks_log_level level, const char *tag, const char *label, clks_bool value) {
    char message[CLKS_LOG_LINE_MAX];
    usize cursor = 0U;

    if (clks_log_level_enabled(level) == CLKS_FALSE) {
        return;
    }

    clks_log_append_labeled_value_prefix(message, &cursor, label);
    clks_log_append_text(message, &cursor, (value == CLKS_TRUE) ? "yes" : "no");
    message[cursor] = '\0';

    clks_log_emit_value_message(level, tag, message);
}

void clks_log_hex(enum clks_log_level level, const char *tag, const char *label, u64 value) {
    char message[CLKS_LOG_LINE_MAX];
    usize cursor = 0U;

    if (clks_log_level_enabled(level) == CLKS_FALSE) {
        return;
    }

    clks_log_append_labeled_value_prefix(message, &cursor, label);
    clks_log_append_hex_u64(message, &cursor, value);
    message[cursor] = '\0';

    clks_log_emit_value_message(level, tag, message);
}

u64 clks_log_journal_count(void) {
    return clks_rust_klog_count();
}

clks_bool clks_log_journal_read(u64 index_from_oldest, char *out_line, usize out_line_size) {
    if (out_line == CLKS_NULL || out_line_size == 0U) {
        return CLKS_FALSE;
    }

    out_line[0] = '\0';

    return clks_rust_klog_read(index_from_oldest, out_line, out_line_size);
}

u64 clks_log_journal_count_filtered(enum clks_log_level min_level, const char *tag) {
    if (min_level > CLKS_LOG_ERROR) {
        min_level = CLKS_LOG_ERROR;
    }

    return clks_rust_klog_count_filtered((u32)min_level, tag);
}

clks_bool clks_log_journal_read_filtered(enum clks_log_level min_level, const char *tag, u64 index_from_oldest_matching,
                                         char *out_line, usize out_line_size) {
    if (out_line == CLKS_NULL || out_line_size == 0U) {
        return CLKS_FALSE;
    }

    out_line[0] = '\0';

    if (min_level > CLKS_LOG_ERROR) {
        min_level = CLKS_LOG_ERROR;
    }

    return clks_rust_klog_read_filtered((u32)min_level, tag, index_from_oldest_matching, out_line, out_line_size);
}
