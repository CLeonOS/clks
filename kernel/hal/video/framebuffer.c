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

static void clks_fb_copy_forward_bytes(void *dst, const void *src, usize bytes) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;

    if (dst == CLKS_NULL || src == CLKS_NULL || bytes == 0U || dst == src) {
        return;
    }

#if defined(CLKS_ARCH_X86_64)
    {
        usize n = bytes;
        __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    }
    return;
#else
    while (bytes > 0U && ((((usize)d) | ((usize)s)) & (sizeof(usize) - 1U)) != 0U) {
        *d++ = *s++;
        bytes--;
    }

    while (bytes >= sizeof(usize)) {
        *(usize *)(void *)d = *(const usize *)(const void *)s;
        d += sizeof(usize);
        s += sizeof(usize);
        bytes -= sizeof(usize);
    }

    while (bytes > 0U) {
        *d++ = *s++;
        bytes--;
    }
#endif
}

static void clks_fb_fill_rows_color32(u8 *base, usize row_bytes, u32 row_count, u32 fill_rgb) {
    u32 row;
    usize qwords_per_row;
    usize rem_bytes;
    u64 fill_qword;
    u8 fill_bytes[4];

    if (base == CLKS_NULL || row_bytes == 0U || row_count == 0U) {
        return;
    }

    qwords_per_row = row_bytes / 8U;
    rem_bytes = row_bytes % 8U;
    fill_qword = ((u64)fill_rgb << 32U) | (u64)fill_rgb;
    fill_bytes[0] = (u8)(fill_rgb & 0xFFU);
    fill_bytes[1] = (u8)((fill_rgb >> 8U) & 0xFFU);
    fill_bytes[2] = (u8)((fill_rgb >> 16U) & 0xFFU);
    fill_bytes[3] = (u8)((fill_rgb >> 24U) & 0xFFU);

    for (row = 0U; row < row_count; row++) {
        u8 *row_ptr = base + ((usize)row * row_bytes);
        usize i = 0U;
#if defined(CLKS_ARCH_X86_64)
        if (qwords_per_row > 0U) {
            u64 *dst_q = (u64 *)(void *)row_ptr;
            usize n = qwords_per_row;
            __asm__ volatile("rep stosq" : "+D"(dst_q), "+c"(n) : "a"(fill_qword) : "memory");
            row_ptr = (u8 *)(void *)dst_q;
            i = 0U;
        }
#else
        for (i = 0U; i < qwords_per_row; i++) {
            ((u64 *)(void *)row_ptr)[i] = fill_qword;
        }
        row_ptr += qwords_per_row * 8U;
#endif

        if (rem_bytes > 0U) {
            usize rem = rem_bytes;
            if (rem >= 4U) {
                ((u32 *)(void *)row_ptr)[0] = fill_rgb;
                row_ptr += 4U;
                rem -= 4U;
            }

            for (i = 0U; i < rem; i++) {
                row_ptr[i] = fill_bytes[i];
            }
        }
    }
}

static void clks_fb_fill_span_color32(u32 *dst, u32 pixel_count, u32 fill_rgb) {
    if (dst == CLKS_NULL || pixel_count == 0U) {
        return;
    }

#if defined(CLKS_ARCH_X86_64)
    {
        usize n = (usize)pixel_count;
        __asm__ volatile("rep stosl" : "+D"(dst), "+c"(n) : "a"(fill_rgb) : "memory");
    }
#else
    {
        u32 i;
        for (i = 0U; i < pixel_count; i++) {
            dst[i] = fill_rgb;
        }
    }
#endif
}

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
    u32 end_x;
    u32 end_y;
    u32 span_pixels;
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

    span_pixels = end_x - x;

    if (clks_fb.shadow_ready == CLKS_TRUE) {
        for (py = y; py < end_y; py++) {
            u32 *row_ptr = (u32 *)(void *)(clks_fb.address + ((usize)py * (usize)clks_fb.info.pitch) + ((usize)x * 4U));
            u32 *shadow_row =
                (u32 *)(void *)(clks_fb.shadow + ((usize)py * (usize)clks_fb.info.pitch) + ((usize)x * 4U));

            clks_fb_fill_span_color32(shadow_row, span_pixels, rgb);
            clks_fb_fill_span_color32(row_ptr, span_pixels, rgb);
        }
        return;
    }

    for (py = y; py < end_y; py++) {
        u32 *row_ptr = (u32 *)(void *)(clks_fb.address + ((usize)py * (usize)clks_fb.info.pitch) + ((usize)x * 4U));
        clks_fb_fill_span_color32(row_ptr, span_pixels, rgb);
    }
}

