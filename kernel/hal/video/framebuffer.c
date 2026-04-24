#include <clks/framebuffer.h>
#include <clks/heap.h>
#include <clks/string.h>
#include <clks/types.h>

#include "psf_font.h"

struct clks_fb_state {
    volatile u8 *address;
    u8 *shadow;
    usize shadow_size;
    clks_bool shadow_ready;
    struct clks_framebuffer_info info;
    const struct clks_psf_font *font;
    struct clks_psf_font external_font;
    clks_bool external_font_active;
    u32 glyph_width;
    u32 glyph_height;
    clks_bool ready;
};

static struct clks_fb_state clks_fb = {
    .address = CLKS_NULL,
    .shadow = CLKS_NULL,
    .shadow_size = 0U,
    .shadow_ready = CLKS_FALSE,
    .info = {0, 0, 0, 0},
    .font = CLKS_NULL,
    .external_font = {0, 0, 0, 0, 0, CLKS_NULL},
    .external_font_active = CLKS_FALSE,
    .glyph_width = 8U,
    .glyph_height = 8U,
    .ready = CLKS_FALSE,
};

static void clks_fb_apply_font(const struct clks_psf_font *font) {
    clks_fb.font = font;
    clks_fb.glyph_width = 8U;
    clks_fb.glyph_height = 8U;

    if (font != CLKS_NULL) {
        if (font->width != 0U) {
            clks_fb.glyph_width = font->width;
        }

        if (font->height != 0U) {
            clks_fb.glyph_height = font->height;
        }
    }
}

static void clks_fb_put_pixel(u32 x, u32 y, u32 rgb) {
    u8 *row;
    u32 *pixel;

    if (clks_fb.ready == CLKS_FALSE) {
        return;
    }

    if (x >= clks_fb.info.width || y >= clks_fb.info.height) {
        return;
    }

    if (clks_fb.info.bpp != 32) {
        return;
    }

    if (clks_fb.shadow_ready == CLKS_TRUE) {
        u8 *shadow_row = clks_fb.shadow + ((usize)y * (usize)clks_fb.info.pitch);
        u32 *shadow_pixel = (u32 *)(shadow_row + ((usize)x * 4U));
        *shadow_pixel = rgb;
    }

    row = (u8 *)(void *)(clks_fb.address + ((usize)y * (usize)clks_fb.info.pitch));
    pixel = (u32 *)(void *)(row + ((usize)x * 4U));
    *pixel = rgb;
}

void clks_fb_init(const struct limine_framebuffer *fb) {
    u64 shadow_size64;

    if (fb == CLKS_NULL) {
        clks_fb.ready = CLKS_FALSE;
        return;
    }

    if (clks_fb.shadow != CLKS_NULL) {
        clks_kfree(clks_fb.shadow);
        clks_fb.shadow = CLKS_NULL;
        clks_fb.shadow_size = 0U;
        clks_fb.shadow_ready = CLKS_FALSE;
    }

    clks_fb.address = (volatile u8 *)fb->address;
    clks_fb.info.width = (u32)fb->width;
    clks_fb.info.height = (u32)fb->height;
    clks_fb.info.pitch = (u32)fb->pitch;
    clks_fb.info.bpp = fb->bpp;

    shadow_size64 = (u64)clks_fb.info.pitch * (u64)clks_fb.info.height;
    if (shadow_size64 > 0ULL && shadow_size64 <= (u64)(~(usize)0U)) {
        clks_fb.shadow = (u8 *)clks_kmalloc((usize)shadow_size64);
        if (clks_fb.shadow != CLKS_NULL) {
            clks_fb.shadow_size = (usize)shadow_size64;
            clks_fb.shadow_ready = CLKS_TRUE;
            clks_memset(clks_fb.shadow, 0, clks_fb.shadow_size);
        }
    }

    clks_fb.external_font_active = CLKS_FALSE;
    clks_fb_apply_font(clks_psf_default_font());

    clks_fb.ready = CLKS_TRUE;
}

clks_bool clks_fb_ready(void) {
    return clks_fb.ready;
}

struct clks_framebuffer_info clks_fb_info(void) {
    return clks_fb.info;
}

void clks_fb_draw_pixel(u32 x, u32 y, u32 rgb) {
    clks_fb_put_pixel(x, y, rgb);
}

