#include <stdlib.h>
#include <windows.h>

#include "support_renderer.h"

static inline void shader_init();
static inline void unlit_shader_apply();
static inline void texture_shader_apply();
static inline uint32_t npot2pot(uint32_t num);

static uint32_t *p;
static int texture_combiner_active = 0;
static const xgu_texture_t *active_texture = NULL;

xgu_texture_t *texture_create(const void *texture_data, uint32_t width, uint32_t height, XguTexFormatColor format)
{
    xgu_texture_t *xgu_texture = malloc(sizeof(xgu_texture_t));
    xgu_texture->tex_height = height;
    xgu_texture->tex_width = width;
    xgu_texture->data_height = npot2pot(height);
    xgu_texture->data_width = npot2pot(width);

    xgu_texture->format = format;
    switch (format) {
        case XGU_TEXTURE_FORMAT_A8:
            xgu_texture->bytes_per_pixel = 1;
            break;
        case XGU_TEXTURE_FORMAT_R5G6B5:
            xgu_texture->bytes_per_pixel = 2;
            break;
        case XGU_TEXTURE_FORMAT_R8G8B8A8:
        case XGU_TEXTURE_FORMAT_A8B8G8R8:
            xgu_texture->bytes_per_pixel = 4;
            break;
        default:
            free(xgu_texture);
            return NULL;
    }

    size_t allocation_size = xgu_texture->data_height * xgu_texture->data_width * xgu_texture->bytes_per_pixel;
    xgu_texture->data = MmAllocateContiguousMemoryEx(allocation_size, 0, 0xFFFFFFFF, 0, PAGE_WRITECOMBINE | PAGE_READWRITE);
    if (xgu_texture->data == NULL) {
        free(xgu_texture);
        return NULL;
    }
    xgu_texture->data_physical_address = (uint8_t *)MmGetPhysicalAddress(xgu_texture->data);

    const uint8_t *source8 = (const uint8_t *)texture_data;
    const uint32_t data_stride = xgu_texture->data_width * xgu_texture->bytes_per_pixel;
    const uint32_t texture_stride = width * xgu_texture->bytes_per_pixel;
    for (uint32_t i = 0; i < height; i++) {
        memcpy(&xgu_texture->data[i * data_stride], &source8[i * texture_stride], texture_stride);
    }

    // Maybe faster? Don't know
    MmSetAddressProtect(xgu_texture->data, allocation_size, PAGE_READONLY);
    return xgu_texture;
}

void texture_destroy(xgu_texture_t *texture)
{
    MmFreeContiguousMemory(texture->data);
    free(texture);
}

void renderer_deinit(void)
{
}

