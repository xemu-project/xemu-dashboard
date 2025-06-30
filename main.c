/* xemu_menu.c
 * A simple SDL2-based menu using SDL_GameController and stb_truetype.
 */

#include <SDL.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <nxdk/mount.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xgu/xgu.h>
#include <xgu/xgux.h>

#include "main.h"
#include "renderer.h"

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS       1
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS   1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS       1
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS       1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS      0
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS   0
#define NANOPRINTF_USE_ALT_FORM_FLAG                 1
#define NANOPRINTF_SNPRINTF_SAFE_EMPTY_STRING_ON_OVERFLOW
#include <nanoprintf/nanoprintf.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#define __MSC_VER _MSC_VER
#undef _MSC_VER
#include <stb/stb_image.h>
#define _MSC_VER __MSC_VER

static void render_menu(void);
static void cleanup(void);

static xgu_texture_t *background_texture;
static const xgu_texture_tint_t highlight_color = {16, 124, 16, 255};
static const xgu_texture_tint_t text_color = {255, 255, 255, 255};
static const xgu_texture_tint_t header_color = {100, 100, 100, 255};
static const xgu_texture_tint_t info_color = {128, 128, 128, 255};

static xgu_texture_t *body_text;
static stbtt_bakedchar body_text_cdata[96];
static unsigned char body_text_bitmap[FONT_BITMAP_WIDTH * FONT_BITMAP_HEIGHT];

static xgu_texture_t *header_text;
static stbtt_bakedchar header_text_cdata[96];
static unsigned char header_text_bitmap[FONT_BITMAP_WIDTH * FONT_BITMAP_HEIGHT];

static SDL_GameController *controller = NULL;

static Menu *menu_stack[64];
static int menu_stack_top = -1;

// If we are updating visible text from a different thread, use this mutex to synchronise access
// to the text rendering functions.
HANDLE text_render_mutex;

void _putc(int c, void *ctx)
{
    (void)ctx;
    DbgPrint("%c", c);
    if (c == '\n') {
        DbgPrint("\r");
    }
}

static const unsigned char RobotoMono_Regular[] = {
    #embed "assets/RobotoMono-Regular.ttf"
};

static const unsigned char UbuntuMono_Regular[] = {
    #embed "assets/UbuntuMono-Regular.ttf"
};

static const unsigned char background_png[] = {
    #embed "assets/background.png"
};


