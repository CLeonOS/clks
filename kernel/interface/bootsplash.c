#include <clks/bootsplash.h>
#include <clks/framebuffer.h>
#include <clks/string.h>
#include <clks/types.h>

#ifndef CLKS_CFG_BOOT_SPLASH
#define CLKS_CFG_BOOT_SPLASH 1
#endif

#define CLKS_BOOTSPLASH_BG 0x000B1220U
#define CLKS_BOOTSPLASH_PANEL 0x00111D33U
#define CLKS_BOOTSPLASH_PANEL_2 0x00182744U
#define CLKS_BOOTSPLASH_TEXT 0x00EAF2FFU
#define CLKS_BOOTSPLASH_DIM 0x008EA0BAU
#define CLKS_BOOTSPLASH_ACCENT 0x0038BDF8U
#define CLKS_BOOTSPLASH_ACCENT_2 0x0022C55EU
#define CLKS_BOOTSPLASH_BAR_BG 0x0024364FU
#define CLKS_BOOTSPLASH_BAR_EDGE 0x00475A74U
#define CLKS_BOOTSPLASH_TITLE_SCALE 2U
#define CLKS_BOOTSPLASH_TEXT_SCALE 1U
#define CLKS_BOOTSPLASH_DOT_COUNT 4U

typedef struct clks_bootsplash_state {
    clks_bool active;
    clks_bool ready;
    u32 last_percent;
    u32 phase;
    u32 width;
    u32 height;
    char label[64];
} clks_bootsplash_state;

static clks_bootsplash_state clks_bootsplash;

static u32 clks_bootsplash_min_u32(u32 a, u32 b) {
    return (a < b) ? a : b;
}

static u32 clks_bootsplash_text_width(const char *text, u32 scale) {
    u32 cell_w = clks_fb_cell_width();

    if (text == CLKS_NULL) {
        return 0U;
    }

    if (cell_w == 0U) {
        cell_w = 8U;
    }

    if (scale == 0U) {
        scale = 1U;
    }

    return (u32)clks_strlen(text) * cell_w * scale;
}

static void clks_bootsplash_draw_text(u32 x, u32 y, const char *text, u32 color, u32 bg, u32 scale) {
    u32 i = 0U;
    u32 cell_w = clks_fb_cell_width();

    if (text == CLKS_NULL) {
        return;
    }

    if (cell_w == 0U) {
        cell_w = 8U;
    }
    if (scale == 0U) {
        scale = 1U;
    }

    while (text[i] != '\0') {
        clks_fb_draw_char_scaled(x + (i * cell_w * scale), y, text[i], color, bg, 0U, scale);
        i++;
    }
}

static void clks_bootsplash_draw_centered(u32 y, const char *text, u32 color, u32 bg, u32 scale) {
    u32 text_w = clks_bootsplash_text_width(text, scale);
    u32 x = (clks_bootsplash.width > text_w) ? ((clks_bootsplash.width - text_w) / 2U) : 0U;
    clks_bootsplash_draw_text(x, y, text, color, bg, scale);
}

