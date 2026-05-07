#include <clks/string.h>
#include <clks/types.h>
#include <clks/ttf.h>

#define XIAOBAIOS_TTF_MAX_POINTS 512U
#define XIAOBAIOS_TTF_MAX_CONTOURS 96U
#define XIAOBAIOS_TTF_MAX_EDGES 768U
#define XIAOBAIOS_TTF_TAG(a, b, c, d)                                                                                 \
    ((((u32)(a)) << 24U) | (((u32)(b)) << 16U) | (((u32)(c)) << 8U) | ((u32)(d)))

struct xiaobaios_ttf_point {
    i32 x;
    i32 y;
    u8 on_curve;
};

struct xiaobaios_ttf_edge {
    i32 x0;
    i32 y0;
    i32 x1;
    i32 y1;
};

struct xiaobaios_ttf_intersection {
    i32 x;
    i32 winding_delta;
};

static u16 ttf_be16(const u8 *p) {
    return (u16)(((u16)p[0] << 8U) | (u16)p[1]);
}

static i16 ttf_be_i16(const u8 *p) {
    return (i16)ttf_be16(p);
}

static u32 ttf_be32(const u8 *p) {
    return ((u32)p[0] << 24U) | ((u32)p[1] << 16U) | ((u32)p[2] << 8U) | (u32)p[3];
}

static clks_bool ttf_range_valid(const struct xiaobaios_ttf_font *font, u32 offset, u32 len) {
    if (font == CLKS_NULL) {
        return CLKS_FALSE;
    }
    return ((u64)offset <= font->size && (u64)len <= font->size - (u64)offset) ? CLKS_TRUE : CLKS_FALSE;
}

static const u8 *ttf_ptr(const struct xiaobaios_ttf_font *font, u32 offset, u32 len) {
    if (ttf_range_valid(font, offset, len) == CLKS_FALSE) {
        return CLKS_NULL;
    }
    return font->data + offset;
}

static void ttf_set_table(struct xiaobaios_ttf_font *font, u32 tag, u32 offset, u32 length) {
    struct xiaobaios_ttf_table table;

    if (ttf_range_valid(font, offset, length) == CLKS_FALSE) {
        return;
    }

    table.offset = offset;
    table.length = length;
    if (tag == XIAOBAIOS_TTF_TAG('c', 'm', 'a', 'p')) {
        font->cmap = table;
    } else if (tag == XIAOBAIOS_TTF_TAG('h', 'e', 'a', 'd')) {
        font->head = table;
    } else if (tag == XIAOBAIOS_TTF_TAG('h', 'h', 'e', 'a')) {
        font->hhea = table;
    } else if (tag == XIAOBAIOS_TTF_TAG('h', 'm', 't', 'x')) {
        font->hmtx = table;
    } else if (tag == XIAOBAIOS_TTF_TAG('l', 'o', 'c', 'a')) {
        font->loca = table;
    } else if (tag == XIAOBAIOS_TTF_TAG('g', 'l', 'y', 'f')) {
        font->glyf = table;
    } else if (tag == XIAOBAIOS_TTF_TAG('m', 'a', 'x', 'p')) {
        font->maxp = table;
    }
}

static clks_bool ttf_find_cmaps(struct xiaobaios_ttf_font *font) {
    const u8 *cmap = ttf_ptr(font, font->cmap.offset, font->cmap.length);
    u16 table_count;
    u16 i;

    if (cmap == CLKS_NULL || font->cmap.length < 4U) {
        return CLKS_FALSE;
    }

    table_count = ttf_be16(cmap + 2U);
    if (4U + ((u32)table_count * 8U) > font->cmap.length) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < table_count; i++) {
        const u8 *rec = cmap + 4U + ((u32)i * 8U);
        u16 platform = ttf_be16(rec);
        u16 encoding = ttf_be16(rec + 2U);
        u32 sub_offset = ttf_be32(rec + 4U);
        const u8 *sub;
        u16 format;

        if (sub_offset + 8U > font->cmap.length) {
            continue;
        }

        sub = cmap + sub_offset;
        format = ttf_be16(sub);
        if (format == 12U && sub_offset + 16U <= font->cmap.length &&
            (platform == 3U || platform == 0U) && (encoding == 10U || encoding == 4U || encoding == 0U)) {
            u32 length = ttf_be32(sub + 4U);
            if (length >= 16U && sub_offset + length <= font->cmap.length) {
                font->cmap12 = font->cmap.offset + sub_offset;
            }
        } else if (format == 4U && (platform == 3U || platform == 0U) &&
                   (encoding == 1U || encoding == 0U || encoding == 3U)) {
            font->cmap4 = font->cmap.offset + sub_offset;
        }
    }

    return (font->cmap12 != 0U || font->cmap4 != 0U) ? CLKS_TRUE : CLKS_FALSE;
}