int main(void)
{
    XVideoSetMode(WINDOW_WIDTH, WINDOW_HEIGHT, 32, REFRESH_DEFAULT);

    nxUnmountDrive('D');
    nxMountDrive('D', "\\Device\\CdRom0");
    nxMountDrive('C', "\\Device\\Harddisk0\\Partition2\\");
    nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
    nxMountDrive('X', "\\Device\\Harddisk0\\Partition3\\");
    nxMountDrive('Y', "\\Device\\Harddisk0\\Partition4\\");
    nxMountDrive('Z', "\\Device\\Harddisk0\\Partition5\\");
    nxMountDrive('F', "\\Device\\Harddisk0\\Partition6\\");

    network_initialise();
    autolaunch_dvd_runner();

    SDL_Init(SDL_INIT_GAMECONTROLLER);

    // Create font texture for body text
    stbtt_BakeFontBitmap(RobotoMono_Regular, 0, BODY_FONT_SIZE, body_text_bitmap, FONT_BITMAP_WIDTH, FONT_BITMAP_HEIGHT, 32, 96, body_text_cdata);
    body_text = texture_create(body_text_bitmap, FONT_BITMAP_WIDTH, FONT_BITMAP_HEIGHT, XGU_TEXTURE_FORMAT_A8);
    assert(body_text);

    // Create font texture for header text
    stbtt_BakeFontBitmap(UbuntuMono_Regular, 0, HEADER_FONT_SIZE, header_text_bitmap, FONT_BITMAP_WIDTH, FONT_BITMAP_HEIGHT, 32, 96, header_text_cdata);
    header_text = texture_create(header_text_bitmap, FONT_BITMAP_WIDTH, FONT_BITMAP_HEIGHT, XGU_TEXTURE_FORMAT_A8);
    assert(header_text);

    // Create background texture
    int width, height, channels;
    unsigned char *background_data = stbi_load_from_memory((const unsigned char *)background_png,
                                                           sizeof(background_png), &width, &height, &channels, 4);
    assert(background_data != NULL);
    background_texture = texture_create(background_data, width, height, XGU_TEXTURE_FORMAT_A8B8G8R8);
    stbi_image_free(background_data);

    // Text updates can come from worker threads so we protect the rending of the text by a mutex
    text_render_mutex = CreateMutex(NULL, FALSE, NULL);

    renderer_initialise();

    // Main menu always exists
    main_menu_activate();

    // Main running loop
    bool running = true;
    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                if (controller == NULL) {
                    controller = SDL_GameControllerOpen(e.cdevice.which);
                }
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (controller &&
                    e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) {
                    SDL_GameControllerClose(controller);
                    controller = NULL;
                }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                Menu *current_menu = menu_peak();
                assert(current_menu != NULL);
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                    current_menu->selected_index = (current_menu->selected_index + 1) % current_menu->item_count;

                    // Skip if not a selectable item on the way down
                    if (current_menu->item[current_menu->selected_index].callback == NULL) {
                        current_menu->selected_index = (current_menu->selected_index + 1) % current_menu->item_count;
                    }
                } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                    current_menu->selected_index = (current_menu->selected_index - 1 + current_menu->item_count) % current_menu->item_count;

                    // Skip if not a selectable item on the way up
                    if (current_menu->item[current_menu->selected_index].callback == NULL) {
                        current_menu->selected_index = (current_menu->selected_index - 1 + current_menu->item_count) % current_menu->item_count;
                    }
                } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                    if (current_menu->item[current_menu->selected_index].callback) {
                        current_menu->item[current_menu->selected_index].callback();
                    }
                } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    if (menu_stack_top > 0) {
                        menu_pop();
                    }
                }
            } else if (e.type == DVD_LAUNCH_EVENT) {
                cleanup();
                XLaunchXBE("\\Device\\CdRom0\\default.xbe");
            }
        }

        // Prepare the renderer
        renderer_start();

        // Render the background
        static float x_offset = 0;
        xgu_texture_boundary_t boundary = {
            0 + x_offset, 640 + x_offset, 0, 480
        };
        x_offset += 0.25f;
        if (x_offset > 127) {
            x_offset = 0;
        }
        renderer_draw_textured_rectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, background_texture, NULL, &boundary);

        // Render the header text
        text_draw(header_text_cdata, header_text, "xemu", X_MARGIN, Y_MARGIN + (int)BODY_FONT_SIZE, &highlight_color);

        // Render footer text
        char ip_address[32];
        char footer_text[64];
        snprintf(footer_text, sizeof(footer_text), "Waiting for a Xbox DVD");
        text_draw(body_text_cdata, body_text, footer_text, WINDOW_WIDTH - X_MARGIN - text_calculate_width(body_text_cdata, footer_text),
                  WINDOW_HEIGHT - Y_MARGIN - (int)BODY_FONT_SIZE, &text_color);

        SYSTEMTIME systemtime;
        GetLocalTime(&systemtime);
        snprintf(footer_text, sizeof(footer_text), "%04d-%02d-%02d %02d:%02d:%02d", systemtime.wYear, systemtime.wMonth, systemtime.wDay,
                 systemtime.wHour, systemtime.wMinute, systemtime.wSecond);
        text_draw(body_text_cdata, body_text, footer_text, WINDOW_WIDTH - X_MARGIN - text_calculate_width(body_text_cdata, footer_text),
                  Y_MARGIN + (int)BODY_FONT_SIZE, &info_color);

        network_get_ip_address(ip_address, sizeof(ip_address));
        snprintf(footer_text, sizeof(footer_text), "FTP Server - %s", ip_address);
        text_draw(body_text_cdata, body_text, footer_text, WINDOW_WIDTH - X_MARGIN - text_calculate_width(body_text_cdata, footer_text),
                  WINDOW_HEIGHT - Y_MARGIN, &text_color);

        // Render the build version
        text_draw(body_text_cdata, body_text, GIT_VERSION, X_MARGIN, WINDOW_HEIGHT - Y_MARGIN, &info_color);

        // Render the actual menu items
        render_menu();

        // Show me
        renderer_present();
    }

    cleanup();
    return 0;
}

void menu_push(Menu *menu)
{
    menu_stack[++menu_stack_top] = menu;
    if (menu->selected_index == 0) {
        menu->selected_index = 1;
    }
}

Menu *menu_pop(void)
{
    Menu *menu = NULL;
    if (menu_stack_top >= 0) {
        menu = menu_stack[menu_stack_top];
        menu_stack_top--;
    }
    return menu;
}

Menu *menu_peak(void)
{
    Menu *current_menu = NULL;
    if (menu_stack_top >= 0) {
        current_menu = menu_stack[menu_stack_top];
    }
    return current_menu;
}

static void render_menu(void)
{
    const int item_height = (int)BODY_FONT_SIZE + ITEM_PADDING;

    int y = Y_MARGIN + item_height + item_height;
    Menu *current_menu = menu_peak();
    assert(current_menu != NULL);

    const int visible_height = WINDOW_HEIGHT - y - Y_MARGIN;
    const int visible_items = visible_height / item_height;

    int scroll_offset = 0;
    if (current_menu->selected_index < scroll_offset) {
        scroll_offset = current_menu->selected_index;
    } else if (current_menu->selected_index >= scroll_offset + visible_items) {
        scroll_offset = current_menu->selected_index - visible_items + 1;
    }

    for (int i = 0; i < visible_items; ++i) {
        int item_index = scroll_offset + i;
        if (item_index >= current_menu->item_count) {
            break;
        }

        xgu_texture_tint_t color = text_color;
        if (current_menu->item[item_index].callback == NULL) {
            color = (item_index == 0) ? header_color : text_color;
        } else if (item_index == current_menu->selected_index) {
            color = highlight_color;
        }

        WaitForSingleObject(text_render_mutex, INFINITE);
        text_draw(body_text_cdata, body_text, current_menu->item[item_index].label, X_MARGIN, y, &color);
        ReleaseMutex(text_render_mutex);
        y += item_height;
    }
}

void usbh_core_deinit();
void nvnetdrv_stop(void);
static void cleanup(void)
{
    if (controller) {
        SDL_GameControllerClose(controller);
    }
    if (body_text) {
        texture_destroy(body_text);
    }
    if (background_texture) {
        texture_destroy(background_texture);
    }
    pb_kill();
    nvnetdrv_stop();
    usbh_core_deinit();
    SDL_Quit();
}
