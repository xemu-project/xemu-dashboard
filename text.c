#include <SDL.h>
#include <stb/stb_truetype.h>
#include <xgu/xgu.h>
#include <xgu/xgux.h>

#include "main.h"
#include "renderer.h"

int text_calculate_width(stbtt_bakedchar *cdata, const char *text)
{
    int width = 0;
    for (const char *p = text; *p; p++) {
        if (*p < 32) {
            continue;
        }
        const stbtt_bakedchar *b = &cdata[*p - 32];
        width += (int)b->xadvance;
    }
    return width;
}
void text_draw(stbtt_bakedchar *cdata, xgu_texture_t *body_text, const char *text, int x, int y, const xgu_texture_tint_t *tint)
{
    stbtt_bakedchar *datum = &cdata['(' - 32];
    int base_x = x;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            x = base_x;
            y += (int)(datum->y1 - datum->y0) + ITEM_PADDING; // Move to the next line
            continue;
        }
        if (*p < 32) {
            continue;
        }
        const stbtt_bakedchar *b = &cdata[*p - 32];

        xgu_texture_boundary_t xgu_texture_boundary = {
            .s0 = (float)b->x0,
            .s1 = (float)b->x1,
            .t0 = (float)b->y0,
            .t1 = (float)b->y1};

        renderer_draw_textured_rectangle(x + (int)b->xoff, y + (int)b->yoff, b->x1 - b->x0, b->y1 - b->y0, body_text, tint, &xgu_texture_boundary);
        x += (int)b->xadvance;
    }
}
