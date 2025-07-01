#pragma once

#include <SDL.h>
#include <assert.h>
#include <nanoprintf/nanoprintf.h>
#include <stb/stb_truetype.h>
#include <windows.h>

#include "renderer.h"

#define WINDOW_WIDTH       640
#define WINDOW_HEIGHT      480
#define BODY_FONT_SIZE     26.0f
#define HEADER_FONT_SIZE   48.0f
#define ITEM_PADDING       10
#define Y_MARGIN           20
#define X_MARGIN           20
#define FONT_BITMAP_WIDTH  512
#define FONT_BITMAP_HEIGHT 512
#define HEADER_Y (Y_MARGIN + (int)HEADER_FONT_SIZE)
#define MENU_Y (HEADER_Y + (int)BODY_FONT_SIZE + ITEM_PADDING)
#define FOOTER_Y (WINDOW_HEIGHT - Y_MARGIN - (int)BODY_FONT_SIZE)

void _putc(int c, void *ctx);

// Use my own printf implementation
#undef printf
#define printf(...) npf_pprintf(_putc, NULL, __VA_ARGS__);

// Use my own snprintf implementation
#undef snprintf
#define snprintf(...) npf_snprintf(__VA_ARGS__)

#define DVD_LAUNCH_EVENT (SDL_USEREVENT + 0)

typedef struct
{
    char *label;
    void (*callback)(void);
} MenuItem;

typedef struct
{
    MenuItem *item;
    int item_count;
    int selected_index;
    int scroll_offset;
} Menu;

extern HANDLE text_render_mutex;

void menu_push(Menu *menu);
Menu *menu_peak(void);
Menu *menu_pop(void);

void network_initialise(void);
void network_get_status(char *ip_address_buffer, char buffer_length);

void autolaunch_dvd_runner(void);
void main_menu_activate(void);

void text_draw(stbtt_bakedchar *cdata, xgu_texture_t *body_text, const char *text, int x, int y, const xgu_texture_tint_t *tint);
int text_calculate_width(stbtt_bakedchar *cdata, const char *text);
