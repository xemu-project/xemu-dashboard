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

// Use my own vsnprintf implementation
#undef vsnprintf
#define vsnprintf(...) npf_vsnprintf(__VA_ARGS__)

#define DVD_LAUNCH_EVENT (SDL_USEREVENT + 0)

// FIXME probably should be in nxdk
#define AUDIO_FLAG_ENCODING_AC3     0x00010000
#define AUDIO_FLAG_ENCODING_DTS     0x00020000
#define AUDIO_FLAG_ENCODING_MASK    (AUDIO_FLAG_ENCODING_AC3 | AUDIO_FLAG_ENCODING_DTS)
#define AUDIO_FLAG_CHANNEL_MONO     0x00000001
#define AUDIO_FLAG_CHANNEL_SURROUND 0x00000002
#define AUDIO_FLAG_CHANNEL_MASK     (AUDIO_FLAG_CHANNEL_MONO | AUDIO_FLAG_CHANNEL_SURROUND)
#define AV_REGION_NTSC              0x00400100
#define AV_REGION_NTSCJ             0x00400200
#define AV_REGION_PAL               0x00800300
#define AV_REGION_PALM              0x00400400
#define GAME_REGION_NA          0x00000001
#define GAME_REGION_JAPAN       0x00000002
#define GAME_REGION_EUROPE     0x00000004
#define GAME_REGION_MANUFACTURING 0x80000000

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

int downloader_init(void);
void downloader_deinit(void);
int downloader_check_update(char latest_version[64 + 1], char latest_sha[64 + 1], char **download_url);
int downloader_download_update(char *download_url, void **mem, char **downloaded_data, int *downloaded_size, char downloaded_sha[64 + 1]);

void autolaunch_dvd_runner(void);
void main_menu_activate(void);

void text_draw(stbtt_bakedchar *cdata, xgu_texture_t *body_text, const char *text, int x, int y, const xgu_texture_tint_t *tint);
int text_calculate_width(stbtt_bakedchar *cdata, const char *text);