clks_bool xiaobaios_ttf_parse(const void *blob, u64 blob_size, struct xiaobaios_ttf_font *out_font) {
    struct xiaobaios_ttf_font font;
    const u8 *dir;
    const u8 *head;
    const u8 *hhea;
    const u8 *maxp;
    u16 table_count;
    u16 i;

    if (blob == CLKS_NULL || out_font == CLKS_NULL || blob_size < 12ULL || blob_size > 64ULL * 1024ULL * 1024ULL) {
        return CLKS_FALSE;
    }

    clks_memset(&font, 0, sizeof(font));
    font.data = (const u8 *)blob;
    font.size = blob_size;

    dir = ttf_ptr(&font, 0U, 12U);
    if (dir == CLKS_NULL) {
        return CLKS_FALSE;
    }

    table_count = ttf_be16(dir + 4U);
    if ((u64)12U + ((u64)table_count * 16ULL) > blob_size) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < table_count; i++) {
        const u8 *rec = font.data + 12U + ((u32)i * 16U);
        ttf_set_table(&font, ttf_be32(rec), ttf_be32(rec + 8U), ttf_be32(rec + 12U));
    }

    head = ttf_ptr(&font, font.head.offset, 54U);
    hhea = ttf_ptr(&font, font.hhea.offset, 36U);
    maxp = ttf_ptr(&font, font.maxp.offset, 6U);
    if (head == CLKS_NULL || hhea == CLKS_NULL || maxp == CLKS_NULL || font.cmap.length == 0U ||
        font.hmtx.length == 0U || font.loca.length == 0U || font.glyf.length == 0U) {
        return CLKS_FALSE;
    }

    font.units_per_em = ttf_be16(head + 18U);
    font.index_to_loc_format = ttf_be_i16(head + 50U);
    font.ascent = ttf_be_i16(hhea + 4U);
    font.descent = ttf_be_i16(hhea + 6U);
    font.num_hmetrics = ttf_be16(hhea + 34U);
    font.num_glyphs = ttf_be16(maxp + 4U);
    if (font.units_per_em == 0U || font.num_glyphs == 0U || font.num_hmetrics == 0U ||
        ttf_find_cmaps(&font) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    font.ready = CLKS_TRUE;
    *out_font = font;
    return CLKS_TRUE;
}

static u16 ttf_glyph_index_cmap4(const struct xiaobaios_ttf_font *font, u32 codepoint) {
    const u8 *cmap;
    u16 seg_count;
    u32 end_codes;
    u32 start_codes;
    u32 id_delta;
    u32 id_range_offset;
    u16 i;

    if (codepoint > 0xFFFFU || font->cmap4 == 0U) {
        return 0U;
    }

    cmap = ttf_ptr(font, font->cmap4, 8U);
    if (cmap == CLKS_NULL || ttf_be16(cmap) != 4U) {
        return 0U;
    }

    seg_count = (u16)(ttf_be16(cmap + 6U) / 2U);
    end_codes = 14U;
    start_codes = end_codes + ((u32)seg_count * 2U) + 2U;
    id_delta = start_codes + ((u32)seg_count * 2U);
    id_range_offset = id_delta + ((u32)seg_count * 2U);
    if (id_range_offset + ((u32)seg_count * 2U) > ttf_be16(cmap + 2U)) {
        return 0U;
    }

    for (i = 0U; i < seg_count; i++) {
        u16 end_code = ttf_be16(cmap + end_codes + ((u32)i * 2U));
        u16 start_code;
        u16 delta;
        u16 range_offset;
        u32 glyph_offset;

        if ((u16)codepoint > end_code) {
            continue;
        }

        start_code = ttf_be16(cmap + start_codes + ((u32)i * 2U));
        if ((u16)codepoint < start_code) {
            return 0U;
        }

        delta = ttf_be16(cmap + id_delta + ((u32)i * 2U));
        range_offset = ttf_be16(cmap + id_range_offset + ((u32)i * 2U));
        if (range_offset == 0U) {
            return (u16)((u16)codepoint + delta);
        }

        glyph_offset =
            id_range_offset + ((u32)i * 2U) + (u32)range_offset + (((u32)(u16)codepoint - start_code) * 2U);
        if (glyph_offset + 2U > ttf_be16(cmap + 2U)) {
            return 0U;
        }

        {
            u16 glyph = ttf_be16(cmap + glyph_offset);
            return (glyph == 0U) ? 0U : (u16)(glyph + delta);
        }
    }

    return 0U;
}

