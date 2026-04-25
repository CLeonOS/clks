#include <clks/exec.h>
#include <clks/framebuffer.h>
#include <clks/heap.h>
#include <clks/interrupts.h>
#include <clks/keyboard.h>
#include <clks/log.h>
#include <clks/mouse.h>
#include <clks/string.h>
#include <clks/tty.h>
#include <clks/types.h>
#include <clks/wm.h>

#define CLKS_WM_TTY_INDEX 1U

#define CLKS_WM_MAX_WINDOWS 16U
#define CLKS_WM_EVENT_QUEUE_CAP 64U

#define CLKS_WM_MIN_WIDTH 64U
#define CLKS_WM_MIN_HEIGHT 48U
#define CLKS_WM_MAX_WIDTH 2048U
#define CLKS_WM_MAX_HEIGHT 2048U

#define CLKS_WM_BORDER 1U
#define CLKS_WM_TOPBAR_HEIGHT 24U
#define CLKS_WM_CURSOR_RADIUS 6
#define CLKS_WM_CURSOR_DAMAGE_RADIUS 9

#define CLKS_WM_MAX_DAMAGE_RECTS 48U
#define CLKS_WM_DAMAGE_MERGE_GAP 4
#define CLKS_WM_DAMAGE_FULL_THRESHOLD_PERCENT 78ULL

#define CLKS_WM_BG_COLOR 0x00141C2AUL
#define CLKS_WM_TOPBAR_COLOR 0x00253247UL
#define CLKS_WM_BORDER_ACTIVE 0x00FFD166UL
#define CLKS_WM_BORDER_INACTIVE 0x004A566EUL
#define CLKS_WM_STATS_BG_COLOR 0x00192435UL
#define CLKS_WM_STATS_FG_COLOR 0x00E6EDF7UL

#define CLKS_WM_TIMER_HZ_ESTIMATE 18ULL
#define CLKS_WM_STATS_SAMPLE_TICKS 18ULL
#define CLKS_WM_FRAME_CAP_FPS 120ULL
#define CLKS_WM_STATS_TEXT_CELLS 16U

struct clks_wm_event_queue {
    struct clks_wm_event events[CLKS_WM_EVENT_QUEUE_CAP];
    u32 head;
    u32 count;
};

struct clks_wm_window {
    clks_bool used;
    clks_bool focused;
    u64 id;
    u64 owner_pid;
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 *pixels;
    u64 pixel_count;
    struct clks_wm_event_queue queue;
};

struct clks_wm_rect {
    i32 l;
    i32 t;
    i32 r;
    i32 b;
};

struct clks_wm_damage_list {
    struct clks_wm_rect rects[CLKS_WM_MAX_DAMAGE_RECTS];
    u32 count;
};

struct clks_wm_layers {
    u32 *scene;
    u32 width;
    u32 height;
    u32 pitch_bytes;
    u64 pixel_count;
    clks_bool ready;
    clks_bool background_dirty;
};

struct clks_wm_timing {
    u64 timebase_tick;
    u64 timebase_tsc;
    u64 cycles_per_tick;
    clks_bool timebase_valid;
    clks_bool tsc_ready;
    u64 last_present_tick;
    u64 last_present_tsc;
    u64 stats_anchor_tick;
    u64 stats_anchor_tsc;
    clks_bool stats_anchor_valid;
    u32 stats_frames;
    u32 fps;
    u32 frame_ms;
};

static struct clks_wm_window clks_wm_windows[CLKS_WM_MAX_WINDOWS];
static u32 clks_wm_z_order[CLKS_WM_MAX_WINDOWS];
static u32 clks_wm_z_count = 0U;
static u64 clks_wm_next_window_id = 1ULL;

static clks_bool clks_wm_ready_flag = CLKS_FALSE;
static clks_bool clks_wm_active_last = CLKS_FALSE;
static i32 clks_wm_last_mouse_x = -1;
static i32 clks_wm_last_mouse_y = -1;
static u8 clks_wm_last_mouse_buttons = 0U;
static clks_bool clks_wm_last_mouse_ready = CLKS_FALSE;

static struct clks_wm_damage_list clks_wm_scene_damage;
static struct clks_wm_damage_list clks_wm_present_damage;
static struct clks_wm_layers clks_wm_layers;
static struct clks_wm_timing clks_wm_timing;

static clks_bool clks_wm_owner_allows(const struct clks_wm_window *win, u64 owner_pid) {
    if (win == CLKS_NULL || win->used == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (owner_pid == 0ULL) {
        return CLKS_TRUE;
    }

    return (win->owner_pid == owner_pid) ? CLKS_TRUE : CLKS_FALSE;
}

static i32 clks_wm_find_slot_by_id(u64 id) {
    u32 i;

    if (id == 0ULL) {
        return -1;
    }

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        if (clks_wm_windows[i].used == CLKS_TRUE && clks_wm_windows[i].id == id) {
            return (i32)i;
        }
    }

    return -1;
}

static i32 clks_wm_find_free_slot(void) {
    u32 i;

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        if (clks_wm_windows[i].used == CLKS_FALSE) {
            return (i32)i;
        }
    }

    return -1;
}