static void clks_fb_blit_rgba_internal(i32 dst_x, i32 dst_y, const void *src_pixels, u32 src_width, u32 src_height,
                                       u32 src_pitch_bytes, clks_bool update_shadow) {
    i32 blit_x = dst_x;
    i32 blit_y = dst_y;
    i32 src_start_x = 0;
    i32 src_start_y = 0;
    i32 blit_w;
    i32 blit_h;
    u32 row;
    usize row_bytes;
    const u8 *src_base;

    if (clks_fb.ready == CLKS_FALSE || clks_fb.info.bpp != 32) {
        return;
    }

    if (src_pixels == CLKS_NULL || src_width == 0U || src_height == 0U) {
        return;
    }

    if (src_pitch_bytes < (src_width * 4U)) {
        return;
    }

    blit_w = (i32)src_width;
    blit_h = (i32)src_height;

    if (blit_x < 0) {
        src_start_x = -blit_x;
        blit_w -= src_start_x;
        blit_x = 0;
    }

    if (blit_y < 0) {
        src_start_y = -blit_y;
        blit_h -= src_start_y;
        blit_y = 0;
    }

    if (blit_w <= 0 || blit_h <= 0) {
        return;
    }

    if (blit_x >= (i32)clks_fb.info.width || blit_y >= (i32)clks_fb.info.height) {
        return;
    }

    if (blit_x + blit_w > (i32)clks_fb.info.width) {
        blit_w = (i32)clks_fb.info.width - blit_x;
    }

    if (blit_y + blit_h > (i32)clks_fb.info.height) {
        blit_h = (i32)clks_fb.info.height - blit_y;
    }

    if (blit_w <= 0 || blit_h <= 0) {
        return;
    }

    row_bytes = (usize)(u32)blit_w * 4U;
    src_base =
        (const u8 *)src_pixels + ((usize)(u32)src_start_y * (usize)src_pitch_bytes) + ((usize)(u32)src_start_x * 4U);

    for (row = 0U; row < (u32)blit_h; row++) {
        const u8 *src_row = src_base + ((usize)row * (usize)src_pitch_bytes);
        u8 *dst_row = (u8 *)(void *)(clks_fb.address + ((usize)((u32)blit_y + row) * (usize)clks_fb.info.pitch) +
                                     ((usize)(u32)blit_x * 4U));

        if (update_shadow == CLKS_TRUE && clks_fb.shadow_ready == CLKS_TRUE) {
            u8 *shadow_row =
                clks_fb.shadow + ((usize)((u32)blit_y + row) * (usize)clks_fb.info.pitch) + ((usize)(u32)blit_x * 4U);
            clks_fb_copy_forward_bytes(shadow_row, src_row, row_bytes);
        }

        clks_fb_copy_forward_bytes(dst_row, src_row, row_bytes);
    }
}

void clks_fb_blit_rgba(i32 dst_x, i32 dst_y, const void *src_pixels, u32 src_width, u32 src_height,
                       u32 src_pitch_bytes) {
    clks_fb_blit_rgba_internal(dst_x, dst_y, src_pixels, src_width, src_height, src_pitch_bytes, CLKS_TRUE);
}

void clks_fb_blit_rgba_no_shadow(i32 dst_x, i32 dst_y, const void *src_pixels, u32 src_width, u32 src_height,
                                 u32 src_pitch_bytes) {
    clks_fb_blit_rgba_internal(dst_x, dst_y, src_pixels, src_width, src_height, src_pitch_bytes, CLKS_FALSE);
}

void clks_fb_clear(u32 rgb) {
    clks_fb_fill_rect(0U, 0U, clks_fb.info.width, clks_fb.info.height, rgb);
}