static u16 ttf_glyph_index_cmap12(const struct xiaobaios_ttf_font *font, u32 codepoint) {
    const u8 *cmap;
    u32 groups;
    u32 lo;
    u32 hi;

    if (font->cmap12 == 0U) {
        return 0U;
    }

    cmap = ttf_ptr(font, font->cmap12, 16U);
    if (cmap == CLKS_NULL || ttf_be16(cmap) != 12U) {
        return 0U;
    }

    groups = ttf_be32(cmap + 12U);
    if (16ULL + ((u64)groups * 12ULL) > (u64)ttf_be32(cmap + 4U)) {
        return 0U;
    }

    lo = 0U;
    hi = groups;
    while (lo < hi) {
        u32 mid = lo + ((hi - lo) / 2U);
        const u8 *group = cmap + 16U + (mid * 12U);
        u32 start = ttf_be32(group);
        u32 end = ttf_be32(group + 4U);
        u32 glyph_start = ttf_be32(group + 8U);

        if (codepoint < start) {
            hi = mid;
        } else if (codepoint > end) {
            lo = mid + 1U;
        } else {
            u32 glyph = glyph_start + (codepoint - start);
            return (glyph <= 0xFFFFU) ? (u16)glyph : 0U;
        }
    }

    return 0U;
}

static u16 ttf_glyph_index(const struct xiaobaios_ttf_font *font, u32 codepoint) {
    u16 glyph = ttf_glyph_index_cmap12(font, codepoint);
    if (glyph != 0U) {
        return glyph;
    }
    return ttf_glyph_index_cmap4(font, codepoint);
}

static u32 ttf_glyph_offset(const struct xiaobaios_ttf_font *font, u16 glyph_id) {
    const u8 *loca;

    if (glyph_id >= font->num_glyphs) {
        return 0U;
    }

    if (font->index_to_loc_format == 0) {
        loca = ttf_ptr(font, font->loca.offset + ((u32)glyph_id * 2U), 2U);
        return (loca == CLKS_NULL) ? 0U : ((u32)ttf_be16(loca) * 2U);
    }

    loca = ttf_ptr(font, font->loca.offset + ((u32)glyph_id * 4U), 4U);
    return (loca == CLKS_NULL) ? 0U : ttf_be32(loca);
}

static u16 ttf_advance_width(const struct xiaobaios_ttf_font *font, u16 glyph_id) {
    u32 metric_index = glyph_id;
    const u8 *metric;

    if (metric_index >= (u32)font->num_hmetrics) {
        metric_index = (u32)font->num_hmetrics - 1U;
    }

    metric = ttf_ptr(font, font->hmtx.offset + (metric_index * 4U), 2U);
    return (metric == CLKS_NULL) ? font->units_per_em / 2U : ttf_be16(metric);
}

static i32 ttf_scale_value(const struct xiaobaios_ttf_font *font, i32 value, u32 px) {
    i64 scaled = (i64)value * (i64)px;
    if (font->units_per_em == 0U) {
        return value;
    }
    return (i32)(scaled / (i64)font->units_per_em);
}

static void ttf_add_edge(struct xiaobaios_ttf_edge *edges, u32 *edge_count, i32 x0, i32 y0, i32 x1, i32 y1) {
    if (*edge_count >= XIAOBAIOS_TTF_MAX_EDGES || y0 == y1) {
        return;
    }
    edges[*edge_count].x0 = x0;
    edges[*edge_count].y0 = y0;
    edges[*edge_count].x1 = x1;
    edges[*edge_count].y1 = y1;
    (*edge_count)++;
}

