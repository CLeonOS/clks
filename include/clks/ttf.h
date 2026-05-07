#ifndef XIAOBAIOS_TTF_H
#define XIAOBAIOS_TTF_H

#include <clks/types.h>

#define XIAOBAIOS_TTF_BITMAP_MAX_W 96U
#define XIAOBAIOS_TTF_BITMAP_MAX_H 96U

struct xiaobaios_ttf_table {
    u32 offset;
    u32 length;
};

struct xiaobaios_ttf_font {
    clks_bool ready;
    const u8 *data;
    u64 size;
    struct xiaobaios_ttf_table cmap;
    struct xiaobaios_ttf_table head;
    struct xiaobaios_ttf_table hhea;
    struct xiaobaios_ttf_table hmtx;
    struct xiaobaios_ttf_table loca;
    struct xiaobaios_ttf_table glyf;
    struct xiaobaios_ttf_table maxp;
    u32 cmap4;
    u32 cmap12;
    u16 units_per_em;
    i16 index_to_loc_format;
    u16 num_glyphs;
    u16 num_hmetrics;
    i16 ascent;
    i16 descent;
};

struct xiaobaios_ttf_bitmap {
    clks_bool glyph_found;
    u32 codepoint;
    i32 width;
    i32 height;
    i32 advance;
    u8 alpha[XIAOBAIOS_TTF_BITMAP_MAX_H][XIAOBAIOS_TTF_BITMAP_MAX_W];
};

clks_bool xiaobaios_ttf_parse(const void *blob, u64 blob_size, struct xiaobaios_ttf_font *out_font);
clks_bool xiaobaios_ttf_rasterize(const struct xiaobaios_ttf_font *font, u32 codepoint, u32 pixel_height,
                                  struct xiaobaios_ttf_bitmap *out);

#endif
