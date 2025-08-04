#include <nxdk/path.h>
#include <windows.h>

#include "main.h"

static HANDLE dvd_autolaunch_thread_handle = NULL;

static WINAPI DWORD dvd_autolaunch_thread(LPVOID param)
{
    (void)param;

    char target_path[MAX_PATH];
    nxGetCurrentXbeNtPath(target_path);

    // Remove the filename from the path
    char *last_slash = strrchr(target_path, '\\');
    if (last_slash) {
        *last_slash = '\0';
    }

    while (1) {
        Sleep(1000);

        // Check if we have media inserted in the DVD ROM
        ULONG tray_state;
        HalReadSMCTrayState(&tray_state, NULL);

        // Check for xbox media
        if (tray_state == 0x60) {
            if (strcmp(target_path, "\\Device\\CdRom0") == 0) {
                continue;
            }

            // Push an event to the main loop to trigger the autolaunch
            SDL_Event event = {
                .type = DVD_LAUNCH_EVENT,
            };
            SDL_PushEvent(&event);
        } else if (tray_state == 0x10) {
            // If anything changes with the tray state, reset the target path as it must be a new XISO
            target_path[0] = '\0';
        }
    }
    return 0;
}

void autolaunch_dvd_runner(void)
{
    dvd_autolaunch_thread_handle = CreateThread(NULL, 0, dvd_autolaunch_thread, NULL, 0, NULL);
}

const char *dvd_get_tray_status()
{
    ULONG tray_state;
    NTSTATUS status = HalReadSMCTrayState(&tray_state, NULL);
    if (!NT_SUCCESS(status)) {
        return "Error reading tray state";
    }

    switch (tray_state & 0x70) {
        case 0x00:
            return "Closed";
        case 0x10:
            return "Open";
        case 0x20:
            return "Unloading";
        case 0x30:
            return "Opening";
        case 0x40:
            return "No Media";
        case 0x50:
            return "Closing";
        case 0x60:
            return "XBOX DVD Detected";
        default:
            return "Unknown State";
    }
}