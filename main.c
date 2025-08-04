/* xemu_menu.c
 * A simple SDL2-based menu using SDL_GameController and stb_truetype.
 */

#include <SDL.h>
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <nxdk/mount.h>
#include <nxdk/path.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xgu/xgu.h>
#include <xgu/xgux.h>

#include "main.h"
#include "support_renderer.h"

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

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rect_pack.h>

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

static Font body_font;
static Font header_font;

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
    int result;
    XVideoSetMode(WINDOW_WIDTH, WINDOW_HEIGHT, 32, REFRESH_DEFAULT);

    nxUnmountDrive('D');
    nxMountDrive('D', "\\Device\\CdRom0");
    nxMountDrive('C', "\\Device\\Harddisk0\\Partition2\\");
    nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
    nxMountDrive('X', "\\Device\\Harddisk0\\Partition3\\");
    nxMountDrive('Y', "\\Device\\Harddisk0\\Partition4\\");
    nxMountDrive('Z', "\\Device\\Harddisk0\\Partition5\\");
    nxMountDrive('F', "\\Device\\Harddisk0\\Partition6\\");

    // Mount the root is active xbe to Q:
    {
        char targetPath[MAX_PATH];
        nxGetCurrentXbeNtPath(targetPath);
        *(strrchr(targetPath, '\\') + 1) = '\0';
        nxMountDrive('Q', targetPath);
    }

    network_initialise();
    autolaunch_dvd_runner();

    SDL_Init(SDL_INIT_GAMECONTROLLER);

    // Pseudo-random number generator seed
    LARGE_INTEGER seed;
    KeQuerySystemTime(&seed);
    srand(seed.LowPart);

    // Create font texture for body text
    result = text_create(RobotoMono_Regular, BODY_FONT_SIZE, (const int[][2]){{32, 127}}, 1, &body_font);
    assert(result == 0);

    // Create font texture for header text
    result = text_create(UbuntuMono_Regular, HEADER_FONT_SIZE, (const int[][2]){{32, 127}}, 1, &header_font);
    assert(result == 0);

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

    // If storage space is below some % on any partition, show a warning
    char warning_text[128];
    const char *drive_letters[] = {"C:\\", "E:\\", "F:\\", "G:\\"};
    Menu menu_warning = {
        .item = (MenuItem *)&(MenuItem){warning_text, NULL},
        .item_count = 1,
        .selected_index = 0,
        .scroll_offset = 0,
        .close_callback = NULL};
    int char_offset = 0;
    for (unsigned int i = 0; i < sizeof(drive_letters) / sizeof(drive_letters[0]); i++) {
        ULARGE_INTEGER total_bytes, free_bytes;
        if (GetDiskFreeSpaceEx(drive_letters[i], &free_bytes, &total_bytes, NULL)) {
            ULONGLONG percent_free = ((free_bytes.QuadPart / 1024ULL) * 100ULL) / (total_bytes.QuadPart / 1024ULL);
            if (percent_free < 5ULL) {
                if (char_offset == 0) {
                    char_offset += snprintf(&warning_text[char_offset], sizeof(warning_text) - char_offset, "Warning\nLow storage space on partitions:");
                }
                char_offset += snprintf(&warning_text[char_offset], sizeof(warning_text) - char_offset, " %c,", drive_letters[i][0]);
            }
        }
    }
    if (char_offset > 0) {
        // Remove the last comma
        warning_text[char_offset - 1] = '\0';
        menu_push(&menu_warning);
    }

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
                    while (current_menu->item[current_menu->selected_index].callback == NULL) {
                        current_menu->selected_index = (current_menu->selected_index + 1) % current_menu->item_count;
                        if (current_menu->item[current_menu->selected_index].callback != NULL) {
                            break;
                        }
                        if (current_menu->selected_index == 0) {
                            current_menu->selected_index = 1 % current_menu->item_count;
                            break;
                        }
                    }
                } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                    current_menu->selected_index = (current_menu->selected_index - 1 + current_menu->item_count) % current_menu->item_count;

                    // Skip if not a selectable item on the way up
                    while (current_menu->item[current_menu->selected_index].callback == NULL) {
                        current_menu->selected_index = (current_menu->selected_index - 1 + current_menu->item_count) % current_menu->item_count;
                        if (current_menu->item[current_menu->selected_index].callback != NULL) {
                            break;
                        }
                        if (current_menu->selected_index == 0) {
                            current_menu->selected_index = 1 % current_menu->item_count;
                            break;
                        }
                    }
                } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A || e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    if (current_menu->item[current_menu->selected_index].callback) {
                        current_menu->item[current_menu->selected_index].callback();
                    }
                } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B || e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    if (menu_stack_top > 0) {
                        Menu *popped_menu = menu_pop();
                        if (popped_menu->close_callback) {
                            popped_menu->close_callback();
                        }
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
            0 + x_offset, 640 + x_offset, 0, 480};
        x_offset += 0.25f;
        if (x_offset > 127) {
            x_offset = 0;
        }
        renderer_draw_textured_rectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, background_texture, NULL, &boundary);

        // Render the header text
        text_draw(&header_font, "xemu", X_MARGIN, HEADER_Y, &highlight_color);

        char menu_text_buffer[64];

        // Render the Xbox system local time
        SYSTEMTIME systemtime;
        GetLocalTime(&systemtime);
        snprintf(menu_text_buffer, sizeof(menu_text_buffer), "%04d-%02d-%02d %02d:%02d:%02d", systemtime.wYear, systemtime.wMonth, systemtime.wDay,
                 systemtime.wHour, systemtime.wMinute, systemtime.wSecond);
        text_draw(&body_font, menu_text_buffer, WINDOW_WIDTH - X_MARGIN - text_calculate_width(&body_font, menu_text_buffer),
                  Y_MARGIN + BODY_FONT_SIZE, &info_color);

        // Render footer text
        snprintf(menu_text_buffer, sizeof(menu_text_buffer), "Waiting for a Xbox DVD");
        text_draw(&body_font, menu_text_buffer, WINDOW_WIDTH - X_MARGIN - text_calculate_width(&body_font, menu_text_buffer),
                  FOOTER_Y, &text_color);

        char network_status[32];
        network_get_status(network_status, sizeof(network_status));
        snprintf(menu_text_buffer, sizeof(menu_text_buffer), "FTP Server - %s", network_status);
        text_draw(&body_font, menu_text_buffer, WINDOW_WIDTH - X_MARGIN - text_calculate_width(&body_font, menu_text_buffer),
                  FOOTER_Y + BODY_FONT_SIZE, &text_color);

        // Render the actual menu items
        render_menu();

        // Show me
        renderer_present();
    }

    cleanup();
    HalReturnToFirmware(HalRebootRoutine);
    return 0;
}