void renderer_initialise(void)
{
    pb_init();
    pb_show_front_screen();

    const float m_identity[4 * 4] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};

    p = pb_begin();

    shader_init();
    unlit_shader_apply();
    texture_combiner_active = false;

    p = xgu_set_blend_enable(p, true);
    p = xgu_set_depth_test_enable(p, false);
    p = xgu_set_blend_func_sfactor(p, XGU_FACTOR_SRC_ALPHA);
    p = xgu_set_blend_func_dfactor(p, XGU_FACTOR_ONE_MINUS_SRC_ALPHA);
    p = xgu_set_depth_func(p, XGU_FUNC_LESS_OR_EQUAL);

    p = xgu_set_skin_mode(p, XGU_SKIN_MODE_OFF);
    p = xgu_set_normalization_enable(p, false);
    p = xgu_set_lighting_enable(p, false);
    p = xgu_set_clear_rect_vertical(p, 0, pb_back_buffer_height());
    p = xgu_set_clear_rect_horizontal(p, 0, pb_back_buffer_width());

    for (int i = 0; i < XGU_TEXTURE_COUNT; i++) {
        p = xgu_set_texgen_s(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texgen_t(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texgen_r(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texgen_q(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texture_matrix_enable(p, i, false);
        p = xgu_set_texture_matrix(p, i, m_identity);
    }

    for (int i = 0; i < XGU_WEIGHT_COUNT; i++) {
        p = xgu_set_model_view_matrix(p, i, m_identity);
        p = xgu_set_inverse_model_view_matrix(p, i, m_identity);
    }

    p = xgu_set_transform_execution_mode(p, XGU_FIXED, XGU_RANGE_MODE_PRIVATE);
    p = xgu_set_projection_matrix(p, m_identity);
    p = xgu_set_composite_matrix(p, m_identity);
    p = xgu_set_viewport_offset(p, 0.0f, 0.0f, 0.0f, 0.0f);
    p = xgu_set_viewport_scale(p, 1.0f, 1.0f, 1.0f, 1.0f);

    p = xgu_set_scissor_rect(p, false, 0, 0, pb_back_buffer_width() - 1, pb_back_buffer_height() - 1);

    pb_end(p);
}

void renderer_start(void)
{
    pb_reset();
    pb_target_back_buffer();
    p = pb_begin();
    p = xgu_set_color_clear_value(p, 0xff000000);
    p = xgu_clear_surface(p, XGU_CLEAR_Z | XGU_CLEAR_STENCIL | XGU_CLEAR_COLOR);
    pb_end(p);
}

void renderer_set_scissor(int x, int y, int width, int height)
{
    p = pb_begin();
    p = xgu_set_scissor_rect(p, false, x, y, width, height);
    pb_end(p);
}

void renderer_draw_rectangle(int x, int y, int width, int height, const xgu_texture_tint_t *tint)
{
    p = pb_begin();

    if (texture_combiner_active == 1) {
        unlit_shader_apply();
        texture_combiner_active = 0;
    }

    p = xgux_set_color4ub(p, tint->r, tint->g, tint->b, tint->a);

    p = xgu_begin(p, XGU_QUADS);
    p = xgu_vertex4f(p, x, y, 1, 1);
    p = xgu_vertex4f(p, x + width, y, 1, 1);
    p = xgu_vertex4f(p, x + width, y + height, 1, 1);
    p = xgu_vertex4f(p, x, y + height, 1, 1);
    p = xgu_end(p);

    pb_end(p);
}

void renderer_draw_textured_rectangle(int x, int y, int width, int height, const xgu_texture_t *texture, const xgu_texture_tint_t *tint, const xgu_texture_boundary_t *boundary)
{
    p = pb_begin();
    
    if (texture_combiner_active == 0) {
        texture_shader_apply();
        texture_combiner_active = 1;
    }

    if (active_texture != texture) {
        p = xgu_set_texture_offset(p, 0, texture->data_physical_address);
        p = xgu_set_texture_format(p, 0, 2, false, XGU_SOURCE_COLOR, 2, texture->format, 1,
                                   __builtin_ctz(texture->data_width), __builtin_ctz(texture->data_height), 0);
        p = xgu_set_texture_address(p, 0, XGU_CLAMP_TO_EDGE, false, XGU_CLAMP_TO_EDGE, false, XGU_CLAMP_TO_EDGE, false, false);
        p = xgu_set_texture_control0(p, 0, true, 0, 0);
        p = xgu_set_texture_control1(p, 0, texture->data_width * texture->bytes_per_pixel);
        p = xgu_set_texture_image_rect(p, 0, texture->data_width, texture->data_height);
        p = xgu_set_texture_filter(p, 0, 0, XGU_TEXTURE_CONVOLUTION_GAUSSIAN, 2, 2, false, false, false, false);
        active_texture = texture;
    }

    if (tint) {
        p = xgux_set_color4ub(p, tint->r, tint->g, tint->b, tint->a);
    } else {
        p = xgux_set_color4ub(p, 255, 255, 255, 255);
    }

    const float dest_x = (float)x;
    const float dest_y = (float)y;
    const float dest_w = (float)width;
    const float dest_h = (float)height;

    const float s0 = (boundary) ? boundary->s0 : 0.0f;
    const float s1 = (boundary) ? boundary->s1 : texture->tex_width;
    const float t0 = (boundary) ? boundary->t0 : 0.0f;
    const float t1 = (boundary) ? boundary->t1 : texture->tex_height;

    p = xgu_begin(p, XGU_TRIANGLE_STRIP);

    p = xgux_set_texcoord3f(p, 0, s0, t0, 1);
    p = xgu_vertex4f(p, dest_x, dest_y, 1, 1);
    p = xgux_set_texcoord3f(p, 0, s1, t0, 1);
    p = xgu_vertex4f(p, dest_x + dest_w, dest_y, 1, 1);
    p = xgux_set_texcoord3f(p, 0, s0, t1, 1);
    p = xgu_vertex4f(p, dest_x, dest_y + dest_h, 1, 1);
    p = xgux_set_texcoord3f(p, 0, s1, t1, 1);
    p = xgu_vertex4f(p, dest_x + dest_w, dest_y + dest_h, 1, 1);

    p = xgu_end(p);

    pb_end(p);
}

void renderer_present(void)
{
    pb_wait_for_vbl();

    while (pb_busy()) {
        Sleep(0);
    }

    while (pb_finished()) {
        Sleep(0);
    }
}

static inline uint32_t npot2pot(uint32_t num)
{
    uint32_t msb;
    __asm__("bsr %1, %0" : "=r"(msb) : "r"(num));

    if ((1 << msb) == num) {
        return num;
    }

    return 1 << (msb + 1);
}

// clang-format off

#undef MASK
#define MASK(mask, val) (((val) << (__builtin_ffs(mask)-1)) & (mask))

static inline void shader_init()
{
   pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT,
    MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE1, 0)
    | MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE2, 0)
    | MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE3, 0));
    p += 2;
    pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM,
        MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE0, NV097_SET_SHADER_STAGE_PROGRAM_STAGE0_PROGRAM_NONE)
        | MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE1, NV097_SET_SHADER_STAGE_PROGRAM_STAGE1_PROGRAM_NONE)
        | MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE2, NV097_SET_SHADER_STAGE_PROGRAM_STAGE2_PROGRAM_NONE)
        | MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE3, NV097_SET_SHADER_STAGE_PROGRAM_STAGE3_PROGRAM_NONE));
    p += 2;

    pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + 0 * 4,
    MASK(NV097_SET_COMBINER_COLOR_ICW_A_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_MAP, 0x6)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_MAP, 0x1)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_MAP, 0x0)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_MAP, 0x0));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_COLOR_OCW + 0 * 4,
        MASK(NV097_SET_COMBINER_COLOR_OCW_AB_DST, 0x4)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_CD_DST, 0x0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_SUM_DST, 0x0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_MUX_ENABLE, 0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_AB_DOT_ENABLE, 0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_CD_DOT_ENABLE, 0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_OP, NV097_SET_COMBINER_COLOR_OCW_OP_NOSHIFT));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + 0 * 4,
        MASK(NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_MAP, 0x6)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_MAP, 0x1)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_MAP, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_MAP, 0x0));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_ALPHA_OCW + 0 * 4,
        MASK(NV097_SET_COMBINER_ALPHA_OCW_AB_DST, 0x4)
        | MASK(NV097_SET_COMBINER_ALPHA_OCW_CD_DST, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_OCW_SUM_DST, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_OCW_MUX_ENABLE, 0)
        | MASK(NV097_SET_COMBINER_ALPHA_OCW_OP, NV097_SET_COMBINER_ALPHA_OCW_OP_NOSHIFT));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_CONTROL,
        MASK(NV097_SET_COMBINER_CONTROL_FACTOR0, NV097_SET_COMBINER_CONTROL_FACTOR0_SAME_FACTOR_ALL)
        | MASK(NV097_SET_COMBINER_CONTROL_FACTOR1, NV097_SET_COMBINER_CONTROL_FACTOR1_SAME_FACTOR_ALL)
        | MASK(NV097_SET_COMBINER_CONTROL_ITERATION_COUNT, 1));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW0,
        MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_A_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_A_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_A_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_B_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_B_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_C_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_C_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_INVERSE, 0));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW1,
        MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_E_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_E_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_E_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_F_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_F_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_F_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_ALPHA, 1) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_SPECULAR_CLAMP, 0));
    p += 2;
}

static inline void unlit_shader_apply ()
{
    p = pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
    p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, 0);

    p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + 0 * 4,
    MASK(NV097_SET_COMBINER_COLOR_ICW_A_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_MAP, 0x6)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_MAP, 0x1)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_MAP, 0x0)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_MAP, 0x0));

    p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + 0 * 4,
        MASK(NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_MAP, 0x6)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_MAP, 0x1)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_MAP, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_MAP, 0x0));
}

static inline void texture_shader_apply ()
{
    p = pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
    p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE0, NV097_SET_SHADER_STAGE_PROGRAM_STAGE0_2D_PROJECTIVE));

    p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + 0 * 4,
    MASK(NV097_SET_COMBINER_COLOR_ICW_A_SOURCE, 0x8) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_MAP, 0x6)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_B_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_MAP, 0x6)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_MAP, 0x0)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_MAP, 0x0));
  
    p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + 0 * 4,
        MASK(NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE, 0x8) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_MAP, 0x6)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_MAP, 0x6)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_MAP, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_MAP, 0x0));
}
// clang-format on