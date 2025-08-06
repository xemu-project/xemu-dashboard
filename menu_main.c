#include <SDL.h>
#include <nxdk/format.h>
#include <stdio.h>
#include <windows.h>

#include "main.h"

static void xbox_exit(void);
static void xbox_flush_cache(void);
void menu_system_info_activate(void);
void menu_eeprom_activate(void);
void menu_install_dash_activate(void);

static MenuItem menu_items[] = {
    {"Main Menu", NULL},
    {"System Info", menu_system_info_activate},
    {"EEPROM Settings", menu_eeprom_activate},
    {"Clear Cache", xbox_flush_cache},
    {"Install", menu_install_dash_activate},
    {"Reboot", xbox_exit}};

static Menu menu = {
    .item = menu_items,
    .item_count = sizeof(menu_items) / sizeof(MenuItem),
    .selected_index = 0,
    .scroll_offset = 0,
    .close_callback = NULL};

void main_menu_activate(void)
{
    menu_push(&menu);
}

static void xbox_exit(void)
{
    SDL_Event event = {
        .type = SDL_QUIT,
    };
    SDL_PushEvent(&event);
}

static void recursive_empty_folder(const char *folderPath)
{
    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s\\*", folderPath);

    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(searchPath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        // Ignore `.` and `..`
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
            continue;
        }

        char filePath[MAX_PATH];
        snprintf(filePath, MAX_PATH, "%s\\%s", folderPath, findData.cFileName);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recursively delete subdirectory
            recursive_empty_folder(filePath);
            RemoveDirectory(filePath);
        } else {
            // Delete file
            if (!DeleteFile(filePath)) {
                printf("Failed to delete file: %s\r\n", filePath);
            } else {
                printf("Deleted file: %s\r\n", filePath);
            }
        }

    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
}

static void xbox_flush_cache(void)
{
    // Source: https://github.com/dracc/NevolutionX/blob/master/Sources/wipeCache.cpp
    const char *partitions[] = {
        "\\Device\\Harddisk0\\Partition3", // "X"
        "\\Device\\Harddisk0\\Partition4", // "Y"
        "\\Device\\Harddisk0\\Partition5"  // "Z"
    };
    const int partition_cnt = sizeof(partitions) / sizeof(partitions[0]);
    for (int i = 0; i < partition_cnt; i++) {
        if (nxFormatVolume(partitions[i], 0) == false) {
            printf("ERROR: Could not format %s\r\n", partitions[i]);
        } else {
            printf("Formatted %s ok!\r\n", partitions[i]);
        }
    }

    // Delete E:\CACHE too
    recursive_empty_folder("E:\\CACHE");

    static MenuItem status_message_items[] = {
        {"Cache cleared successfully", NULL}};

    static Menu status_message = {
        .item = status_message_items,
        .item_count = sizeof(status_message_items) / sizeof(status_message_items),
        .selected_index = 0,
        .scroll_offset = 0,
        .close_callback = NULL};

    menu_push(&status_message);
}