void menu_push(Menu *menu)
{
    menu_stack[++menu_stack_top] = menu;
    if (menu->selected_index == 0) {
        menu->selected_index = 1 % menu->item_count;
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
    Menu *current_menu = menu_peak();
    assert(current_menu != NULL);

    // The first line of each menu is reserved for the menu title
    text_draw(&body_font, current_menu->item[0].label, X_MARGIN, MENU_Y, &header_color);

    renderer_set_scissor(0, MENU_Y + ITEM_PADDING, WINDOW_WIDTH, FOOTER_Y - (int)BODY_FONT_SIZE - MENU_Y);

    const int selected_y_bottom = MENU_Y + current_menu->selected_index * (int)body_font.line_height +
                                  current_menu->scroll_offset + ITEM_PADDING;
    const int selected_y_top = selected_y_bottom - (int)body_font.line_height;

    // Start at the bottom of the first menu item
    int y = MENU_Y + (int)body_font.line_height;

    // Scroll the selected item to be in view
    if (selected_y_top < MENU_Y + ITEM_PADDING) {
        current_menu->scroll_offset += (MENU_Y + ITEM_PADDING - selected_y_top + 3) >> 2;
    } else if (selected_y_bottom > FOOTER_Y - (int)BODY_FONT_SIZE) {
        current_menu->scroll_offset -= (selected_y_bottom - (FOOTER_Y - (int)BODY_FONT_SIZE) + 3) >> 2;
    }

    for (int i = 1; i < current_menu->item_count; ++i) {
        xgu_texture_tint_t color;
        if (current_menu->item[i].callback == NULL) {
            color = info_color;
        } else if (i == current_menu->selected_index) {
            color = highlight_color;
        } else {
            color = text_color;
        }

        WaitForSingleObject(text_render_mutex, INFINITE);
        text_draw(&body_font, current_menu->item[i].label, X_MARGIN,
                  y + current_menu->scroll_offset, &color);
        ReleaseMutex(text_render_mutex);
        y += body_font.line_height;
    }

    renderer_set_scissor(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
}

void usbh_core_deinit();
void nvnetdrv_stop(void);
static void cleanup(void)
{
    if (controller) {
        SDL_GameControllerClose(controller);
    }
    if (body_font.texture) {
        texture_destroy(body_font.texture);
    }
    if (header_font.texture) {
        texture_destroy(header_font.texture);
    }
    if (background_texture) {
        texture_destroy(background_texture);
    }
    pb_kill();
    nvnetdrv_stop();
    usbh_core_deinit();
    debugClearScreen();
    SDL_Quit();
}