static void clks_wm_z_remove_slot(u32 slot) {
    u32 i;

    for (i = 0U; i < clks_wm_z_count; i++) {
        if (clks_wm_z_order[i] == slot) {
            u32 j;

            for (j = i; j + 1U < clks_wm_z_count; j++) {
                clks_wm_z_order[j] = clks_wm_z_order[j + 1U];
            }

            if (clks_wm_z_count > 0U) {
                clks_wm_z_count--;
            }

            return;
        }
    }
}

static void clks_wm_z_push_top(u32 slot) {
    if (clks_wm_z_count >= CLKS_WM_MAX_WINDOWS) {
        return;
    }

    clks_wm_z_order[clks_wm_z_count] = slot;
    clks_wm_z_count++;
}

static i32 clks_wm_focused_slot(void) {
    u32 i;

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        if (clks_wm_windows[i].used == CLKS_TRUE && clks_wm_windows[i].focused == CLKS_TRUE) {
            return (i32)i;
        }
    }

    return -1;
}

static void clks_wm_event_push(u32 slot, u64 type, u64 arg0, u64 arg1, u64 arg2, u64 arg3) {
    struct clks_wm_event_queue *queue;
    u32 write_index;

    if (slot >= CLKS_WM_MAX_WINDOWS || clks_wm_windows[slot].used == CLKS_FALSE) {
        return;
    }

    queue = &clks_wm_windows[slot].queue;
    if (queue->count >= CLKS_WM_EVENT_QUEUE_CAP) {
        queue->head = (queue->head + 1U) % CLKS_WM_EVENT_QUEUE_CAP;
        queue->count--;
    }

    write_index = (queue->head + queue->count) % CLKS_WM_EVENT_QUEUE_CAP;
    queue->events[write_index].type = type;
    queue->events[write_index].arg0 = arg0;
    queue->events[write_index].arg1 = arg1;
    queue->events[write_index].arg2 = arg2;
    queue->events[write_index].arg3 = arg3;
    queue->count++;
}

static clks_bool clks_wm_event_pop(u32 slot, struct clks_wm_event *out_event) {
    struct clks_wm_event_queue *queue;

    if (out_event == CLKS_NULL || slot >= CLKS_WM_MAX_WINDOWS || clks_wm_windows[slot].used == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    queue = &clks_wm_windows[slot].queue;
    if (queue->count == 0U) {
        return CLKS_FALSE;
    }

    *out_event = queue->events[queue->head];
    queue->head = (queue->head + 1U) % CLKS_WM_EVENT_QUEUE_CAP;
    queue->count--;
    return CLKS_TRUE;
}

static u64 clks_wm_read_tsc(void) {
#if defined(CLKS_ARCH_X86_64)
    u32 lo;
    u32 hi;

    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32U) | (u64)lo;
#else
    return 0ULL;
#endif
}

static u64 clks_wm_cycles_per_second(void) {
    if (clks_wm_timing.tsc_ready == CLKS_FALSE || clks_wm_timing.cycles_per_tick == 0ULL) {
        return 0ULL;
    }

    return clks_wm_timing.cycles_per_tick * CLKS_WM_TIMER_HZ_ESTIMATE;
}

static void clks_wm_timing_reset(void) {
    clks_memset(&clks_wm_timing, 0, sizeof(clks_wm_timing));
}

static void clks_wm_timing_update_timebase(u64 now_tick, u64 now_tsc) {
    u64 tick_delta;
    u64 tsc_delta;

    if (now_tsc == 0ULL) {
        return;
    }

    if (clks_wm_timing.timebase_valid == CLKS_FALSE) {
        clks_wm_timing.timebase_tick = now_tick;
        clks_wm_timing.timebase_tsc = now_tsc;
        clks_wm_timing.timebase_valid = CLKS_TRUE;
        return;
    }

    if (now_tick == clks_wm_timing.timebase_tick || now_tick < clks_wm_timing.timebase_tick ||
        now_tsc <= clks_wm_timing.timebase_tsc) {
        return;
    }

    tick_delta = now_tick - clks_wm_timing.timebase_tick;
    tsc_delta = now_tsc - clks_wm_timing.timebase_tsc;
    if (tick_delta == 0ULL) {
        return;
    }

    if (clks_wm_timing.cycles_per_tick == 0ULL) {
        clks_wm_timing.cycles_per_tick = tsc_delta / tick_delta;
    } else {
        u64 measured = tsc_delta / tick_delta;
        clks_wm_timing.cycles_per_tick = ((clks_wm_timing.cycles_per_tick * 7ULL) + measured) / 8ULL;
    }

    if (clks_wm_timing.cycles_per_tick != 0ULL) {
        clks_wm_timing.tsc_ready = CLKS_TRUE;
    }

    clks_wm_timing.timebase_tick = now_tick;
    clks_wm_timing.timebase_tsc = now_tsc;
}

