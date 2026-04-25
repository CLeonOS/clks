#ifndef CLKS_WM_H
#define CLKS_WM_H

#include <clks/types.h>

#define CLKS_WM_EVENT_FOCUS_GAINED 1ULL
#define CLKS_WM_EVENT_FOCUS_LOST 2ULL
#define CLKS_WM_EVENT_KEY 3ULL
#define CLKS_WM_EVENT_MOUSE_MOVE 4ULL
#define CLKS_WM_EVENT_MOUSE_BUTTON 5ULL

#define CLKS_WM_FLAG_TOPMOST 0x1ULL

struct clks_wm_event {
    u64 type;
    u64 arg0;
    u64 arg1;
    u64 arg2;
    u64 arg3;
};

void clks_wm_init(void);
void clks_wm_tick(u64 tick);
clks_bool clks_wm_ready(void);
clks_bool clks_wm_is_foreground(void);

u64 clks_wm_create(u64 owner_pid, i32 x, i32 y, u32 width, u32 height, u64 flags);
clks_bool clks_wm_destroy(u64 owner_pid, u64 window_id);
clks_bool clks_wm_present(u64 owner_pid, u64 window_id, const void *pixels, u32 src_width, u32 src_height,
                          u32 src_pitch_bytes);
clks_bool clks_wm_poll_event(u64 owner_pid, u64 window_id, struct clks_wm_event *out_event);
clks_bool clks_wm_move(u64 owner_pid, u64 window_id, i32 x, i32 y);
clks_bool clks_wm_set_focus(u64 owner_pid, u64 window_id);
clks_bool clks_wm_set_flags(u64 owner_pid, u64 window_id, u64 flags);
clks_bool clks_wm_resize(u64 owner_pid, u64 window_id, u32 width, u32 height);

#endif
