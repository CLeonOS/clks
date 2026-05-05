#include <clks/display.h>
#include <clks/framebuffer.h>
#include <clks/types.h>

struct clks_display_target_state {
    u32 logical_width;
    u32 logical_height;
};

static struct clks_display_target_state clks_display_targets[CLKS_DISPLAY_TARGET_COUNT];
static u32 clks_display_physical_width = 0U;
static u32 clks_display_physical_height = 0U;
static clks_bool clks_display_ready = CLKS_FALSE;

static u32 clks_display_clamp_dimension(u32 value, u32 physical, u32 min_value) {
    if (physical == 0U) {
        return 0U;
    }

    if (value < min_value) {
        value = min_value;
    }

    if (value > physical) {
        value = physical;
    }

    return value;
}

static clks_bool clks_display_target_valid(u32 target) {
    return (target < CLKS_DISPLAY_TARGET_COUNT) ? CLKS_TRUE : CLKS_FALSE;
}

static i32 clks_display_origin_axis(u32 physical, u32 logical) {
    if (physical <= logical) {
        return 0;
    }

    return (i32)((physical - logical) / 2U);
}

void clks_display_init(void) {
    struct clks_framebuffer_info fb;
    u32 target;

    if (clks_fb_ready() == CLKS_FALSE) {
        clks_display_ready = CLKS_FALSE;
        clks_display_physical_width = 0U;
        clks_display_physical_height = 0U;
        return;
    }

    fb = clks_fb_info();
    clks_display_physical_width = fb.width;
    clks_display_physical_height = fb.height;

    for (target = 0U; target < CLKS_DISPLAY_TARGET_COUNT; target++) {
        clks_display_targets[target].logical_width = fb.width;
        clks_display_targets[target].logical_height = fb.height;
    }

    clks_display_ready = (fb.width > 0U && fb.height > 0U) ? CLKS_TRUE : CLKS_FALSE;
}

clks_bool clks_display_mode_get(u32 target, struct clks_display_mode *out_mode) {
    if (out_mode == CLKS_NULL || clks_display_ready == CLKS_FALSE || clks_display_target_valid(target) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    out_mode->physical_width = clks_display_physical_width;
    out_mode->physical_height = clks_display_physical_height;
    out_mode->logical_width = clks_display_targets[target].logical_width;
    out_mode->logical_height = clks_display_targets[target].logical_height;
    return CLKS_TRUE;
}

clks_bool clks_display_mode_set(u32 target, u32 logical_width, u32 logical_height) {
    if (clks_display_ready == CLKS_FALSE || clks_display_target_valid(target) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    logical_width = clks_display_clamp_dimension(logical_width, clks_display_physical_width, 64U);
    logical_height = clks_display_clamp_dimension(logical_height, clks_display_physical_height, 48U);

    if (logical_width == 0U || logical_height == 0U) {
        return CLKS_FALSE;
    }

    clks_display_targets[target].logical_width = logical_width;
    clks_display_targets[target].logical_height = logical_height;
    return CLKS_TRUE;
}

u32 clks_display_width(u32 target) {
    if (clks_display_ready == CLKS_FALSE || clks_display_target_valid(target) == CLKS_FALSE) {
        return 0U;
    }

    return clks_display_targets[target].logical_width;
}

u32 clks_display_height(u32 target) {
    if (clks_display_ready == CLKS_FALSE || clks_display_target_valid(target) == CLKS_FALSE) {
        return 0U;
    }

    return clks_display_targets[target].logical_height;
}

i32 clks_display_origin_x(u32 target) {
    if (clks_display_ready == CLKS_FALSE || clks_display_target_valid(target) == CLKS_FALSE) {
        return 0;
    }

    return clks_display_origin_axis(clks_display_physical_width, clks_display_targets[target].logical_width);
}

i32 clks_display_origin_y(u32 target) {
    if (clks_display_ready == CLKS_FALSE || clks_display_target_valid(target) == CLKS_FALSE) {
        return 0;
    }

    return clks_display_origin_axis(clks_display_physical_height, clks_display_targets[target].logical_height);
}

i32 clks_display_map_x_phys_to_logical(u32 target, i32 physical_x) {
    u32 logical_width;
    i32 origin_x;

    if (clks_display_ready == CLKS_FALSE || clks_display_target_valid(target) == CLKS_FALSE) {
        return physical_x;
    }

    logical_width = clks_display_targets[target].logical_width;
    origin_x = clks_display_origin_x(target);
    if (logical_width == 0U) {
        return physical_x;
    }

    return physical_x - origin_x;
}

i32 clks_display_map_y_phys_to_logical(u32 target, i32 physical_y) {
    u32 logical_height;
    i32 origin_y;

    if (clks_display_ready == CLKS_FALSE || clks_display_target_valid(target) == CLKS_FALSE) {
        return physical_y;
    }

    logical_height = clks_display_targets[target].logical_height;
    origin_y = clks_display_origin_y(target);
    if (logical_height == 0U) {
        return physical_y;
    }

    return physical_y - origin_y;
}