static clks_bool clks_wm_frame_paced(u64 now_tick, u64 now_tsc) {
    u64 cycles_per_second;
    u64 min_cycles;
    u64 tick_interval;

    if (clks_wm_timing.last_present_tick == 0ULL && clks_wm_timing.last_present_tsc == 0ULL) {
        (void)now_tick;
        (void)now_tsc;
        return CLKS_TRUE;
    }

    cycles_per_second = clks_wm_cycles_per_second();
    if (cycles_per_second != 0ULL && now_tsc > clks_wm_timing.last_present_tsc) {
        min_cycles = cycles_per_second / CLKS_WM_FRAME_CAP_FPS;
        if (min_cycles == 0ULL) {
            return CLKS_TRUE;
        }

        return ((now_tsc - clks_wm_timing.last_present_tsc) >= min_cycles) ? CLKS_TRUE : CLKS_FALSE;
    }

    if (CLKS_WM_FRAME_CAP_FPS >= CLKS_WM_TIMER_HZ_ESTIMATE) {
        return CLKS_TRUE;
    }

    tick_interval = CLKS_WM_TIMER_HZ_ESTIMATE / CLKS_WM_FRAME_CAP_FPS;
    if (tick_interval == 0ULL) {
        tick_interval = 1ULL;
    }

    return (now_tick >= clks_wm_timing.last_present_tick + tick_interval) ? CLKS_TRUE : CLKS_FALSE;
}

static void clks_wm_stats_on_present(u64 now_tick, u64 now_tsc) {
    u64 cycles_per_second = clks_wm_cycles_per_second();

    if (cycles_per_second != 0ULL && clks_wm_timing.last_present_tsc != 0ULL &&
        now_tsc > clks_wm_timing.last_present_tsc) {
        u64 delta_cycles = now_tsc - clks_wm_timing.last_present_tsc;
        u64 ms = (delta_cycles * 1000ULL + (cycles_per_second / 2ULL)) / cycles_per_second;
        clks_wm_timing.frame_ms = (ms > 9999ULL) ? 9999U : (u32)ms;
    } else if (clks_wm_timing.last_present_tick != 0ULL && now_tick > clks_wm_timing.last_present_tick) {
        u64 delta_ticks = now_tick - clks_wm_timing.last_present_tick;
        u64 ms = (delta_ticks * 1000ULL + (CLKS_WM_TIMER_HZ_ESTIMATE / 2ULL)) / CLKS_WM_TIMER_HZ_ESTIMATE;
        clks_wm_timing.frame_ms = (ms > 9999ULL) ? 9999U : (u32)ms;
    }

    clks_wm_timing.stats_frames++;
    if (clks_wm_timing.stats_anchor_valid == CLKS_FALSE) {
        clks_wm_timing.stats_anchor_tick = now_tick;
        clks_wm_timing.stats_anchor_tsc = now_tsc;
        clks_wm_timing.stats_anchor_valid = CLKS_TRUE;
    } else if (cycles_per_second != 0ULL && now_tsc > clks_wm_timing.stats_anchor_tsc) {
        u64 elapsed_cycles = now_tsc - clks_wm_timing.stats_anchor_tsc;
        u64 sample_cycles = clks_wm_timing.cycles_per_tick * CLKS_WM_STATS_SAMPLE_TICKS;

        if (elapsed_cycles >= sample_cycles && sample_cycles != 0ULL) {
            u64 fps = ((u64)clks_wm_timing.stats_frames * cycles_per_second + (elapsed_cycles / 2ULL)) / elapsed_cycles;
            clks_wm_timing.fps = (fps > 9999ULL) ? 9999U : (u32)fps;
            clks_wm_timing.stats_frames = 0U;
            clks_wm_timing.stats_anchor_tick = now_tick;
            clks_wm_timing.stats_anchor_tsc = now_tsc;
        }
    } else if (now_tick >= clks_wm_timing.stats_anchor_tick + CLKS_WM_STATS_SAMPLE_TICKS) {
        u64 elapsed_ticks = now_tick - clks_wm_timing.stats_anchor_tick;

        if (elapsed_ticks != 0ULL) {
            u64 fps =
                ((u64)clks_wm_timing.stats_frames * CLKS_WM_TIMER_HZ_ESTIMATE + (elapsed_ticks / 2ULL)) / elapsed_ticks;
            clks_wm_timing.fps = (fps > 9999ULL) ? 9999U : (u32)fps;
        }

        clks_wm_timing.stats_frames = 0U;
        clks_wm_timing.stats_anchor_tick = now_tick;
        clks_wm_timing.stats_anchor_tsc = now_tsc;
    }

    clks_wm_timing.last_present_tick = now_tick;
    clks_wm_timing.last_present_tsc = now_tsc;
}

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
}

static void clks_wm_mark_dirty_full(void) {
    struct clks_framebuffer_info fb = clks_fb_info();

    clks_wm_mark_scene_dirty_rect(0, 0, (i32)fb.width, (i32)fb.height);
}

static void clks_wm_layer_free(void) {
    if (clks_wm_layers.scene != CLKS_NULL) {
        clks_kfree(clks_wm_layers.scene);
    }

    clks_memset(&clks_wm_layers, 0, sizeof(clks_wm_layers));
}

