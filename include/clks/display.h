#ifndef CLKS_DISPLAY_H
#define CLKS_DISPLAY_H

#include <clks/types.h>

#define CLKS_DISPLAY_TARGET_TTY 0U
#define CLKS_DISPLAY_TARGET_WM 1U
#define CLKS_DISPLAY_TARGET_COUNT 2U

struct clks_display_mode {
    u32 physical_width;
    u32 physical_height;
    u32 logical_width;
    u32 logical_height;
};

void clks_display_init(void);
clks_bool clks_display_mode_get(u32 target, struct clks_display_mode *out_mode);
clks_bool clks_display_mode_set(u32 target, u32 logical_width, u32 logical_height);
u32 clks_display_width(u32 target);
u32 clks_display_height(u32 target);
i32 clks_display_origin_x(u32 target);
i32 clks_display_origin_y(u32 target);
i32 clks_display_map_x_phys_to_logical(u32 target, i32 physical_x);
i32 clks_display_map_y_phys_to_logical(u32 target, i32 physical_y);

#endif
