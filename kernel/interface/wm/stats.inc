#if CLKS_CFG_WM_STATS_OVERLAY != 0
static u32 clks_wm_u32_to_dec(char *out, u32 out_size, u32 value) {
    char tmp[10];
    u32 len = 0U;
    u32 i;

    if (out == CLKS_NULL || out_size == 0U) {
        return 0U;
    }

    if (value == 0U) {
        out[0] = '0';
        return 1U;
    }

    while (value > 0U && len < (u32)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    if (len > out_size) {
        len = out_size;
    }

    for (i = 0U; i < len; i++) {
        out[i] = tmp[len - 1U - i];
    }

    return len;
}

static clks_bool clks_wm_stats_rect(struct clks_wm_rect *out_rect) {
    struct clks_framebuffer_info fb;
    u32 cell_w;
    u32 cell_h;
    u32 box_w;
    u32 box_h;

    if (out_rect == CLKS_NULL) {
        return CLKS_FALSE;
    }

    fb = clks_fb_info();
    cell_w = clks_fb_cell_width();
    cell_h = clks_fb_cell_height();
    if (fb.width == 0U || fb.height == 0U || cell_w == 0U || cell_h == 0U) {
        return CLKS_FALSE;
    }

    box_w = (CLKS_WM_STATS_TEXT_CELLS * cell_w) + 8U;
    box_h = cell_h + 6U;
    if (box_w + 4U > fb.width || box_h + 4U > fb.height) {
        return CLKS_FALSE;
    }

    out_rect->l = (i32)(fb.width - box_w - 4U);
    out_rect->t = (i32)(fb.height - box_h - 4U);
    out_rect->r = (i32)(fb.width - 4U);
    out_rect->b = (i32)(fb.height - 4U);
    return CLKS_TRUE;
}
#endif

static void clks_wm_draw_stats_overlay_clipped(const struct clks_wm_rect *clip) {
#if CLKS_CFG_WM_STATS_OVERLAY == 0
    (void)clip;
    return;
#else
    struct clks_wm_rect box;
    struct clks_wm_rect clipped;
    u32 cell_w;
    u32 text_len = 0U;
    u32 i;
    char text[CLKS_WM_STATS_TEXT_CELLS];

    if (clip == CLKS_NULL || clks_wm_stats_rect(&box) == CLKS_FALSE ||
        clks_wm_rect_intersect(&box, clip, &clipped) == CLKS_FALSE) {
        return;
    }

    (void)clipped;
    cell_w = clks_fb_cell_width();
    clks_fb_fill_rect((u32)box.l, (u32)box.t, (u32)(box.r - box.l), (u32)(box.b - box.t), CLKS_WM_STATS_BG_COLOR);

    text[text_len++] = 'F';
    text[text_len++] = 'P';
    text[text_len++] = 'S';
    text[text_len++] = ':';
    text_len += clks_wm_u32_to_dec(text + text_len, (u32)sizeof(text) - text_len, clks_wm_timing.fps);
    text[text_len++] = ' ';
    text[text_len++] = 'M';
    text[text_len++] = 'S';
    text[text_len++] = ':';
    text_len += clks_wm_u32_to_dec(text + text_len, (u32)sizeof(text) - text_len, clks_wm_timing.frame_ms);

    for (i = 0U; i < CLKS_WM_STATS_TEXT_CELLS; i++) {
        char ch = (i < text_len) ? text[i] : ' ';
        clks_fb_draw_char((u32)box.l + 4U + (i * cell_w), (u32)box.t + 3U, ch, CLKS_WM_STATS_FG_COLOR,
                          CLKS_WM_STATS_BG_COLOR);
    }
#endif
}
