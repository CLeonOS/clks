static void clks_wm_layer_free(void) {
    if (clks_wm_layers.scene != CLKS_NULL) {
        clks_kfree(clks_wm_layers.scene);
    }

    clks_memset(&clks_wm_layers, 0, sizeof(clks_wm_layers));
}

static clks_bool clks_wm_layer_ensure(void) {
#if CLKS_CFG_WM_LAYER_CACHE == 0
    clks_wm_layer_free();
    return CLKS_FALSE;
#else
    u64 pixel_count;
    u64 bytes;
    u32 width = clks_wm_width();
    u32 height = clks_wm_height();

    if (width == 0U || height == 0U) {
        return CLKS_FALSE;
    }

    if (clks_wm_layers.ready == CLKS_TRUE && clks_wm_layers.width == width && clks_wm_layers.height == height) {
        return CLKS_TRUE;
    }

    clks_wm_layer_free();

    pixel_count = (u64)width * (u64)height;
    bytes = pixel_count * 4ULL;
    if (pixel_count == 0ULL || bytes == 0ULL || bytes > (u64)(~(usize)0U)) {
        return CLKS_FALSE;
    }

    clks_wm_layers.scene = (u32 *)clks_kmalloc((usize)bytes);
    if (clks_wm_layers.scene == CLKS_NULL) {
        clks_wm_layer_free();
        return CLKS_FALSE;
    }

    clks_wm_layers.width = width;
    clks_wm_layers.height = height;
    clks_wm_layers.pitch_bytes = width * 4U;
    clks_wm_layers.pixel_count = pixel_count;
    clks_wm_layers.ready = CLKS_TRUE;
    clks_wm_layers.background_dirty = CLKS_TRUE;
    return CLKS_TRUE;
#endif
}

static void clks_wm_layer_fill_rect(u32 *layer, const struct clks_wm_rect *rect, u32 color) {
    u32 y;
    u32 width;

    if (layer == CLKS_NULL || rect == CLKS_NULL || clks_wm_layers.ready == CLKS_FALSE) {
        return;
    }

    if (rect->r <= rect->l || rect->b <= rect->t) {
        return;
    }

    width = (u32)(rect->r - rect->l);
    for (y = (u32)rect->t; y < (u32)rect->b; y++) {
        u32 *row = layer + ((u64)y * (u64)clks_wm_layers.width) + (u32)rect->l;
        clks_wm_fill_span32(row, width, color);
    }
}

static void clks_wm_layer_fill_box_clipped(u32 *layer, i32 x, i32 y, i32 w, i32 h, u32 color,
                                           const struct clks_wm_rect *clip) {
    struct clks_wm_rect src;
    struct clks_wm_rect clipped;

    if (clip == CLKS_NULL || w <= 0 || h <= 0) {
        return;
    }

    src.l = x;
    src.t = y;
    src.r = x + w;
    src.b = y + h;

    if (clks_wm_rect_intersect(&src, clip, &clipped) == CLKS_FALSE) {
        return;
    }

    clks_wm_layer_fill_rect(layer, &clipped, color);
}

static void clks_wm_copy_background_to_scene(const struct clks_wm_rect *rect) {
    struct clks_wm_rect body;
    struct clks_wm_rect topbar;
    struct clks_wm_rect clipped;

    if (rect == CLKS_NULL || clks_wm_layers.ready == CLKS_FALSE) {
        return;
    }

    if (rect->r <= rect->l || rect->b <= rect->t) {
        return;
    }

    body = *rect;
    if (body.t < (i32)CLKS_WM_TOPBAR_HEIGHT) {
        body.t = (i32)CLKS_WM_TOPBAR_HEIGHT;
    }
    if (body.b > body.t) {
        clks_wm_layer_fill_rect(clks_wm_layers.scene, &body, (u32)CLKS_WM_BG_COLOR);
    }

    topbar.l = 0;
    topbar.t = 0;
    topbar.r = (i32)clks_wm_layers.width;
    topbar.b = (i32)CLKS_WM_TOPBAR_HEIGHT;
    if (clks_wm_rect_intersect(rect, &topbar, &clipped) == CLKS_TRUE) {
        clks_wm_layer_fill_rect(clks_wm_layers.scene, &clipped, (u32)CLKS_WM_TOPBAR_COLOR);
    }
}

static void clks_wm_fill_fb_box_clipped(i32 x, i32 y, i32 w, i32 h, u32 color, const struct clks_wm_rect *clip) {
    struct clks_wm_rect src;
    struct clks_wm_rect clipped;

    if (clip == CLKS_NULL || w <= 0 || h <= 0) {
        return;
    }

    src.l = x;
    src.t = y;
    src.r = x + w;
    src.b = y + h;

    if (clks_wm_rect_intersect(&src, clip, &clipped) == CLKS_FALSE) {
        return;
    }

    clks_wm_fb_fill_rect(clipped.l, clipped.t, (u32)(clipped.r - clipped.l), (u32)(clipped.b - clipped.t), color);
}

static void clks_wm_copy_background_to_fb(const struct clks_wm_rect *rect) {
    struct clks_wm_rect body;
    struct clks_wm_rect topbar;
    struct clks_wm_rect clipped;
    u32 width;

    if (rect == CLKS_NULL || rect->r <= rect->l || rect->b <= rect->t) {
        return;
    }

    width = clks_wm_width();
    body = *rect;
    if (body.t < (i32)CLKS_WM_TOPBAR_HEIGHT) {
        body.t = (i32)CLKS_WM_TOPBAR_HEIGHT;
    }
    if (body.b > body.t) {
        clks_wm_fb_fill_rect(body.l, body.t, (u32)(body.r - body.l), (u32)(body.b - body.t), (u32)CLKS_WM_BG_COLOR);
    }

    topbar.l = 0;
    topbar.t = 0;
    topbar.r = (i32)width;
    topbar.b = (i32)CLKS_WM_TOPBAR_HEIGHT;
    if (clks_wm_rect_intersect(rect, &topbar, &clipped) == CLKS_TRUE) {
        clks_wm_fb_fill_rect(clipped.l, clipped.t, (u32)(clipped.r - clipped.l), (u32)(clipped.b - clipped.t),
                             (u32)CLKS_WM_TOPBAR_COLOR);
    }
}