static i32 ttf_quad(i32 a, i32 b, i32 c, i32 t, i32 steps) {
    i32 mt = steps - t;
    return ((mt * mt * a) + (2 * mt * t * b) + (t * t * c)) / (steps * steps);
}

static void ttf_add_quad(struct xiaobaios_ttf_edge *edges, u32 *edge_count, struct xiaobaios_ttf_point a,
                         struct xiaobaios_ttf_point b, struct xiaobaios_ttf_point c) {
    i32 prev_x = a.x;
    i32 prev_y = a.y;
    i32 step;

    for (step = 1; step <= 8; step++) {
        i32 x = ttf_quad(a.x, b.x, c.x, step, 8);
        i32 y = ttf_quad(a.y, b.y, c.y, step, 8);
        ttf_add_edge(edges, edge_count, prev_x, prev_y, x, y);
        prev_x = x;
        prev_y = y;
    }
}

static void ttf_trace_contour(struct xiaobaios_ttf_point *points, i32 first, i32 last,
                              struct xiaobaios_ttf_edge *edges, u32 *edge_count) {
    struct xiaobaios_ttf_point start;
    struct xiaobaios_ttf_point prev;
    i32 i;

    if (first > last) {
        return;
    }

    if (points[first].on_curve != 0U) {
        start = points[first];
        i = first + 1;
    } else {
        struct xiaobaios_ttf_point lastp = points[last];
        if (lastp.on_curve != 0U) {
            start = lastp;
        } else {
            start.x = (points[first].x + lastp.x) / 2;
            start.y = (points[first].y + lastp.y) / 2;
            start.on_curve = 1U;
        }
        i = first;
    }

    prev = start;
    while (i <= last) {
        struct xiaobaios_ttf_point p = points[i];
        if (p.on_curve != 0U) {
            ttf_add_edge(edges, edge_count, prev.x, prev.y, p.x, p.y);
            prev = p;
            i++;
        } else {
            struct xiaobaios_ttf_point next = (i == last) ? start : points[i + 1];
            if (next.on_curve != 0U) {
                ttf_add_quad(edges, edge_count, prev, p, next);
                prev = next;
                i += 2;
            } else {
                struct xiaobaios_ttf_point mid;
                mid.x = (p.x + next.x) / 2;
                mid.y = (p.y + next.y) / 2;
                mid.on_curve = 1U;
                ttf_add_quad(edges, edge_count, prev, p, mid);
                prev = mid;
                i++;
            }
        }
    }
    ttf_add_edge(edges, edge_count, prev.x, prev.y, start.x, start.y);
}

