static clks_bool clks_wm_rect_clip_to_screen(i32 l, i32 t, i32 r, i32 b, struct clks_wm_rect *out_rect) {
    struct clks_framebuffer_info fb;
    i32 max_r;
    i32 max_b;

    if (out_rect == CLKS_NULL) {
        return CLKS_FALSE;
    }

    fb = clks_fb_info();
    if (fb.width == 0U || fb.height == 0U) {
        return CLKS_FALSE;
    }

    max_r = (i32)fb.width;
    max_b = (i32)fb.height;

    if (l < 0) {
        l = 0;
    }
    if (t < 0) {
        t = 0;
    }
    if (r > max_r) {
        r = max_r;
    }
    if (b > max_b) {
        b = max_b;
    }

    if (r <= l || b <= t) {
        return CLKS_FALSE;
    }

    out_rect->l = l;
    out_rect->t = t;
    out_rect->r = r;
    out_rect->b = b;
    return CLKS_TRUE;
}

static clks_bool clks_wm_rect_intersect(const struct clks_wm_rect *a, const struct clks_wm_rect *b,
                                        struct clks_wm_rect *out_rect) {
    i32 l;
    i32 t;
    i32 r;
    i32 bottom;

    if (a == CLKS_NULL || b == CLKS_NULL || out_rect == CLKS_NULL) {
        return CLKS_FALSE;
    }

    l = (a->l > b->l) ? a->l : b->l;
    t = (a->t > b->t) ? a->t : b->t;
    r = (a->r < b->r) ? a->r : b->r;
    bottom = (a->b < b->b) ? a->b : b->b;

    if (r <= l || bottom <= t) {
        return CLKS_FALSE;
    }

    out_rect->l = l;
    out_rect->t = t;
    out_rect->r = r;
    out_rect->b = bottom;
    return CLKS_TRUE;
}

static u64 clks_wm_rect_area(const struct clks_wm_rect *rect) {
    if (rect == CLKS_NULL || rect->r <= rect->l || rect->b <= rect->t) {
        return 0ULL;
    }

    return (u64)(u32)(rect->r - rect->l) * (u64)(u32)(rect->b - rect->t);
}

static struct clks_wm_rect clks_wm_rect_union(const struct clks_wm_rect *a, const struct clks_wm_rect *b) {
    struct clks_wm_rect out;

    out.l = (a->l < b->l) ? a->l : b->l;
    out.t = (a->t < b->t) ? a->t : b->t;
    out.r = (a->r > b->r) ? a->r : b->r;
    out.b = (a->b > b->b) ? a->b : b->b;
    return out;
}

static clks_bool clks_wm_rect_contains(const struct clks_wm_rect *outer, const struct clks_wm_rect *inner) {
    if (outer == CLKS_NULL || inner == CLKS_NULL) {
        return CLKS_FALSE;
    }

    return (outer->l <= inner->l && outer->t <= inner->t && outer->r >= inner->r && outer->b >= inner->b) ? CLKS_TRUE
                                                                                                          : CLKS_FALSE;
}

static void clks_wm_fill_span32(u32 *dst, u32 pixel_count, u32 color) {
    if (dst == CLKS_NULL || pixel_count == 0U) {
        return;
    }

#if defined(CLKS_ARCH_X86_64)
    {
        usize n = (usize)pixel_count;
        __asm__ volatile("rep stosl" : "+D"(dst), "+c"(n) : "a"(color) : "memory");
    }
#else
    {
        u32 i;

        for (i = 0U; i < pixel_count; i++) {
            dst[i] = color;
        }
    }
#endif
}

