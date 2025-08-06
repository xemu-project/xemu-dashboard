#include <hal/video.h>
#include <nxdk/path.h>
#include <stdlib.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include "main.h"
static void install_dashboard_from_online(void);
static void install_dashboard_from_local(void);

static void restore_backuo(void)
{
    static MenuItem status_message_items[] = {
        {"Backup restored successfully", NULL},
        {"Error restoring backup", NULL}};

    static Menu status_message = {
        .item = NULL,
        .item_count = 1,
        .selected_index = 0,
        .scroll_offset = 0,
        .close_callback = NULL};

    const char *src = "C:\\xboxdash.xbe.bak";
    const char *dst = "C:\\xboxdash.xbe";

    SetFileAttributesA(dst, FILE_ATTRIBUTE_NORMAL);

    if (CopyFile(src, dst, FALSE)) {
        status_message.item = &status_message_items[0];
    } else {
        status_message.item = &status_message_items[1];
    }
    menu_push(&status_message);
}

static void cancel(void)
{
    menu_pop();
}

// These text fields dynamically change so we use a separate variable for them
static char *default_offline_install_text = "Install running file? (default.xbe)";
static char *default_online_install_text = "Check online for latest file?";
#define OFFLINE_INSTALL_LINE 2
#define ONLINE_INSTALL_LINE  3

static MenuItem menu_items[] = {
    {"Dashboard Installer (This will replace xboxdash.xbe)", NULL},
    {"Restore backup", restore_backuo},
    {"Install running file?", install_dashboard_from_local},
    {"Check online for latest file?", install_dashboard_from_online},
    {"Cancel", cancel}};

static Menu menu = {
    .item = menu_items,
    .item_count = sizeof(menu_items) / sizeof(MenuItem),
    .selected_index = 0,
    .scroll_offset = 0,
    .close_callback = NULL};

void menu_install_dash_activate(void)
{
    menu_items[OFFLINE_INSTALL_LINE].label = default_offline_install_text;
    menu_items[ONLINE_INSTALL_LINE].label = default_online_install_text;

    // If this xbe is launched from C:/xboxdash.xbe we disable this installer option
    char target_path[MAX_PATH];
    nxGetCurrentXbeNtPath(target_path);
    if (strcmp(target_path, "\\Device\\Harddisk0\\Partition2\\xboxdash.xbe") == 0) {
        menu_items[OFFLINE_INSTALL_LINE].callback = NULL;
    }

    menu_push(&menu);
}

static HANDLE downloader_thread_handle = NULL;
static HANDLE downloader_semaphore = NULL;
static void trigger_online_action()
{
    ReleaseSemaphore(downloader_semaphore, 1, NULL);
}

static inline void update_downloader_status(char *status, void *callback)
{
    WaitForSingleObject(text_render_mutex, INFINITE);
    menu_items[ONLINE_INSTALL_LINE].label = status;
    menu_items[ONLINE_INSTALL_LINE].callback = callback;
    ReleaseMutex(text_render_mutex);
}

