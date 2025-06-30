#pragma once

#include <stdint.h>
#include <xgu/xgu.h>
#include <xgu/xgux.h>

typedef struct xgu_texture
{
    uint32_t data_width;
    uint32_t data_height;
    uint32_t tex_width;
    uint32_t tex_height;
    uint32_t bytes_per_pixel;
    XguTexFormatColor format;
    uint8_t *data;
    uint8_t *data_physical_address;
} xgu_texture_t;

typedef struct xgu_texture_boundary
{
    float s0;
    float s1;
    float t0;
    float t1;
} xgu_texture_boundary_t;

typedef struct xgu_texture_tint
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} xgu_texture_tint_t;

void renderer_initialise(void);
void renderer_start(void);
void renderer_set_scissor(int x, int y, int width, int height);
void renderer_draw_rectangle(int x, int y, int width, int height, const xgu_texture_tint_t *tint);
void renderer_draw_textured_rectangle(int x, int y, int width, int height,
                                      const xgu_texture_t *texture, const xgu_texture_tint_t *tint, const xgu_texture_boundary_t *boundary);
void renderer_present(void);

xgu_texture_t *texture_create(const void *texture_data, uint32_t width, uint32_t height, XguTexFormatColor format);
void texture_destroy(xgu_texture_t *texture);