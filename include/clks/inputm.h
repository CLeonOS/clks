#ifndef CLKS_INPUTM_H
#define CLKS_INPUTM_H

#include <clks/syscall.h>
#include <clks/types.h>

#define CLKS_INPUTM_STATUS_MAX 384U

void clks_inputm_init(void);
u64 clks_inputm_count(void);
clks_bool clks_inputm_info_at(u64 index, struct clks_inputm_info *out_info);
u64 clks_inputm_current(void);
clks_bool clks_inputm_select(u64 index);
u64 clks_inputm_register(const char *name, const char *path, u64 flags);
void clks_inputm_cycle(void);
clks_bool clks_inputm_handle_char(u32 tty_index, char ch);
const char *clks_inputm_current_name(void);
const char *clks_inputm_status_text(void);
clks_bool clks_inputm_status_is_composing(void);
void clks_inputm_status_set(const char *text);
void clks_inputm_status_copy(char *out_text, usize out_size);

#endif