static void clks_bootsplash_draw_progress(u32 percent, const char *label) {
    u32 bar_margin;
    u32 bar_w;
    u32 bar_h;
    u32 bar_x;
    u32 bar_y;
    u32 fill_w;
    u32 dot_step;
    u32 i;
    char pct[5];
    u32 pct_value = (percent > 100U) ? 100U : percent;
    u32 panel_h;
    u32 label_y;

    if (clks_bootsplash.ready == CLKS_FALSE) {
        return;
    }

    bar_margin = clks_bootsplash.width / 8U;
    if (bar_margin < 48U) {
        bar_margin = 48U;
    }
    if (bar_margin * 2U >= clks_bootsplash.width) {
        bar_margin = 16U;
    }

    bar_w = clks_bootsplash.width - (bar_margin * 2U);
    bar_h = clks_bootsplash.height / 48U;
    if (bar_h < 10U) {
        bar_h = 10U;
    }
    if (bar_h > 18U) {
        bar_h = 18U;
    }

    panel_h = 96U;
    if (panel_h > clks_bootsplash.height / 3U) {
        panel_h = clks_bootsplash.height / 3U;
    }
    bar_x = bar_margin;
    bar_y = (clks_bootsplash.height > 72U) ? (clks_bootsplash.height - 56U) : (clks_bootsplash.height - bar_h - 8U);
    label_y = (bar_y > 28U) ? (bar_y - 28U) : 0U;

    clks_fb_fill_rect(0U, clks_bootsplash.height - panel_h, clks_bootsplash.width, panel_h, CLKS_BOOTSPLASH_BG);
    clks_fb_fill_rect(bar_x, bar_y, bar_w, bar_h, CLKS_BOOTSPLASH_BAR_EDGE);
    clks_fb_fill_rect(bar_x + 1U, bar_y + 1U, bar_w - 2U, bar_h - 2U, CLKS_BOOTSPLASH_BAR_BG);

    fill_w = ((bar_w - 4U) * pct_value) / 100U;
    if (fill_w > 0U) {
        clks_fb_fill_rect(bar_x + 2U, bar_y + 2U, fill_w, bar_h - 4U, CLKS_BOOTSPLASH_ACCENT);
        if (fill_w > 18U) {
            clks_fb_fill_rect(bar_x + 2U, bar_y + 2U, fill_w / 3U, bar_h - 4U, CLKS_BOOTSPLASH_ACCENT_2);
        }
    }

    dot_step = bar_w / (CLKS_BOOTSPLASH_DOT_COUNT + 1U);
    if (dot_step == 0U) {
        dot_step = 1U;
    }
    for (i = 0U; i < CLKS_BOOTSPLASH_DOT_COUNT; i++) {
        u32 dot_x = bar_x + dot_step * (i + 1U);
        u32 dot_y = bar_y + (bar_h / 2U);
        u32 color = ((i + clks_bootsplash.phase) % CLKS_BOOTSPLASH_DOT_COUNT == 0U) ? CLKS_BOOTSPLASH_TEXT
                                                                                    : CLKS_BOOTSPLASH_DIM;
        clks_fb_fill_rect(dot_x, dot_y, 5U, 5U, color);
    }

    pct[0] = (char)('0' + (pct_value / 100U));
    pct[1] = (char)('0' + ((pct_value / 10U) % 10U));
    pct[2] = (char)('0' + (pct_value % 10U));
    pct[3] = '%';
    pct[4] = '\0';

    if (label != CLKS_NULL && label[0] != '\0') {
        clks_bootsplash_draw_text(bar_x, label_y, label, CLKS_BOOTSPLASH_DIM, CLKS_BOOTSPLASH_BG,
                                  CLKS_BOOTSPLASH_TEXT_SCALE);
    }
    clks_bootsplash_draw_text(bar_x + bar_w - clks_bootsplash_text_width(pct, CLKS_BOOTSPLASH_TEXT_SCALE), label_y, pct,
                              CLKS_BOOTSPLASH_TEXT, CLKS_BOOTSPLASH_BG, CLKS_BOOTSPLASH_TEXT_SCALE);
}