static clks_bool clks_wm_rect_mergeable(const struct clks_wm_rect *a, const struct clks_wm_rect *b) {
    if (a == CLKS_NULL || b == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (a->r + CLKS_WM_DAMAGE_MERGE_GAP < b->l || b->r + CLKS_WM_DAMAGE_MERGE_GAP < a->l) {
        return CLKS_FALSE;
    }

    if (a->b + CLKS_WM_DAMAGE_MERGE_GAP < b->t || b->b + CLKS_WM_DAMAGE_MERGE_GAP < a->t) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static void clks_wm_damage_remove(struct clks_wm_damage_list *list, u32 index) {
    u32 i;

    if (list == CLKS_NULL || index >= list->count) {
        return;
    }

    for (i = index; i + 1U < list->count; i++) {
        list->rects[i] = list->rects[i + 1U];
    }

    list->count--;
}

static void clks_wm_damage_force_full(struct clks_wm_damage_list *list) {
    struct clks_framebuffer_info fb = clks_fb_info();

    if (list == CLKS_NULL || fb.width == 0U || fb.height == 0U) {
        return;
    }

    list->count = 1U;
    list->rects[0].l = 0;
    list->rects[0].t = 0;
    list->rects[0].r = (i32)fb.width;
    list->rects[0].b = (i32)fb.height;
}

static void clks_wm_damage_add(struct clks_wm_damage_list *list, const struct clks_wm_rect *input) {
    struct clks_wm_rect rect;
    u32 i;
    u64 total_area;
    u64 screen_area;

    if (list == CLKS_NULL || input == CLKS_NULL) {
        return;
    }

    if (clks_wm_rect_clip_to_screen(input->l, input->t, input->r, input->b, &rect) == CLKS_FALSE) {
        return;
    }

#if CLKS_CFG_WM_MULTI_RECT_DAMAGE == 0
    if (list->count == 0U) {
        list->count = 1U;
        list->rects[0] = rect;
    } else {
        list->rects[0] = clks_wm_rect_union(&list->rects[0], &rect);
    }
    return;
#endif

    i = 0U;
    while (i < list->count) {
        if (clks_wm_rect_contains(&list->rects[i], &rect) == CLKS_TRUE) {
            return;
        }

        if (clks_wm_rect_contains(&rect, &list->rects[i]) == CLKS_TRUE ||
            clks_wm_rect_mergeable(&list->rects[i], &rect) == CLKS_TRUE) {
            rect = clks_wm_rect_union(&rect, &list->rects[i]);
            clks_wm_damage_remove(list, i);
            continue;
        }

        i++;
    }

    if (list->count < CLKS_WM_MAX_DAMAGE_RECTS) {
        list->rects[list->count++] = rect;
    } else {
        u32 best = 0U;
        u64 best_growth = ~(u64)0ULL;

        for (i = 0U; i < list->count; i++) {
            struct clks_wm_rect merged = clks_wm_rect_union(&list->rects[i], &rect);
            u64 old_area = clks_wm_rect_area(&list->rects[i]);
            u64 new_area = clks_wm_rect_area(&merged);
            u64 growth = (new_area > old_area) ? (new_area - old_area) : 0ULL;

            if (growth < best_growth) {
                best_growth = growth;
                best = i;
            }
        }

        list->rects[best] = clks_wm_rect_union(&list->rects[best], &rect);
    }

    total_area = 0ULL;
    for (i = 0U; i < list->count; i++) {
        total_area += clks_wm_rect_area(&list->rects[i]);
    }

    screen_area = (u64)clks_wm_layers.width * (u64)clks_wm_layers.height;
    if (screen_area != 0ULL && (total_area * 100ULL) > (screen_area * CLKS_WM_DAMAGE_FULL_THRESHOLD_PERCENT)) {
        clks_wm_damage_force_full(list);
    }
}

static void clks_wm_mark_scene_dirty_rect(i32 l, i32 t, i32 r, i32 b) {
    struct clks_wm_rect rect;

    rect.l = l;
    rect.t = t;
    rect.r = r;
    rect.b = b;
    clks_wm_damage_add(&clks_wm_scene_damage, &rect);
    clks_wm_damage_add(&clks_wm_present_damage, &rect);
}

static void clks_wm_mark_present_dirty_rect(i32 l, i32 t, i32 r, i32 b) {
    struct clks_wm_rect rect;

    rect.l = l;
    rect.t = t;
    rect.r = r;
    rect.b = b;
    clks_wm_damage_add(&clks_wm_present_damage, &rect);
}

static void clks_wm_mark_dirty_window(const struct clks_wm_window *win) {
    if (win == CLKS_NULL || win->used == CLKS_FALSE || win->width == 0U || win->height == 0U) {
        return;
    }

    clks_wm_mark_scene_dirty_rect(win->x, win->y, win->x + (i32)win->width, win->y + (i32)win->height);
}

static void clks_wm_mark_dirty_cursor(i32 x, i32 y) {
    clks_wm_mark_present_dirty_rect(x - CLKS_WM_CURSOR_DAMAGE_RADIUS, y - CLKS_WM_CURSOR_DAMAGE_RADIUS,
                                    x + CLKS_WM_CURSOR_DAMAGE_RADIUS + 1, y + CLKS_WM_CURSOR_DAMAGE_RADIUS + 1);
}

static void clks_wm_mark_stats_dirty(void) {
#if CLKS_CFG_WM_STATS_OVERLAY == 0
    return;
#else
    struct clks_framebuffer_info fb = clks_fb_info();
    u32 cell_w = clks_fb_cell_width();
    u32 cell_h = clks_fb_cell_height();
    u32 box_w;
    u32 box_h;

    if (fb.width == 0U || fb.height == 0U || cell_w == 0U || cell_h == 0U) {
        return;
    }

    box_w = (CLKS_WM_STATS_TEXT_CELLS * cell_w) + 8U;
    box_h = cell_h + 6U;
    if (box_w + 4U > fb.width || box_h + 4U > fb.height) {
        return;
    }

    clks_wm_mark_present_dirty_rect((i32)(fb.width - box_w - 4U), (i32)(fb.height - box_h - 4U), (i32)(fb.width - 4U),
                                    (i32)(fb.height - 4U));
#endif
}

static void clks_wm_mark_dirty_full(void) {
    struct clks_framebuffer_info fb = clks_fb_info();

    clks_wm_mark_scene_dirty_rect(0, 0, (i32)fb.width, (i32)fb.height);
}