static DWORD WINAPI downloader_update_thread(LPVOID lpThreadParameter)
{
    (void)lpThreadParameter;

    char status_text[128];
    char latest_version[64 + 1] = {0};
    char latest_sha[64 + 1] = {0};
    char *download_url = NULL;

    downloader_deinit();
    downloader_init();

    if (downloader_check_update(latest_version, latest_sha, &download_url) == 0) {
        // Check that the version is different from the current version
        if (strcmp(latest_version, GIT_VERSION) == 0) {
            update_downloader_status("You are already on the latest version", install_dashboard_from_online);
            return 0;
        }

        // Update the menu item to show the latest version
        snprintf(status_text, sizeof(status_text), "Download update (%s)?", latest_version);
        update_downloader_status(status_text, trigger_online_action);

        // Wait for user to trigger the download. We sleep for a short time to check if the user has aborted the update
        while (1) {
            if (WaitForSingleObject(downloader_semaphore, 100) == WAIT_OBJECT_0) {
                break; // Exit the loop if we got the signal to download
            }
            // Check if the menu has changed, if so we exit the thread
            if (menu_peak() != &menu) {
                update_downloader_status(default_online_install_text, install_dashboard_from_online);
                return 0;
            }
        }

        update_downloader_status("Downloading update...", NULL);

        char *downloaded_data = NULL;
        int downloaded_size = 0;
        char downloaded_sha[64 + 1];
        void *mem;
        if (downloader_download_update(download_url, &mem, &downloaded_data, &downloaded_size, downloaded_sha) == 0) {
            // Compare the downloaded SHA with the expected SHA
            if (strcmp(downloaded_sha, latest_sha) != 0) {
                update_downloader_status("Hash mismatch! - Aborting download", NULL);
            }

            update_downloader_status("Update downloaded successfully! - Install?", trigger_online_action);

            // Wait for user to trigger the download. We sleep for a short time to check if the user has aborted the update
            while (1) {
                if (WaitForSingleObject(downloader_semaphore, 100) == WAIT_OBJECT_0) {
                    break; // Exit the loop if we got the signal to download
                }
                // Check if the menu has changed, if so we exit the thread
                if (menu_peak() != &menu) {
                    free(mem);
                    update_downloader_status(default_online_install_text, install_dashboard_from_online);
                    return 0;
                }
            }

            update_downloader_status("Installing update...", NULL);

            // Prepare to install the update
            const char *dst = "C:\\xboxdash.xbe";
            const char *bak = "C:\\xboxdash.xbe.bak";
            SetFileAttributesA(dst, FILE_ATTRIBUTE_NORMAL);
            SetFileAttributesA(bak, FILE_ATTRIBUTE_NORMAL);

            // Create a backup
            CopyFile(dst, bak, FALSE);

            HANDLE file_handle = CreateFileA(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file_handle != INVALID_HANDLE_VALUE) {
                DWORD bytes_written;
                if (WriteFile(file_handle, downloaded_data, downloaded_size, &bytes_written, NULL)) {
                    update_downloader_status("Update installed successfully!", install_dashboard_from_online);
                } else {
                    update_downloader_status("Error writing update file", install_dashboard_from_online);
                }
                CloseHandle(file_handle);
            } else {
                update_downloader_status("Error opening file for writing", install_dashboard_from_online);
            }
            free(mem);
        } else {
            update_downloader_status("Failed to download update", install_dashboard_from_online);
        }

    } else {
        update_downloader_status("Failed to check for updates", install_dashboard_from_online);
    }
    return 0;
}

static void install_dashboard_from_online(void)
{
    // Prevent starting the downloader thread multiple times
    if (downloader_thread_handle != NULL) {
        WaitForSingleObject(downloader_thread_handle, INFINITE);
        CloseHandle(downloader_thread_handle);
        CloseHandle(downloader_semaphore);
    }

    downloader_semaphore = CreateSemaphore(NULL, 0, 1, NULL);
    if (downloader_semaphore == NULL) {
        printf("Failed to create downloader semaphore.\n");
        return;
    }

    downloader_thread_handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)downloader_update_thread, NULL, CREATE_SUSPENDED, NULL);
    if (downloader_thread_handle == NULL) {
        printf("Failed to create downloader thread.\n");
        return;
    }

    // Update the menu items to show the checking status
    update_downloader_status("Checking for updates...", NULL);

    ResumeThread(downloader_thread_handle);
}

static void install_dashboard_from_local(void)
{
    const char *src = "Q:\\default.xbe";
    const char *dst = "C:\\xboxdash.xbe";
    const char *bak = "C:\\xboxdash.xbe.bak";

    SetFileAttributesA(dst, FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesA(bak, FILE_ATTRIBUTE_NORMAL);

    // Create a backup
    CopyFile(dst, bak, FALSE);

    if (CopyFile(src, dst, FALSE)) {
        menu_items[OFFLINE_INSTALL_LINE].label = "Installed successfully";
    } else {
        menu_items[OFFLINE_INSTALL_LINE].label = "Error installing";
    }
}