clks_bool clks_fb_read_pixel(u32 x, u32 y, u32 *out_rgb) {
    u8 *row;
    u32 *pixel;

    if (out_rgb == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_fb.ready == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (x >= clks_fb.info.width || y >= clks_fb.info.height) {
        return CLKS_FALSE;
    }

    if (clks_fb.info.bpp != 32) {
        return CLKS_FALSE;
    }

    if (clks_fb.shadow_ready == CLKS_TRUE) {
        u8 *shadow_row = clks_fb.shadow + ((usize)y * (usize)clks_fb.info.pitch);
        u32 *shadow_pixel = (u32 *)(shadow_row + ((usize)x * 4U));
        *out_rgb = *shadow_pixel;
        return CLKS_TRUE;
    }

    row = (u8 *)(void *)(clks_fb.address + ((usize)y * (usize)clks_fb.info.pitch));
    pixel = (u32 *)(void *)(row + ((usize)x * 4U));
    *out_rgb = *pixel;
    return CLKS_TRUE;
}

void clks_fb_fill_rect(u32 x, u32 y, u32 width, u32 height, u32 rgb) {
    u32 py;
    u32 px;
    u32 end_x;
    u32 end_y;
    u64 end_x64;
    u64 end_y64;

    if (clks_fb.ready == CLKS_FALSE) {
        return;
    }

    if (width == 0U || height == 0U) {
        return;
    }

    if (x >= clks_fb.info.width || y >= clks_fb.info.height) {
        return;
    }

    end_x64 = (u64)x + (u64)width;
    end_y64 = (u64)y + (u64)height;

    end_x = (end_x64 > (u64)clks_fb.info.width) ? clks_fb.info.width : (u32)end_x64;
    end_y = (end_y64 > (u64)clks_fb.info.height) ? clks_fb.info.height : (u32)end_y64;

    if (clks_fb.shadow_ready == CLKS_TRUE) {
        for (py = y; py < end_y; py++) {
            u32 *row_ptr = (u32 *)(void *)(clks_fb.address + ((usize)py * (usize)clks_fb.info.pitch));
            u32 *shadow_row = (u32 *)(void *)(clks_fb.shadow + ((usize)py * (usize)clks_fb.info.pitch));

            for (px = x; px < end_x; px++) {
                shadow_row[px] = rgb;
                row_ptr[px] = rgb;
            }
        }
    } else {
        for (py = y; py < end_y; py++) {
            u32 *row_ptr = (u32 *)(void *)(clks_fb.address + ((usize)py * (usize)clks_fb.info.pitch));

            for (px = x; px < end_x; px++) {
                row_ptr[px] = rgb;
            }
        }
    }
}

void clks_fb_clear(u32 rgb) {
    clks_fb_fill_rect(0U, 0U, clks_fb.info.width, clks_fb.info.height, rgb);
}

void clks_fb_scroll_up(u32 pixel_rows, u32 fill_rgb) {
    usize row_bytes;
    u32 y;
    u32 move_rows;
    u32 x;

    if (clks_fb.ready == CLKS_FALSE) {
        return;
    }

    if (clks_fb.info.bpp != 32) {
        return;
    }

    if (pixel_rows == 0U) {
        return;
    }

    if (pixel_rows >= clks_fb.info.height) {
        clks_fb_clear(fill_rgb);
        return;
    }

    row_bytes = (usize)clks_fb.info.pitch;
    move_rows = clks_fb.info.height - pixel_rows;

    if (clks_fb.shadow_ready == CLKS_TRUE) {
        clks_memmove(clks_fb.shadow, clks_fb.shadow + ((usize)pixel_rows * row_bytes), (usize)move_rows * row_bytes);

        for (y = clks_fb.info.height - pixel_rows; y < clks_fb.info.height; y++) {
            u32 *shadow_row = (u32 *)(void *)(clks_fb.shadow + ((usize)y * row_bytes));

            for (x = 0U; x < clks_fb.info.width; x++) {
                shadow_row[x] = fill_rgb;
            }
        }

        for (y = 0U; y < move_rows; y++) {
            u32 *dst_row = (u32 *)(void *)(clks_fb.address + ((usize)y * row_bytes));
            u32 *src_shadow = (u32 *)(void *)(clks_fb.shadow + ((usize)y * row_bytes));

            for (x = 0U; x < clks_fb.info.width; x++) {
                dst_row[x] = src_shadow[x];
            }
        }

        for (y = clks_fb.info.height - pixel_rows; y < clks_fb.info.height; y++) {
            u32 *row_ptr = (u32 *)(void *)(clks_fb.address + ((usize)y * row_bytes));

            for (x = 0U; x < clks_fb.info.width; x++) {
                row_ptr[x] = fill_rgb;
            }
        }
        return;
    }

    for (y = 0U; y < move_rows; y++) {
        u32 *dst_row = (u32 *)(void *)(clks_fb.address + ((usize)y * row_bytes));
        u32 *src_row = (u32 *)(void *)(clks_fb.address + ((usize)(y + pixel_rows) * row_bytes));

        for (x = 0U; x < clks_fb.info.width; x++) {
            dst_row[x] = src_row[x];
        }
    }

    for (y = clks_fb.info.height - pixel_rows; y < clks_fb.info.height; y++) {
        u32 *row_ptr = (u32 *)(void *)(clks_fb.address + ((usize)y * row_bytes));

        for (x = 0U; x < clks_fb.info.width; x++) {
            row_ptr[x] = fill_rgb;
        }
    }
}

void clks_fb_draw_char_styled(u32 x, u32 y, char ch, u32 fg_rgb, u32 bg_rgb, u32 style_flags) {
    const u8 *glyph;
    u32 row;
    u32 col;
    u32 cols;
    u32 rows;
    u32 row_stride;
    u32 draw_cols;
    u32 draw_rows;
    clks_bool style_bold;
    clks_bool style_underline;
    u32 underline_row;

    if (clks_fb.ready == CLKS_FALSE || clks_fb.font == CLKS_NULL) {
        return;
    }

    if (clks_fb.info.bpp != 32) {
        return;
    }

    if (x >= clks_fb.info.width || y >= clks_fb.info.height) {
        return;
    }

    glyph = clks_psf_glyph(clks_fb.font, (u32)(u8)ch);

    cols = clks_fb.glyph_width;
    rows = clks_fb.glyph_height;

    if (cols == 0U) {
        cols = 8U;
    }

    if (rows == 0U) {
        rows = 8U;
    }

    row_stride = clks_fb.font->bytes_per_row;

    if (row_stride == 0U) {
        row_stride = (cols + 7U) / 8U;
    }

    if (row_stride == 0U) {
        return;
    }

    if (((usize)row_stride * (usize)rows) > (usize)clks_fb.font->bytes_per_glyph) {
        return;
    }

    draw_cols = cols;
    if (x + draw_cols > clks_fb.info.width) {
        draw_cols = clks_fb.info.width - x;
    }

    draw_rows = rows;
    if (y + draw_rows > clks_fb.info.height) {
        draw_rows = clks_fb.info.height - y;
    }

    style_bold = ((style_flags & CLKS_FB_STYLE_BOLD) != 0U) ? CLKS_TRUE : CLKS_FALSE;
    style_underline = ((style_flags & CLKS_FB_STYLE_UNDERLINE) != 0U) ? CLKS_TRUE : CLKS_FALSE;
    underline_row = (rows > 1U) ? (rows - 2U) : 0U;

    for (row = 0U; row < draw_rows; row++) {
        const u8 *row_bits = glyph + ((usize)row * (usize)row_stride);
        u32 *dst_row = (u32 *)(void *)(clks_fb.address + ((usize)(y + row) * (usize)clks_fb.info.pitch) + ((usize)x * 4U));
        u32 *shadow_row = CLKS_NULL;

        if (clks_fb.shadow_ready == CLKS_TRUE) {
            shadow_row =
                (u32 *)(void *)(clks_fb.shadow + ((usize)(y + row) * (usize)clks_fb.info.pitch) + ((usize)x * 4U));
        }

        for (col = 0U; col < draw_cols; col++) {
            u8 bits = row_bits[col >> 3U];
            u8 mask = (u8)(0x80U >> (col & 7U));
            clks_bool pixel_on = ((bits & mask) != 0U) ? CLKS_TRUE : CLKS_FALSE;
            u32 color;

            if (style_bold == CLKS_TRUE && pixel_on == CLKS_FALSE && col > 0U) {
                u32 left_col = col - 1U;
                u8 left_bits = row_bits[left_col >> 3U];
                u8 left_mask = (u8)(0x80U >> (left_col & 7U));

                if ((left_bits & left_mask) != 0U) {
                    pixel_on = CLKS_TRUE;
                }
            }

            if (style_underline == CLKS_TRUE && row == underline_row) {
                pixel_on = CLKS_TRUE;
            }

            color = (pixel_on == CLKS_TRUE) ? fg_rgb : bg_rgb;
            dst_row[col] = color;
            if (shadow_row != CLKS_NULL) {
                shadow_row[col] = color;
            }
        }
    }
}

void clks_fb_draw_char(u32 x, u32 y, char ch, u32 fg_rgb, u32 bg_rgb) {
    clks_fb_draw_char_styled(x, y, ch, fg_rgb, bg_rgb, 0U);
}

clks_bool clks_fb_load_psf_font(const void *blob, u64 blob_size) {
    struct clks_psf_font parsed = {0, 0, 0, 0, 0, CLKS_NULL};

    if (clks_psf_parse_font(blob, blob_size, &parsed) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_fb.external_font = parsed;
    clks_fb.external_font_active = CLKS_TRUE;
    clks_fb_apply_font(&clks_fb.external_font);
    return CLKS_TRUE;
}

u32 clks_fb_cell_width(void) {
    return clks_fb.glyph_width == 0U ? 8U : clks_fb.glyph_width;
}

u32 clks_fb_cell_height(void) {
    return clks_fb.glyph_height == 0U ? 8U : clks_fb.glyph_height;
}
