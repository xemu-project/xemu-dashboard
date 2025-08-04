#pragma once

#include <SDL.h>
#include <assert.h>
#include <nanoprintf/nanoprintf.h>
#include <stb/stb_truetype.h>
#include <windows.h>

#include "support_renderer.h"

#define WINDOW_WIDTH       640
#define WINDOW_HEIGHT      480
#define BODY_FONT_SIZE     26.0f
#define HEADER_FONT_SIZE   48.0f
#define ITEM_PADDING       10
#define Y_MARGIN           20
#define X_MARGIN           20
#define FONT_BITMAP_WIDTH  128
#define FONT_BITMAP_HEIGHT 512
#define HEADER_Y           (Y_MARGIN + (int)HEADER_FONT_SIZE)
#define MENU_Y             (HEADER_Y + (int)BODY_FONT_SIZE + ITEM_PADDING)
#define FOOTER_Y           (WINDOW_HEIGHT - Y_MARGIN - (int)BODY_FONT_SIZE)

void _putc(int c, void *ctx);

// Use my own printf implementation
#undef printf
#define printf(...) npf_pprintf(_putc, NULL, __VA_ARGS__);

// Use my own snprintf implementation
#undef snprintf
#define snprintf(...) npf_snprintf(__VA_ARGS__)

// Use my own vsnprintf implementation
#undef vsnprintf
#define vsnprintf(...) npf_vsnprintf(__VA_ARGS__)

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

typedef struct font
{
    stbtt_fontinfo font_info;
    xgu_texture_t *texture;
    stbtt_pack_range *range;
    int range_count;
    float line_height;
    float scale;
} Font;

extern HANDLE text_render_mutex;

void menu_push(Menu *menu);
Menu *menu_peak(void);
Menu *menu_pop(void);

void network_initialise(void);
void network_get_status(char *ip_address_buffer, char buffer_length);

int downloader_init(void);
void downloader_deinit(void);
int downloader_check_update(char latest_version[64 + 1], char latest_sha[64 + 1], char **download_url);
int downloader_download_update(char *download_url, void **mem, char **downloaded_data, int *downloaded_size, char downloaded_sha[64 + 1]);

void autolaunch_dvd_runner(void);
void main_menu_activate(void);

int text_create(const unsigned char *ttf_data, float font_size, const int(*range)[2], int range_count, Font *font);
void text_draw(Font *font, const char *text, int x, int y, const xgu_texture_tint_t *tint);
int text_calculate_width(Font *font, const char *text);
