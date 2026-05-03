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
#define CLKS_WM_MAX_WIDTH 4096U
#define CLKS_WM_MAX_HEIGHT 4096U

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

#define CLKS_WM_FRAME_CAP_FPS 120ULL
#define CLKS_WM_STATS_TEXT_CELLS 16U

#ifndef CLKS_CFG_WM_MULTI_RECT_DAMAGE
#define CLKS_CFG_WM_MULTI_RECT_DAMAGE 1
#endif

#ifndef CLKS_CFG_WM_LAYER_CACHE
#define CLKS_CFG_WM_LAYER_CACHE 1
#endif

#ifndef CLKS_CFG_WM_FRAME_PACING
#define CLKS_CFG_WM_FRAME_PACING 1
#endif

#ifndef CLKS_CFG_WM_STATS_OVERLAY
#define CLKS_CFG_WM_STATS_OVERLAY 1
#endif

#ifndef CLKS_CFG_WM_INPUT_DISPATCH
#define CLKS_CFG_WM_INPUT_DISPATCH 1
#endif

#ifndef CLKS_CFG_WM_REAP_DEAD_OWNERS
#define CLKS_CFG_WM_REAP_DEAD_OWNERS 1
#endif

struct clks_wm_event_queue {
    struct clks_wm_event events[CLKS_WM_EVENT_QUEUE_CAP];
    u32 head;
    u32 count;
};

struct clks_wm_window {
    clks_bool used;
    clks_bool presented;
    clks_bool focused;
    u64 id;
    u64 owner_pid;
    u64 flags;
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

static void clks_wm_reap_dead_owners(void);

#include "wm/window_list.inc"
#include "wm/timing.inc"
#include "wm/rect_damage.inc"
#include "wm/layers_background.inc"
#include "wm/window_draw.inc"
#include "wm/stats.inc"
#include "wm/cursor_present.inc"
#include "wm/focus_lifecycle.inc"
#include "wm/input_dispatch.inc"
#include "wm/init_query.inc"
#include "wm/create_present.inc"
#include "wm/public_api.inc"