static clks_bool ttf_append_simple_glyph(const struct xiaobaios_ttf_font *font, u16 glyph_id, i32 dx, i32 dy,
                                         struct xiaobaios_ttf_point *points, u16 *point_count, u16 *end_points,
                                         u16 *contour_count) {
    u32 glyph_offset = ttf_glyph_offset(font, glyph_id);
    u32 next_offset = ttf_glyph_offset(font, (u16)(glyph_id + 1U));
    const u8 *glyph;
    i16 contours;
    u16 local_point_count;
    u16 flags_count = 0U;
    u16 local_ends[XIAOBAIOS_TTF_MAX_CONTOURS];
    u8 flags[XIAOBAIOS_TTF_MAX_POINTS];
    u32 pos;
    u16 i;
    u16 base_point;

    if (next_offset <= glyph_offset || *point_count >= XIAOBAIOS_TTF_MAX_POINTS ||
        *contour_count >= XIAOBAIOS_TTF_MAX_CONTOURS) {
        return CLKS_FALSE;
    }

    glyph = ttf_ptr(font, font->glyf.offset + glyph_offset, next_offset - glyph_offset);
    if (glyph == CLKS_NULL || next_offset - glyph_offset < 10U) {
        return CLKS_FALSE;
    }

    contours = ttf_be_i16(glyph);
    if (contours <= 0 || contours > (i16)XIAOBAIOS_TTF_MAX_CONTOURS ||
        *contour_count + (u16)contours > XIAOBAIOS_TTF_MAX_CONTOURS) {
        return CLKS_FALSE;
    }

    pos = 10U;
    for (i = 0U; i < (u16)contours; i++) {
        local_ends[i] = ttf_be16(glyph + pos);
        pos += 2U;
    }

    local_point_count = (u16)(local_ends[contours - 1] + 1U);
    if (local_point_count == 0U || local_point_count > XIAOBAIOS_TTF_MAX_POINTS ||
        *point_count + local_point_count > XIAOBAIOS_TTF_MAX_POINTS || pos + 2U > next_offset - glyph_offset) {
        return CLKS_FALSE;
    }

    {
        u16 instruction_len = ttf_be16(glyph + pos);
        pos += 2U + (u32)instruction_len;
        if (pos >= next_offset - glyph_offset) {
            return CLKS_FALSE;
        }
    }

    while (flags_count < local_point_count && pos < next_offset - glyph_offset) {
        u8 flag = glyph[pos++];
        u8 repeat = 0U;
        flags[flags_count++] = flag;
        if ((flag & 0x08U) != 0U) {
            if (pos >= next_offset - glyph_offset) {
                return CLKS_FALSE;
            }
            repeat = glyph[pos++];
        }
        while (repeat > 0U && flags_count < local_point_count) {
            flags[flags_count++] = flag;
            repeat--;
        }
    }

    if (flags_count != local_point_count) {
        return CLKS_FALSE;
    }

    base_point = *point_count;
    {
        i32 x = 0;
        for (i = 0U; i < local_point_count; i++) {
            if ((flags[i] & 0x02U) != 0U) {
                if (pos >= next_offset - glyph_offset) {
                    return CLKS_FALSE;
                }
                x += ((flags[i] & 0x10U) != 0U) ? (i32)glyph[pos] : -(i32)glyph[pos];
                pos++;
            } else if ((flags[i] & 0x10U) == 0U) {
                if (pos + 2U > next_offset - glyph_offset) {
                    return CLKS_FALSE;
                }
                x += (i32)ttf_be_i16(glyph + pos);
                pos += 2U;
            }
            points[base_point + i].x = x + dx;
            points[base_point + i].on_curve = (flags[i] & 0x01U) != 0U ? 1U : 0U;
        }
    }

    {
        i32 y = 0;
        for (i = 0U; i < local_point_count; i++) {
            if ((flags[i] & 0x04U) != 0U) {
                if (pos >= next_offset - glyph_offset) {
                    return CLKS_FALSE;
                }
                y += ((flags[i] & 0x20U) != 0U) ? (i32)glyph[pos] : -(i32)glyph[pos];
                pos++;
            } else if ((flags[i] & 0x20U) == 0U) {
                if (pos + 2U > next_offset - glyph_offset) {
                    return CLKS_FALSE;
                }
                y += (i32)ttf_be_i16(glyph + pos);
                pos += 2U;
            }
            points[base_point + i].y = y + dy;
        }
    }

    for (i = 0U; i < (u16)contours; i++) {
        end_points[*contour_count + i] = (u16)(base_point + local_ends[i]);
    }
    *point_count = (u16)(*point_count + local_point_count);
    *contour_count = (u16)(*contour_count + (u16)contours);
    return CLKS_TRUE;
}

static clks_bool ttf_append_compound_glyph(const struct xiaobaios_ttf_font *font, const u8 *glyph, u32 glyph_len,
                                           struct xiaobaios_ttf_point *points, u16 *point_count, u16 *end_points,
                                           u16 *contour_count) {
    u32 pos = 10U;
    u32 component_count = 0U;

    while (pos + 4U <= glyph_len && component_count < 32U) {
        u16 flags = ttf_be16(glyph + pos);
        u16 component_gid = ttf_be16(glyph + pos + 2U);
        i32 arg1;
        i32 arg2;
        i32 dx = 0;
        i32 dy = 0;

        pos += 4U;
        if ((flags & 0x0001U) != 0U) {
            if (pos + 4U > glyph_len) {
                return CLKS_FALSE;
            }
            if ((flags & 0x0002U) != 0U) {
                arg1 = (i32)ttf_be_i16(glyph + pos);
                arg2 = (i32)ttf_be_i16(glyph + pos + 2U);
            } else {
                arg1 = (i32)ttf_be16(glyph + pos);
                arg2 = (i32)ttf_be16(glyph + pos + 2U);
            }
            pos += 4U;
        } else {
            if (pos + 2U > glyph_len) {
                return CLKS_FALSE;
            }
            if ((flags & 0x0002U) != 0U) {
                arg1 = (i32)(i8)glyph[pos];
                arg2 = (i32)(i8)glyph[pos + 1U];
            } else {
                arg1 = (i32)glyph[pos];
                arg2 = (i32)glyph[pos + 1U];
            }
            pos += 2U;
        }

        if ((flags & 0x0002U) != 0U) {
            dx = arg1;
            dy = arg2;
        }

        if ((flags & 0x0008U) != 0U) {
            pos += 2U;
        } else if ((flags & 0x0040U) != 0U) {
            pos += 4U;
        } else if ((flags & 0x0080U) != 0U) {
            pos += 8U;
        }
        if (pos > glyph_len) {
            return CLKS_FALSE;
        }

        if (ttf_append_simple_glyph(font, component_gid, dx, dy, points, point_count, end_points, contour_count) ==
            CLKS_FALSE) {
            return CLKS_FALSE;
        }

        component_count++;
        if ((flags & 0x0020U) == 0U) {
            break;
        }
    }

    return component_count > 0U ? CLKS_TRUE : CLKS_FALSE;
}