static clks_bool clks_wm_layer_ensure(void) {
    struct clks_framebuffer_info fb = clks_fb_info();
    u64 pixel_count;
    u64 bytes;

    if (fb.width == 0U || fb.height == 0U || fb.bpp != 32U) {
        return CLKS_FALSE;
    }

    if (clks_wm_layers.ready == CLKS_TRUE && clks_wm_layers.width == fb.width && clks_wm_layers.height == fb.height) {
        return CLKS_TRUE;
    }

    clks_wm_layer_free();

    pixel_count = (u64)fb.width * (u64)fb.height;
    bytes = pixel_count * 4ULL;
    if (pixel_count == 0ULL || bytes == 0ULL || bytes > (u64)(~(usize)0U)) {
        return CLKS_FALSE;
    }

    clks_wm_layers.scene = (u32 *)clks_kmalloc((usize)bytes);
    if (clks_wm_layers.scene == CLKS_NULL) {
        clks_wm_layer_free();
        return CLKS_FALSE;
    }

    clks_wm_layers.width = fb.width;
    clks_wm_layers.height = fb.height;
    clks_wm_layers.pitch_bytes = fb.width * 4U;
    clks_wm_layers.pixel_count = pixel_count;
    clks_wm_layers.ready = CLKS_TRUE;
    clks_wm_layers.background_dirty = CLKS_TRUE;
    return CLKS_TRUE;
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
        u32 x;

        for (x = 0U; x < width; x++) {
            row[x] = color;
        }
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

static void clks_wm_draw_window_to_scene_clipped(const struct clks_wm_window *win, const struct clks_wm_rect *clip) {
    struct clks_wm_rect win_rect;
    struct clks_wm_rect draw_rect;
    u32 src_x;
    u32 src_y;
    u32 copy_w;
    u32 copy_h;
    u32 row;
    usize row_bytes;
    u32 border_color;

    if (win == CLKS_NULL || clip == CLKS_NULL || win->used == CLKS_FALSE || win->pixels == CLKS_NULL ||
        clks_wm_layers.ready == CLKS_FALSE) {
        return;
    }

    if (win->width == 0U || win->height == 0U) {
        return;
    }

    win_rect.l = win->x;
    win_rect.t = win->y;
    win_rect.r = win->x + (i32)win->width;
    win_rect.b = win->y + (i32)win->height;

    if (clks_wm_rect_intersect(&win_rect, clip, &draw_rect) == CLKS_FALSE) {
        return;
    }

    src_x = (u32)(draw_rect.l - win->x);
    src_y = (u32)(draw_rect.t - win->y);
    copy_w = (u32)(draw_rect.r - draw_rect.l);
    copy_h = (u32)(draw_rect.b - draw_rect.t);
    row_bytes = (usize)copy_w * 4U;

    for (row = 0U; row < copy_h; row++) {
        u64 src_offset = ((u64)(src_y + row) * (u64)win->width) + (u64)src_x;
        u64 dst_offset = ((u64)(u32)(draw_rect.t + (i32)row) * (u64)clks_wm_layers.width) + (u64)(u32)draw_rect.l;

        if (src_offset + (u64)copy_w > win->pixel_count || dst_offset + (u64)copy_w > clks_wm_layers.pixel_count) {
            return;
        }

        clks_memcpy(clks_wm_layers.scene + dst_offset, win->pixels + src_offset, row_bytes);
    }

    border_color = (win->focused == CLKS_TRUE) ? (u32)CLKS_WM_BORDER_ACTIVE : (u32)CLKS_WM_BORDER_INACTIVE;
    clks_wm_layer_fill_box_clipped(clks_wm_layers.scene, win->x, win->y, (i32)win->width, (i32)CLKS_WM_BORDER,
                                   border_color, clip);
    clks_wm_layer_fill_box_clipped(clks_wm_layers.scene, win->x, win->y + (i32)win->height - (i32)CLKS_WM_BORDER,
                                   (i32)win->width, (i32)CLKS_WM_BORDER, border_color, clip);
    clks_wm_layer_fill_box_clipped(clks_wm_layers.scene, win->x, win->y, (i32)CLKS_WM_BORDER, (i32)win->height,
                                   border_color, clip);
    clks_wm_layer_fill_box_clipped(clks_wm_layers.scene, win->x + (i32)win->width - (i32)CLKS_WM_BORDER, win->y,
                                   (i32)CLKS_WM_BORDER, (i32)win->height, border_color, clip);
}

static void clks_wm_update_scene_layer(void) {
    u32 dirty_index;

    if (clks_wm_scene_damage.count == 0U || clks_wm_layers.ready == CLKS_FALSE) {
        return;
    }

    for (dirty_index = 0U; dirty_index < clks_wm_scene_damage.count; dirty_index++) {
        u32 zi;
        struct clks_wm_rect dirty = clks_wm_scene_damage.rects[dirty_index];

        clks_wm_copy_background_to_scene(&dirty);

        for (zi = 0U; zi < clks_wm_z_count; zi++) {
            u32 slot = clks_wm_z_order[zi];
            clks_wm_draw_window_to_scene_clipped(&clks_wm_windows[slot], &dirty);
        }
    }

    clks_wm_scene_damage.count = 0U;
}

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

static void clks_wm_draw_stats_overlay_clipped(const struct clks_wm_rect *clip) {
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
}

static clks_bool clks_wm_cursor_rect(i32 x, i32 y, struct clks_wm_rect *out_rect) {
    return clks_wm_rect_clip_to_screen(x - CLKS_WM_CURSOR_DAMAGE_RADIUS, y - CLKS_WM_CURSOR_DAMAGE_RADIUS,
                                       x + CLKS_WM_CURSOR_DAMAGE_RADIUS + 1, y + CLKS_WM_CURSOR_DAMAGE_RADIUS + 1,
                                       out_rect);
}

static void clks_wm_draw_cursor_clipped(i32 x, i32 y, u8 buttons, const struct clks_wm_rect *clip) {
    u32 color = ((buttons & CLKS_MOUSE_BTN_LEFT) != 0U) ? 0x00FFD166UL : 0x00FFFFFFUL;
    i32 dx;

    if (clip == CLKS_NULL) {
        return;
    }

    for (dx = -CLKS_WM_CURSOR_RADIUS; dx <= CLKS_WM_CURSOR_RADIUS; dx++) {
        i32 px = x + dx;
        i32 py = y;

        if (px >= clip->l && px < clip->r && py >= clip->t && py < clip->b) {
            clks_fb_draw_pixel((u32)px, (u32)py, color);
        }
    }

    for (dx = -CLKS_WM_CURSOR_RADIUS; dx <= CLKS_WM_CURSOR_RADIUS; dx++) {
        i32 px = x;
        i32 py = y + dx;

        if (px >= clip->l && px < clip->r && py >= clip->t && py < clip->b) {
            clks_fb_draw_pixel((u32)px, (u32)py, color);
        }
    }
}

static void clks_wm_present_rect(const struct clks_wm_rect *rect) {
    struct clks_mouse_state mouse = {0, 0, 0U, 0ULL, CLKS_FALSE};
    struct clks_wm_rect cursor;
    struct clks_wm_rect cursor_clip;
    const u32 *src;
    u32 width;
    u32 height;

    if (rect == CLKS_NULL || clks_wm_layers.ready == CLKS_FALSE || rect->r <= rect->l || rect->b <= rect->t) {
        return;
    }

    width = (u32)(rect->r - rect->l);
    height = (u32)(rect->b - rect->t);
    src = clks_wm_layers.scene + ((u64)(u32)rect->t * (u64)clks_wm_layers.width) + (u32)rect->l;
    clks_fb_blit_rgba(rect->l, rect->t, (const void *)src, width, height, clks_wm_layers.pitch_bytes);

    clks_wm_draw_stats_overlay_clipped(rect);

    clks_mouse_snapshot(&mouse);
    if (mouse.ready == CLKS_TRUE && clks_wm_cursor_rect(mouse.x, mouse.y, &cursor) == CLKS_TRUE &&
        clks_wm_rect_intersect(rect, &cursor, &cursor_clip) == CLKS_TRUE) {
        clks_wm_draw_cursor_clipped(mouse.x, mouse.y, mouse.buttons, &cursor_clip);
    }
}

static void clks_wm_flush_present_damage(u64 now_tick, u64 now_tsc) {
    u32 i;

    if (clks_wm_present_damage.count == 0U) {
        return;
    }

    clks_wm_update_scene_layer();

    for (i = 0U; i < clks_wm_present_damage.count; i++) {
        clks_wm_present_rect(&clks_wm_present_damage.rects[i]);
    }

    clks_wm_present_damage.count = 0U;
    clks_wm_stats_on_present(now_tick, now_tsc);
    clks_wm_mark_stats_dirty();
}

static void clks_wm_set_focus_slot(i32 new_focus_slot) {
    i32 old_focus_slot = clks_wm_focused_slot();

    if (old_focus_slot == new_focus_slot) {
        return;
    }

    if (old_focus_slot >= 0) {
        u32 old_slot = (u32)old_focus_slot;
        clks_wm_mark_dirty_window(&clks_wm_windows[old_slot]);
        clks_wm_windows[old_slot].focused = CLKS_FALSE;
        clks_wm_event_push(old_slot, CLKS_WM_EVENT_FOCUS_LOST, 0ULL, 0ULL, 0ULL, 0ULL);
    }

    if (new_focus_slot >= 0) {
        u32 new_slot = (u32)new_focus_slot;
        clks_wm_mark_dirty_window(&clks_wm_windows[new_slot]);
        clks_wm_windows[new_slot].focused = CLKS_TRUE;
        clks_wm_z_remove_slot(new_slot);
        clks_wm_z_push_top(new_slot);
        clks_wm_mark_dirty_window(&clks_wm_windows[new_slot]);
        clks_wm_event_push(new_slot, CLKS_WM_EVENT_FOCUS_GAINED, 1ULL, 0ULL, 0ULL, 0ULL);
    }
}

static i32 clks_wm_window_hit_test(i32 x, i32 y) {
    i32 zi;

    for (zi = (i32)clks_wm_z_count - 1; zi >= 0; zi--) {
        u32 slot = clks_wm_z_order[(u32)zi];
        const struct clks_wm_window *win = &clks_wm_windows[slot];
        i32 left;
        i32 top;
        i32 right;
        i32 bottom;

        if (win->used == CLKS_FALSE) {
            continue;
        }

        left = win->x;
        top = win->y;
        right = win->x + (i32)win->width;
        bottom = win->y + (i32)win->height;

        if (x >= left && x < right && y >= top && y < bottom) {
            return (i32)slot;
        }
    }

    return -1;
}

static void clks_wm_remove_slot(u32 slot) {
    clks_bool was_focused;

    if (slot >= CLKS_WM_MAX_WINDOWS || clks_wm_windows[slot].used == CLKS_FALSE) {
        return;
    }

    was_focused = clks_wm_windows[slot].focused;
    clks_wm_mark_dirty_window(&clks_wm_windows[slot]);

    if (clks_wm_windows[slot].pixels != CLKS_NULL) {
        clks_kfree(clks_wm_windows[slot].pixels);
    }

    clks_memset(&clks_wm_windows[slot], 0, sizeof(clks_wm_windows[slot]));
    clks_wm_z_remove_slot(slot);

    if (was_focused == CLKS_TRUE) {
        if (clks_wm_z_count > 0U) {
            clks_wm_set_focus_slot((i32)clks_wm_z_order[clks_wm_z_count - 1U]);
        } else {
            clks_wm_set_focus_slot(-1);
        }
    }

    if (clks_wm_z_count == 0U) {
        clks_wm_layer_free();
        clks_wm_scene_damage.count = 0U;
        clks_wm_present_damage.count = 0U;
    }
}

static void clks_wm_reap_dead_owners(void) {
    u32 i;

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        struct clks_exec_proc_snapshot snap;

        if (clks_wm_windows[i].used == CLKS_FALSE || clks_wm_windows[i].owner_pid == 0ULL) {
            continue;
        }

        if (clks_exec_proc_snapshot(clks_wm_windows[i].owner_pid, &snap) == CLKS_FALSE) {
            clks_wm_remove_slot(i);
            continue;
        }

        if (snap.state == CLKS_EXEC_PROC_STATE_UNUSED || snap.state == CLKS_EXEC_PROC_STATE_EXITED) {
            clks_wm_remove_slot(i);
        }
    }
}