static void clks_bootsplash_draw_base(void) {
    u32 logo_y;
    u32 panel_w;
    u32 panel_h;
    u32 panel_x;
    u32 panel_y;

    clks_fb_clear(CLKS_BOOTSPLASH_BG);

    panel_w = clks_bootsplash_min_u32(clks_bootsplash.width - 64U, 640U);
    panel_h = clks_bootsplash_min_u32(clks_bootsplash.height / 3U, 220U);
    if (panel_h < 140U) {
        panel_h = 140U;
    }
    panel_x = (clks_bootsplash.width > panel_w) ? ((clks_bootsplash.width - panel_w) / 2U) : 0U;
    panel_y = (clks_bootsplash.height > panel_h) ? ((clks_bootsplash.height - panel_h) / 2U) : 0U;

    clks_fb_fill_rect(panel_x, panel_y, panel_w, panel_h, CLKS_BOOTSPLASH_PANEL);
    clks_fb_fill_rect(panel_x, panel_y, panel_w, 4U, CLKS_BOOTSPLASH_ACCENT);
    clks_fb_fill_rect(panel_x, panel_y + panel_h - 4U, panel_w, 4U, CLKS_BOOTSPLASH_PANEL_2);
    clks_fb_fill_rect(panel_x, panel_y, 4U, panel_h, CLKS_BOOTSPLASH_PANEL_2);
    clks_fb_fill_rect(panel_x + panel_w - 4U, panel_y, 4U, panel_h, CLKS_BOOTSPLASH_PANEL_2);

    logo_y = panel_y + 44U;
    clks_bootsplash_draw_centered(logo_y, "CLKS Service", CLKS_BOOTSPLASH_TEXT, CLKS_BOOTSPLASH_PANEL,
                                  CLKS_BOOTSPLASH_TITLE_SCALE);
    clks_bootsplash_draw_centered(logo_y + 42U, "CLeonKernelSystem", CLKS_BOOTSPLASH_DIM, CLKS_BOOTSPLASH_PANEL,
                                  CLKS_BOOTSPLASH_TEXT_SCALE);
    clks_bootsplash_draw_centered(logo_y + 68U, "booting kernel services", CLKS_BOOTSPLASH_ACCENT,
                                  CLKS_BOOTSPLASH_PANEL, CLKS_BOOTSPLASH_TEXT_SCALE);
}

static void clks_bootsplash_copy_label(const char *label) {
    u32 i = 0U;

    if (label == CLKS_NULL) {
        clks_bootsplash.label[0] = '\0';
        return;
    }

    while (i + 1U < sizeof(clks_bootsplash.label) && label[i] != '\0') {
        clks_bootsplash.label[i] = label[i];
        i++;
    }

    clks_bootsplash.label[i] = '\0';
}

void clks_bootsplash_init(void) {
#if CLKS_CFG_BOOT_SPLASH
    struct clks_framebuffer_info info;

    clks_memset(&clks_bootsplash, 0, sizeof(clks_bootsplash));
    if (clks_fb_ready() == CLKS_FALSE) {
        return;
    }

    info = clks_fb_info();
    if (info.width < 320U || info.height < 200U || info.bpp != 32U) {
        return;
    }

    clks_bootsplash.ready = CLKS_TRUE;
    clks_bootsplash.active = CLKS_TRUE;
    clks_bootsplash.width = info.width;
    clks_bootsplash.height = info.height;
    clks_bootsplash.last_percent = 0U;
    clks_bootsplash.phase = 0U;
    clks_bootsplash_copy_label("starting");

    clks_bootsplash_draw_base();
    clks_bootsplash_draw_progress(0U, clks_bootsplash.label);
#else
    clks_memset(&clks_bootsplash, 0, sizeof(clks_bootsplash));
#endif
}

void clks_bootsplash_step(u32 percent, const char *label) {
#if CLKS_CFG_BOOT_SPLASH
    if (clks_bootsplash.active == CLKS_FALSE || clks_bootsplash.ready == CLKS_FALSE) {
        return;
    }

    if (percent < clks_bootsplash.last_percent) {
        percent = clks_bootsplash.last_percent;
    }
    if (percent > 100U) {
        percent = 100U;
    }

    clks_bootsplash.last_percent = percent;
    clks_bootsplash.phase++;
    clks_bootsplash_copy_label(label);
    clks_bootsplash_draw_base();
    clks_bootsplash_draw_progress(percent, clks_bootsplash.label);
#else
    (void)percent;
    (void)label;
#endif
}

void clks_bootsplash_finish(void) {
#if CLKS_CFG_BOOT_SPLASH
    if (clks_bootsplash.ready == CLKS_TRUE) {
        clks_bootsplash_draw_base();
        clks_bootsplash_draw_progress(100U, "ready");
    }
    clks_bootsplash.active = CLKS_FALSE;
#else
    clks_bootsplash.active = CLKS_FALSE;
#endif
}

clks_bool clks_bootsplash_active(void) {
#if CLKS_CFG_BOOT_SPLASH
    return clks_bootsplash.active;
#else
    return CLKS_FALSE;
#endif
}