void clks_fb_scroll_up(u32 pixel_rows, u32 fill_rgb) {
    usize row_bytes;
    u32 move_rows;
    usize move_bytes;
    usize tail_offset;
    u8 *fb_base;

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
    move_bytes = (usize)move_rows * row_bytes;
    tail_offset = (usize)(clks_fb.info.height - pixel_rows) * row_bytes;
    fb_base = (u8 *)(void *)clks_fb.address;

    if (clks_fb.shadow_ready == CLKS_TRUE) {
        clks_fb_copy_forward_bytes(clks_fb.shadow, clks_fb.shadow + ((usize)pixel_rows * row_bytes), move_bytes);
        clks_fb_fill_rows_color32(clks_fb.shadow + tail_offset, row_bytes, pixel_rows, fill_rgb);
        clks_fb_copy_forward_bytes(fb_base, fb_base + ((usize)pixel_rows * row_bytes), move_bytes);
        clks_fb_fill_rows_color32(fb_base + tail_offset, row_bytes, pixel_rows, fill_rgb);
        return;
    }

    clks_fb_copy_forward_bytes(fb_base, fb_base + ((usize)pixel_rows * row_bytes), move_bytes);
    clks_fb_fill_rows_color32(fb_base + tail_offset, row_bytes, pixel_rows, fill_rgb);
}

void clks_fb_draw_char_scaled(u32 x, u32 y, char ch, u32 fg_rgb, u32 bg_rgb, u32 style_flags, u32 scale) {
    const u8 *glyph;
    u32 row;
    u32 col;
    u32 cols;
    u32 rows;
    u32 row_stride;
    u32 draw_cols;
    u32 draw_rows;
    u32 out_cols;
    u32 out_rows;
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

    if (scale == 0U) {
        scale = 1U;
    }

    if (scale > 3U) {
        scale = 3U;
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

    out_cols = cols * scale;
    out_rows = rows * scale;

    if (out_cols == 0U || out_rows == 0U) {
        return;
    }

    draw_cols = out_cols;
    if (x + draw_cols > clks_fb.info.width) {
        draw_cols = clks_fb.info.width - x;
    }

    draw_rows = out_rows;
    if (y + draw_rows > clks_fb.info.height) {
        draw_rows = clks_fb.info.height - y;
    }

    style_bold = ((style_flags & CLKS_FB_STYLE_BOLD) != 0U) ? CLKS_TRUE : CLKS_FALSE;
    style_underline = ((style_flags & CLKS_FB_STYLE_UNDERLINE) != 0U) ? CLKS_TRUE : CLKS_FALSE;
    underline_row = (out_rows > scale) ? (out_rows - (2U * scale)) : 0U;

    for (row = 0U; row < draw_rows; row++) {
        u32 glyph_row = row / scale;
        const u8 *row_bits = glyph + ((usize)glyph_row * (usize)row_stride);
        u32 *dst_row =
            (u32 *)(void *)(clks_fb.address + ((usize)(y + row) * (usize)clks_fb.info.pitch) + ((usize)x * 4U));
        u32 *shadow_row =
            (clks_fb.shadow_ready == CLKS_TRUE)
                ? (u32 *)(void *)(clks_fb.shadow + ((usize)(y + row) * (usize)clks_fb.info.pitch) + ((usize)x * 4U))
                : CLKS_NULL;

        for (col = 0U; col < draw_cols; col++) {
            u32 glyph_col = col / scale;
            u8 bits = row_bits[glyph_col >> 3U];
            u8 mask = (u8)(0x80U >> (glyph_col & 7U));
            clks_bool pixel_on = ((bits & mask) != 0U) ? CLKS_TRUE : CLKS_FALSE;
            u32 color;

            if (style_bold == CLKS_TRUE && pixel_on == CLKS_FALSE && glyph_col > 0U) {
                u32 left_col = glyph_col - 1U;
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
            if (shadow_row != CLKS_NULL) {
                shadow_row[col] = color;
                dst_row[col] = color;
            } else {
                dst_row[col] = color;
            }
        }
    }
}

void clks_fb_draw_char_styled(u32 x, u32 y, char ch, u32 fg_rgb, u32 bg_rgb, u32 style_flags) {
    clks_fb_draw_char_scaled(x, y, ch, fg_rgb, bg_rgb, style_flags, 1U);
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