static void clks_wm_dispatch_keyboard_to_focused(void) {
    i32 focused_slot = clks_wm_focused_slot();
    u32 budget = 64U;

    if (focused_slot < 0) {
        return;
    }

    while (budget > 0U) {
        char ch = '\0';

        if (clks_keyboard_pop_char_for_tty(CLKS_WM_TTY_INDEX, &ch) == CLKS_FALSE) {
            break;
        }

        clks_wm_event_push((u32)focused_slot, CLKS_WM_EVENT_KEY, (u64)(u8)ch, 0ULL, 0ULL, 0ULL);
        budget--;
    }
}

static void clks_wm_dispatch_mouse(void) {
    struct clks_mouse_state mouse = {0, 0, 0U, 0ULL, CLKS_FALSE};
    i32 focused_slot;

    clks_mouse_snapshot(&mouse);
    focused_slot = clks_wm_focused_slot();

    if (mouse.ready == CLKS_TRUE && focused_slot >= 0) {
        const struct clks_wm_window *focus_win = &clks_wm_windows[(u32)focused_slot];
        i64 local_x = (i64)mouse.x - (i64)focus_win->x;
        i64 local_y = (i64)mouse.y - (i64)focus_win->y;

        if (mouse.x != clks_wm_last_mouse_x || mouse.y != clks_wm_last_mouse_y) {
            clks_wm_event_push((u32)focused_slot, CLKS_WM_EVENT_MOUSE_MOVE, (u64)(i64)mouse.x, (u64)(i64)mouse.y,
                               (u64)local_x, (u64)local_y);
        }

        if (mouse.buttons != clks_wm_last_mouse_buttons) {
            u64 changed = (u64)(mouse.buttons ^ clks_wm_last_mouse_buttons);
            clks_wm_event_push((u32)focused_slot, CLKS_WM_EVENT_MOUSE_BUTTON, (u64)mouse.buttons, changed,
                               (u64)local_x, (u64)local_y);
        }
    }

    if (mouse.ready == CLKS_TRUE && (mouse.buttons & CLKS_MOUSE_BTN_LEFT) != 0U &&
        (clks_wm_last_mouse_buttons & CLKS_MOUSE_BTN_LEFT) == 0U) {
        i32 hit_slot = clks_wm_window_hit_test(mouse.x, mouse.y);
        if (hit_slot >= 0) {
            clks_wm_set_focus_slot(hit_slot);
        }
    }

    if (clks_wm_last_mouse_ready == CLKS_TRUE &&
        (mouse.ready != clks_wm_last_mouse_ready || mouse.x != clks_wm_last_mouse_x || mouse.y != clks_wm_last_mouse_y ||
         mouse.buttons != clks_wm_last_mouse_buttons)) {
        clks_wm_mark_dirty_cursor(clks_wm_last_mouse_x, clks_wm_last_mouse_y);
    }

    if (mouse.ready == CLKS_TRUE &&
        (mouse.ready != clks_wm_last_mouse_ready || mouse.x != clks_wm_last_mouse_x || mouse.y != clks_wm_last_mouse_y ||
         mouse.buttons != clks_wm_last_mouse_buttons)) {
        clks_wm_mark_dirty_cursor(mouse.x, mouse.y);
    }

    clks_wm_last_mouse_ready = mouse.ready;
    clks_wm_last_mouse_x = mouse.x;
    clks_wm_last_mouse_y = mouse.y;
    clks_wm_last_mouse_buttons = mouse.buttons;
}