static void ttf_sort_intersections(struct xiaobaios_ttf_intersection *values, u32 count) {
    u32 i;

    for (i = 1U; i < count; i++) {
        struct xiaobaios_ttf_intersection value = values[i];
        i32 j = (i32)i - 1;
        while (j >= 0 && values[j].x > value.x) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = value;
    }
}

static u32 ttf_scanline_intersections(const struct xiaobaios_ttf_edge *edges, u32 edge_count, i32 y,
                                      struct xiaobaios_ttf_intersection *xs, u32 xs_max) {
    u32 count = 0U;
    u32 i;

    for (i = 0U; i < edge_count && count < xs_max; i++) {
        i32 x0 = edges[i].x0;
        i32 y0 = edges[i].y0;
        i32 x1 = edges[i].x1;
        i32 y1 = edges[i].y1;

        if (((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) != 0) {
            i64 cross = (i64)(x1 - x0) * (i64)(y - y0);
            i64 denom = (i64)(y1 - y0);
            xs[count].x = x0 + (i32)(cross / denom);
            xs[count].winding_delta = (y1 > y0) ? 1 : -1;
            count++;
        }
    }

    ttf_sort_intersections(xs, count);
    return count;
}

static clks_bool ttf_inside_span(const struct xiaobaios_ttf_intersection *xs, u32 count, i32 x) {
    i32 winding = 0;
    u32 i;

    for (i = 0U; i < count; i++) {
        if (x < xs[i].x) {
            break;
        }
        winding += xs[i].winding_delta;
    }
    return (winding != 0) ? CLKS_TRUE : CLKS_FALSE;
}

clks_bool xiaobaios_ttf_rasterize(const struct xiaobaios_ttf_font *font, u32 codepoint, u32 pixel_height,
                                  struct xiaobaios_ttf_bitmap *out) {
    u16 glyph_id;
    u32 glyph_offset;
    u32 next_offset;
    const u8 *glyph;
    i16 contours;
    i16 x_min;
    i16 x_max;
    u16 point_count = 0U;
    u16 flags_count = 0U;
    u16 end_points[XIAOBAIOS_TTF_MAX_CONTOURS];
    u8 flags[XIAOBAIOS_TTF_MAX_POINTS];
    struct xiaobaios_ttf_point points[XIAOBAIOS_TTF_MAX_POINTS];
    struct xiaobaios_ttf_edge edges[XIAOBAIOS_TTF_MAX_EDGES];
    u32 edge_count = 0U;
    u32 pos;
    u16 i;
    u16 raster_contours = 0U;
    i32 ascent_px;
    i32 baseline;
    i32 width;
    i32 height;
    u32 glyph_px_x;
    u32 glyph_px_y;

    if (font == CLKS_NULL || out == CLKS_NULL || font->ready == CLKS_FALSE || pixel_height < 8U ||
        pixel_height > XIAOBAIOS_TTF_BITMAP_MAX_H) {
        return CLKS_FALSE;
    }

    clks_memset(out, 0, sizeof(*out));
    glyph_id = ttf_glyph_index(font, codepoint);
    out->codepoint = codepoint;
    out->glyph_found = (glyph_id != 0U) ? CLKS_TRUE : CLKS_FALSE;
    height = (i32)pixel_height;
    glyph_px_y = pixel_height > 12U ? pixel_height - 3U : pixel_height - 1U;
    if (pixel_height >= 44U) {
        glyph_px_y = pixel_height - 2U;
    }
    if (glyph_px_y >= pixel_height) {
        glyph_px_y = pixel_height - 1U;
    }

    /*
     * The framebuffer TTY cell is intentionally narrower than the line height.
     * Keep large TTF glyphs slightly condensed instead of letting headings look
     * horizontally stretched or clipped inside the cell.
     */
    glyph_px_x = (glyph_px_y * 92U + 50U) / 100U;
    if (glyph_px_x < 8U) {
        glyph_px_x = 8U;
    }
    out->advance = ttf_scale_value(font, (i32)ttf_advance_width(font, glyph_id), glyph_px_x);
    if (out->advance <= 0) {
        out->advance = (i32)((pixel_height + 1U) / 2U);
    }

    glyph_offset = ttf_glyph_offset(font, glyph_id);
    next_offset = ttf_glyph_offset(font, (u16)(glyph_id + 1U));
    if (glyph_id == 0U || next_offset <= glyph_offset) {
        out->width = out->advance;
        out->height = height;
        return CLKS_TRUE;
    }

    glyph = ttf_ptr(font, font->glyf.offset + glyph_offset, next_offset - glyph_offset);
    if (glyph == CLKS_NULL || next_offset - glyph_offset < 10U) {
        return CLKS_FALSE;
    }

    contours = ttf_be_i16(glyph);
    if (contours == 0 || contours > (i16)XIAOBAIOS_TTF_MAX_CONTOURS) {
        return CLKS_FALSE;
    }

    x_min = ttf_be_i16(glyph + 2U);
    x_max = ttf_be_i16(glyph + 6U);
    if (contours > 0) {
        pos = 10U;
        for (i = 0U; i < (u16)contours; i++) {
            end_points[i] = ttf_be16(glyph + pos);
            pos += 2U;
        }

        point_count = (u16)(end_points[contours - 1] + 1U);
        if (point_count == 0U || point_count > XIAOBAIOS_TTF_MAX_POINTS || pos + 2U > next_offset - glyph_offset) {
            return CLKS_FALSE;
        }

        {
            u16 instruction_len = ttf_be16(glyph + pos);
            pos += 2U + (u32)instruction_len;
            if (pos >= next_offset - glyph_offset) {
                return CLKS_FALSE;
            }
        }

        while (flags_count < point_count && pos < next_offset - glyph_offset) {
            u8 flag = glyph[pos++];
            u8 repeat = 0U;
            flags[flags_count++] = flag;
            if ((flag & 0x08U) != 0U) {
                if (pos >= next_offset - glyph_offset) {
                    return CLKS_FALSE;
                }
                repeat = glyph[pos++];
            }
            while (repeat > 0U && flags_count < point_count) {
                flags[flags_count++] = flag;
                repeat--;
            }
        }

        if (flags_count != point_count) {
            return CLKS_FALSE;
        }

        {
            i32 x = 0;
            for (i = 0U; i < point_count; i++) {
                if ((flags[i] & 0x02U) != 0U) {
                    if (pos >= next_offset - glyph_offset) {
                        return CLKS_FALSE;
                    }
                    x += ((flags[i] & 0x10U) != 0U) ? (i32)glyph[pos] : -(i32)glyph[pos];
                    pos++;
                } else if ((flags[i] & 0x10U) == 0U) {
                    if (pos + 2U > next_offset - glyph_offset) {
                        return CLKS_FALSE;
                    }
                    x += (i32)ttf_be_i16(glyph + pos);
                    pos += 2U;
                }
                points[i].x = x;
                points[i].on_curve = (flags[i] & 0x01U) != 0U ? 1U : 0U;
            }
        }

        {
            i32 y = 0;
            for (i = 0U; i < point_count; i++) {
                if ((flags[i] & 0x04U) != 0U) {
                    if (pos >= next_offset - glyph_offset) {
                        return CLKS_FALSE;
                    }
                    y += ((flags[i] & 0x20U) != 0U) ? (i32)glyph[pos] : -(i32)glyph[pos];
                    pos++;
                } else if ((flags[i] & 0x20U) == 0U) {
                    if (pos + 2U > next_offset - glyph_offset) {
                        return CLKS_FALSE;
                    }
                    y += (i32)ttf_be_i16(glyph + pos);
                    pos += 2U;
                }
                points[i].y = y;
            }
        }
        raster_contours = (u16)contours;
    } else if (ttf_append_compound_glyph(font, glyph, next_offset - glyph_offset, points, &point_count, end_points,
                                         &raster_contours) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    ascent_px = ttf_scale_value(font, (i32)font->ascent, glyph_px_y);
    baseline = ascent_px;
    if (baseline < 1) {
        baseline = 1;
    }
    if (baseline > height - 2) {
        baseline = height - 2;
    }
    width = ttf_scale_value(font, (i32)(x_max - x_min), glyph_px_x) + 2;
    if (width < out->advance) {
        width = out->advance;
    }
    if (width > (i32)XIAOBAIOS_TTF_BITMAP_MAX_W) {
        width = (i32)XIAOBAIOS_TTF_BITMAP_MAX_W;
    }
    if (width <= 0) {
        width = 1;
    }

    for (i = 0U; i < point_count; i++) {
        points[i].x = ttf_scale_value(font, points[i].x - (i32)x_min, glyph_px_x * 4U);
        points[i].y = (baseline * 4) - ttf_scale_value(font, points[i].y, glyph_px_y * 4U);
    }

    {
        i32 first = 0;
        for (i = 0U; i < raster_contours; i++) {
            i32 last = (i32)end_points[i];
            ttf_trace_contour(points, first, last, edges, &edge_count);
            first = last + 1;
        }
    }

    out->width = width;
    out->height = height;
    if (out->advance > width) {
        out->advance = width;
    }

    {
        i32 yy;
        struct xiaobaios_ttf_intersection xs_a[XIAOBAIOS_TTF_MAX_EDGES];
        struct xiaobaios_ttf_intersection xs_b[XIAOBAIOS_TTF_MAX_EDGES];
        struct xiaobaios_ttf_intersection xs_c[XIAOBAIOS_TTF_MAX_EDGES];
        struct xiaobaios_ttf_intersection xs_d[XIAOBAIOS_TTF_MAX_EDGES];
        for (yy = 0; yy < height && yy < (i32)XIAOBAIOS_TTF_BITMAP_MAX_H; yy++) {
            i32 xx;
            u32 count_a = ttf_scanline_intersections(edges, edge_count, (yy * 4) + 1, xs_a,
                                                     XIAOBAIOS_TTF_MAX_EDGES);
            u32 count_b = ttf_scanline_intersections(edges, edge_count, (yy * 4) + 3, xs_b,
                                                     XIAOBAIOS_TTF_MAX_EDGES);
            u32 count_c = ttf_scanline_intersections(edges, edge_count, (yy * 4) + 2, xs_c,
                                                     XIAOBAIOS_TTF_MAX_EDGES);
            u32 count_d = ttf_scanline_intersections(edges, edge_count, (yy * 4) + 4, xs_d,
                                                     XIAOBAIOS_TTF_MAX_EDGES);
            for (xx = 0; xx < width && xx < (i32)XIAOBAIOS_TTF_BITMAP_MAX_W; xx++) {
                u32 samples = 0U;
                samples += ttf_inside_span(xs_a, count_a, (xx * 4) + 1) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_a, count_a, (xx * 4) + 2) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_a, count_a, (xx * 4) + 3) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_a, count_a, (xx * 4) + 4) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_c, count_c, (xx * 4) + 1) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_c, count_c, (xx * 4) + 2) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_c, count_c, (xx * 4) + 3) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_c, count_c, (xx * 4) + 4) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_b, count_b, (xx * 4) + 1) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_b, count_b, (xx * 4) + 2) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_b, count_b, (xx * 4) + 3) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_b, count_b, (xx * 4) + 4) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_d, count_d, (xx * 4) + 1) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_d, count_d, (xx * 4) + 2) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_d, count_d, (xx * 4) + 3) == CLKS_TRUE ? 1U : 0U;
                samples += ttf_inside_span(xs_d, count_d, (xx * 4) + 4) == CLKS_TRUE ? 1U : 0U;
                out->alpha[yy][xx] = (u8)((samples * 255U + 8U) / 16U);
            }
        }
    }

    return CLKS_TRUE;
}
