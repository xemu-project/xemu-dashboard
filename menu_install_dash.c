#include <hal/video.h>
#include <stdlib.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <nxdk/path.h>

#include "main.h"

static void install_dashboard(void)
{
    static MenuItem status_message_items[] = {
        {"Installed successfully", NULL},
        {"Error installing", NULL}};

    static Menu status_message = {
        .item = NULL,
        .item_count = 1,
        .selected_index = 0,
        .scroll_offset = 0};

    const char *src = "Q:\\default.xbe";
    const char *dst = "C:\\xboxdash.xbe";
    const char *bak = "C:\\xboxdash.xbe.bak";

    SetFileAttributesA(dst, FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesA(bak, FILE_ATTRIBUTE_NORMAL);

    // Create a backup
    CopyFile(dst, bak, FALSE);

    if (CopyFile(src, dst, FALSE)) {
        status_message.item = &status_message_items[0];
        menu_push(&status_message);
    } else {
        status_message.item = &status_message_items[1];
        menu_push(&status_message);
    }
}

static void restore_backuo(void)
{
    static MenuItem status_message_items[] = {
        {"Backup restored successfully", NULL},
        {"Error restoring backup", NULL}};

    static Menu status_message = {
        .item = NULL,
        .item_count = 1,
        .selected_index = 0,
        .scroll_offset = 0};

    const char *src = "C:\\xboxdash.xbe.bak";
    const char *dst = "C:\\xboxdash.xbe";

    SetFileAttributesA(dst, FILE_ATTRIBUTE_NORMAL);

    if (CopyFile(src, dst, FALSE)) {
        status_message.item = &status_message_items[0];
        menu_push(&status_message);
    } else {
        status_message.item = &status_message_items[1];
        menu_push(&status_message);
    }
}

static void cancel(void)
{
    menu_pop();
}

static MenuItem menu_items[] = {
    {"Dashboard Installer", NULL},
    {"Restore backup", restore_backuo},
    {"Install and overwrite \"C:\\xboxdash.xbe\"?", install_dashboard},
    {"Cancel", cancel}};

static Menu menu = {
    .item = menu_items,
    .item_count = sizeof(menu_items) / sizeof(MenuItem),
    .selected_index = 0,
    .scroll_offset = 0};

void menu_install_dash_activate(void)
{
    // If this xbe is launched from C:/xboxdash.xbe we disable the installer option
    char target_path[MAX_PATH];
    nxGetCurrentXbeNtPath(target_path);
    printf("target_path: %s\n", target_path);
    if (strcmp(target_path, "\\Device\\Harddisk0\\Partition2\\xboxdash.xbe") == 0) {
        menu_items[2].callback = NULL;
    }

    menu_push(&menu);
}