static void clks_wm_pump_foreground(void) {
    u64 now_tick;
    u64 now_tsc;

    if (clks_wm_ready_flag == CLKS_FALSE) {
        return;
    }

    if (clks_tty_active() != CLKS_WM_TTY_INDEX) {
        clks_wm_active_last = CLKS_FALSE;
        clks_wm_scene_damage.count = 0U;
        clks_wm_present_damage.count = 0U;
        clks_wm_timing.stats_anchor_valid = CLKS_FALSE;
        return;
    }

    if (clks_wm_layer_ensure() == CLKS_FALSE) {
        return;
    }

    if (clks_wm_active_last == CLKS_FALSE) {
        clks_wm_layers.background_dirty = CLKS_TRUE;
        clks_wm_mark_dirty_full();
        clks_wm_mark_stats_dirty();
    }

    clks_wm_dispatch_mouse();
    clks_wm_dispatch_keyboard_to_focused();

    now_tick = clks_interrupts_timer_ticks();
    now_tsc = clks_wm_read_tsc();
    clks_wm_timing_update_timebase(now_tick, now_tsc);

    if (clks_wm_present_damage.count != 0U && clks_wm_frame_paced(now_tick, now_tsc) == CLKS_TRUE) {
        clks_wm_flush_present_damage(now_tick, now_tsc);
    }

    clks_wm_active_last = CLKS_TRUE;
}

