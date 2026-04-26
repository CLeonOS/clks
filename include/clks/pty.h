#ifndef CLKS_PTY_H
#define CLKS_PTY_H

#include <clks/types.h>

void clks_pty_init(void);
u64 clks_pty_alloc(void);
clks_bool clks_pty_retain(u64 pty_id);
void clks_pty_release(u64 pty_id);
u64 clks_pty_read(u64 pty_id, void *out_buffer, u64 size);
u64 clks_pty_write(u64 pty_id, const void *buffer, u64 size);

#endif
