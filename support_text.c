#include <SDL.h>
#include <stb/stb_truetype.h>
#include <xgu/xgu.h>
#include <xgu/xgux.h>

#include "main.h"
#include "support_renderer.h"

#define UTF8_IS_CONT(b)         (((b) & 0xC0) == 0x80)
#define UTF8_MASK(b)            ((b) & 0x3F)
#define UTF8_C2(b0, b1)         (((b0) & 0x1F) << 6 | UTF8_MASK(b1))
#define UTF8_C3(b0, b1, b2)     (((b0) & 0x0F) << 12 | UTF8_MASK(b1) << 6 | UTF8_MASK(b2))
#define UTF8_C4(b0, b1, b2, b3) (((b0) & 0x07) << 18 | UTF8_MASK(b1) << 12 | UTF8_MASK(b2) << 6 | UTF8_MASK(b3))

static int utf8_decode(const char *s, int *len)
{
    const unsigned char *p = (const unsigned char *)s;

    if (p[0] < 0x80) {
        *len = 1;
        return p[0];
    }

    if ((p[0] & 0xE0) == 0xC0) {
        if (!UTF8_IS_CONT(p[1])) {
            return 0;
        }
        *len = 2;
        return UTF8_C2(p[0], p[1]);
    }

    if ((p[0] & 0xF0) == 0xE0) {
        if (!UTF8_IS_CONT(p[1]) || !UTF8_IS_CONT(p[2])) {
            return 0;
        }
        *len = 3;
        return UTF8_C3(p[0], p[1], p[2]);
    }

    if ((p[0] & 0xF8) == 0xF0) {
        if (!UTF8_IS_CONT(p[1]) || !UTF8_IS_CONT(p[2]) || !UTF8_IS_CONT(p[3])) {
            return 0;
        }
        *len = 4;
        return UTF8_C4(p[0], p[1], p[2], p[3]);
    }

    return 0;
}

int text_calculate_width(Font *font, const char *text)
{
    float xadvance = 0.0f;
    int len;
    for (const char *p = text; *p && *p != '\0'; p++) {
        int unicode_codepoint = utf8_decode(p, &len);
        assert(len > 0);

        int advance_width = 0;

        int index = stbtt_FindGlyphIndex(&font->font_info, unicode_codepoint);
        if (index == 0) {
            index = stbtt_FindGlyphIndex(&font->font_info, '-');
            assert(index != 0);
        }
        stbtt_GetGlyphHMetrics(&font->font_info, index, &advance_width, NULL);

        xadvance += advance_width;

        p += len - 1;
    }
    return (int)(xadvance * font->scale);
}

static const stbtt_packedchar *get_packed_char_range(Font *font, int unicode_codepoint, int *character_index)
{
    for (int i = 0; i < font->range_count; i++) {
        stbtt_pack_range *range = &font->range[i];
        if (unicode_codepoint >= range->first_unicode_codepoint_in_range &&
            unicode_codepoint < range->first_unicode_codepoint_in_range + range->num_chars) {
            *character_index = unicode_codepoint - range->first_unicode_codepoint_in_range;
            return range->chardata_for_range;
        }
    }
    return NULL;
}

void text_draw(Font *font, const char *text, int x, int y, const xgu_texture_tint_t *tint)
{
    float x_offset = (float)x;
    float y_offset = (float)y;
    float base_x = x_offset;

    int len, index, unicode_codepoint;
    for (const char *p = text; *p && *p != '\0'; p++) {

        if (*p == '\n') {
            x_offset = base_x;
            y_offset += font->line_height;
            continue;
        }

        if (*p == '\r') {
            x_offset = base_x;
            continue;
        }

        unicode_codepoint = utf8_decode(p, &len);
        assert(len > 0);

        const stbtt_packedchar *packed_char = get_packed_char_range(font, unicode_codepoint, &index);
        // If the character is not found in the packed data, draw a placeholder
        if (packed_char == NULL) {
            packed_char = get_packed_char_range(font, '-', &index);

            // Every font should have a basic ASCII character fallback
            assert(packed_char != NULL);
        }

        stbtt_aligned_quad b;
        stbtt_GetPackedQuad(packed_char, font->texture->data_width, font->texture->data_height, index,
                            &x_offset, &y_offset, &b, 1);

        xgu_texture_boundary_t xgu_texture_boundary = {
            .s0 = b.s0 * (float)font->texture->data_width,
            .s1 = b.s1 * (float)font->texture->data_width,
            .t0 = b.t0 * (float)font->texture->data_height,
            .t1 = b.t1 * (float)font->texture->data_height};

        renderer_draw_textured_rectangle(
            (int)b.x0, (int)b.y0,
            (int)(b.x1 - b.x0), (int)(b.y1 - b.y0),
            font->texture, tint, &xgu_texture_boundary);

        p += len - 1;
    }
}

int text_create(const unsigned char *ttf_data, float font_size, const int (*range)[2], int range_count, Font *font)
{
    unsigned char *font_texture_bitmap = NULL;

    font->range = malloc(sizeof(stbtt_pack_range) * range_count);
    font->range_count = range_count;

    for (int i = 0; i < range_count; i++) {
        font->range[i].font_size = font_size;
        font->range[i].first_unicode_codepoint_in_range = range[i][0];
        font->range[i].num_chars = range[i][1] - range[i][0] + 1;
        font->range[i].chardata_for_range = malloc(sizeof(stbtt_packedchar) * font->range[i].num_chars);
        memset(font->range[i].chardata_for_range, 0, sizeof(stbtt_packedchar) * font->range[i].num_chars);
    }

    int w = 32, h = 32, ret = 0;
    do {
        stbtt_pack_context pc;
        font_texture_bitmap = malloc(w * h);
        if (font_texture_bitmap == NULL) {
            return -1;
        }

        stbtt_PackBegin(&pc, font_texture_bitmap, w, h, 0, 1, NULL);
        ret = stbtt_PackFontRanges(&pc, ttf_data, 0, font->range, range_count);
        stbtt_PackEnd(&pc);

        if (ret == 0) {
            (w < h) ? (w *= 2) : (h *= 2);
            free(font_texture_bitmap);
            continue;
        }

        stbtt_InitFont(&font->font_info, ttf_data, stbtt_GetFontOffsetForIndex(ttf_data, 0));
        font->scale = stbtt_ScaleForPixelHeight(&font->font_info, font_size);
        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font->font_info, &ascent, &descent, &line_gap);

        font->line_height = (float)(ascent - descent + line_gap) * font->scale;

        font->texture = texture_create(font_texture_bitmap, w, h, XGU_TEXTURE_FORMAT_A8);
        if (font->texture == NULL) {
            free(font_texture_bitmap);
            return -1;
        }
        free(font_texture_bitmap);
    } while (ret == 0);

    return 0;
}