void clks_wm_init(void) {
    u32 i;

    if (clks_fb_ready() == CLKS_FALSE) {
        clks_wm_ready_flag = CLKS_FALSE;
        clks_log(CLKS_LOG_WARN, "WM", "FRAMEBUFFER NOT READY");
        return;
    }

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        clks_memset(&clks_wm_windows[i], 0, sizeof(clks_wm_windows[i]));
    }

    clks_wm_z_count = 0U;
    clks_wm_next_window_id = 1ULL;
    clks_wm_active_last = CLKS_FALSE;
    clks_wm_last_mouse_x = -1;
    clks_wm_last_mouse_y = -1;
    clks_wm_last_mouse_buttons = 0U;
    clks_wm_last_mouse_ready = CLKS_FALSE;
    clks_wm_scene_damage.count = 0U;
    clks_wm_present_damage.count = 0U;
    clks_wm_timing_reset();

    clks_wm_ready_flag = CLKS_TRUE;
    clks_log(CLKS_LOG_INFO, "WM", "KERNEL WINDOW FRAMEWORK ONLINE");
    clks_log(CLKS_LOG_INFO, "WM", "MULTI-RECT COMPOSITOR ONLINE");
}

void clks_wm_tick(u64 tick) {
    (void)tick;

    if (clks_wm_ready_flag == CLKS_FALSE) {
        return;
    }

    clks_wm_reap_dead_owners();
    clks_wm_pump_foreground();
}

clks_bool clks_wm_ready(void) {
    return clks_wm_ready_flag;
}

u64 clks_wm_create(u64 owner_pid, i32 x, i32 y, u32 width, u32 height, u64 flags) {
    struct clks_framebuffer_info fb_info;
    i32 slot;
    i32 max_x;
    i32 max_y;
    u64 pixel_count;
    u32 *pixels;

    (void)flags;

    if (clks_wm_ready_flag == CLKS_FALSE) {
        return 0ULL;
    }

    if (width < CLKS_WM_MIN_WIDTH || height < CLKS_WM_MIN_HEIGHT || width > CLKS_WM_MAX_WIDTH ||
        height > CLKS_WM_MAX_HEIGHT) {
        return 0ULL;
    }

    fb_info = clks_fb_info();
    if (width > fb_info.width || height > fb_info.height) {
        return 0ULL;
    }

    max_x = (i32)fb_info.width - (i32)width;
    max_y = (i32)fb_info.height - (i32)height;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    if (x < 0) {
        x = 0;
    }
    if (y < (i32)CLKS_WM_TOPBAR_HEIGHT) {
        y = (i32)CLKS_WM_TOPBAR_HEIGHT;
    }
    if (x > max_x) {
        x = max_x;
    }
    if (y > max_y) {
        y = max_y;
    }

    slot = clks_wm_find_free_slot();
    if (slot < 0) {
        return 0ULL;
    }

    pixel_count = (u64)width * (u64)height;
    pixels = (u32 *)clks_kmalloc((usize)(pixel_count * 4ULL));
    if (pixels == CLKS_NULL) {
        return 0ULL;
    }

    clks_memset(pixels, 0, (usize)(pixel_count * 4ULL));

    clks_wm_windows[(u32)slot].used = CLKS_TRUE;
    clks_wm_windows[(u32)slot].focused = CLKS_FALSE;
    clks_wm_windows[(u32)slot].id = clks_wm_next_window_id++;
    clks_wm_windows[(u32)slot].owner_pid = owner_pid;
    clks_wm_windows[(u32)slot].x = x;
    clks_wm_windows[(u32)slot].y = y;
    clks_wm_windows[(u32)slot].width = width;
    clks_wm_windows[(u32)slot].height = height;
    clks_wm_windows[(u32)slot].pixels = pixels;
    clks_wm_windows[(u32)slot].pixel_count = pixel_count;
    clks_wm_windows[(u32)slot].queue.head = 0U;
    clks_wm_windows[(u32)slot].queue.count = 0U;

    clks_wm_z_push_top((u32)slot);
    clks_wm_set_focus_slot(slot);
    clks_wm_mark_dirty_window(&clks_wm_windows[(u32)slot]);

    return clks_wm_windows[(u32)slot].id;
}

