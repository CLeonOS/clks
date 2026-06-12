#ifndef CLKS_LOG_H
#define CLKS_LOG_H

#include <clks/types.h>

enum clks_log_level {
    CLKS_LOG_DEBUG = 0,
    CLKS_LOG_INFO = 1,
    CLKS_LOG_WARN = 2,
    CLKS_LOG_ERROR = 3,
};

void clks_log_set_min_level(enum clks_log_level level);
enum clks_log_level clks_log_min_level(void);
void clks_log(enum clks_log_level level, const char *tag, const char *message);
void clks_log_u64(enum clks_log_level level, const char *tag, const char *label, u64 value);
void clks_log_bytes(enum clks_log_level level, const char *tag, const char *label, u64 value);
void clks_log_bool(enum clks_log_level level, const char *tag, const char *label, clks_bool value);
void clks_log_hex(enum clks_log_level level, const char *tag, const char *label, u64 value);

u64 clks_log_journal_count(void);
clks_bool clks_log_journal_read(u64 index_from_oldest, char *out_line, usize out_line_size);

#endif