clks_bool clks_wm_destroy(u64 owner_pid, u64 window_id) {
    i32 slot = clks_wm_find_slot_by_id(window_id);

    if (slot < 0) {
        return CLKS_FALSE;
    }

    if (clks_wm_owner_allows(&clks_wm_windows[(u32)slot], owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_wm_remove_slot((u32)slot);
    return CLKS_TRUE;
}

clks_bool clks_wm_present(u64 owner_pid, u64 window_id, const void *pixels, u32 src_width, u32 src_height,
                          u32 src_pitch_bytes) {
    i32 slot = clks_wm_find_slot_by_id(window_id);
    struct clks_wm_window *win;
    u32 y;
    const u8 *src_bytes;

    if (slot < 0 || pixels == CLKS_NULL) {
        return CLKS_FALSE;
    }

    win = &clks_wm_windows[(u32)slot];
    if (clks_wm_owner_allows(win, owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (src_width != win->width || src_height != win->height || src_pitch_bytes < (win->width * 4U)) {
        return CLKS_FALSE;
    }

    src_bytes = (const u8 *)pixels;
    if (src_pitch_bytes == (win->width * 4U)) {
        clks_memcpy(win->pixels, src_bytes, (usize)(win->pixel_count * 4ULL));
    } else {
        for (y = 0U; y < win->height; y++) {
            const u32 *src_row = (const u32 *)(const void *)(src_bytes + ((usize)y * (usize)src_pitch_bytes));
            u32 *dst_row = win->pixels + ((u64)y * (u64)win->width);
            clks_memcpy(dst_row, src_row, (usize)(win->width * 4U));
        }
    }

    clks_wm_mark_dirty_window(win);
    return CLKS_TRUE;
}

clks_bool clks_wm_poll_event(u64 owner_pid, u64 window_id, struct clks_wm_event *out_event) {
    i32 slot = clks_wm_find_slot_by_id(window_id);

    if (slot < 0 || out_event == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_wm_owner_allows(&clks_wm_windows[(u32)slot], owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_wm_pump_foreground();
    return clks_wm_event_pop((u32)slot, out_event);
}

clks_bool clks_wm_move(u64 owner_pid, u64 window_id, i32 x, i32 y) {
    i32 slot = clks_wm_find_slot_by_id(window_id);
    struct clks_wm_window *win;
    struct clks_framebuffer_info fb_info;
    i32 max_x;
    i32 max_y;

    if (slot < 0) {
        return CLKS_FALSE;
    }

    win = &clks_wm_windows[(u32)slot];
    if (clks_wm_owner_allows(win, owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    fb_info = clks_fb_info();
    max_x = (i32)fb_info.width - (i32)win->width;
    max_y = (i32)fb_info.height - (i32)win->height;

    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    if (x < 0) {
        x = 0;
    }
    if (y < (i32)CLKS_WM_TOPBAR_HEIGHT) {
        y = (i32)CLKS_WM_TOPBAR_HEIGHT;
    }
    if (x > max_x) {
        x = max_x;
    }
    if (y > max_y) {
        y = max_y;
    }

    if (win->x != x || win->y != y) {
        i32 old_x = win->x;
        i32 old_y = win->y;
        clks_wm_mark_scene_dirty_rect(old_x, old_y, old_x + (i32)win->width, old_y + (i32)win->height);
        win->x = x;
        win->y = y;
        clks_wm_mark_dirty_window(win);
    }

    return CLKS_TRUE;
}

clks_bool clks_wm_set_focus(u64 owner_pid, u64 window_id) {
    i32 slot = clks_wm_find_slot_by_id(window_id);

    if (slot < 0) {
        return CLKS_FALSE;
    }

    if (clks_wm_owner_allows(&clks_wm_windows[(u32)slot], owner_pid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_wm_set_focus_slot(slot);
    return CLKS_TRUE;
}
