#include <nxdk/path.h>
#include <windows.h>

#include "main.h"

static HANDLE dvd_autolaunch_thread_handle = NULL;

static WINAPI DWORD dvd_autolaunch_thread(LPVOID param)
{
    (void) param;
    char target_path[MAX_PATH];
    while (1) {
        Sleep(1000);

        // Check if we have media inserted in the DVD ROM
        ULONG tray_state;
        HalReadSMCTrayState(&tray_state, NULL);

        // Check for xbox media
        if (tray_state == 0x60) {
            // Prevent recursive launch by checking the current xbe wasn't launched from the DVD itself
            nxGetCurrentXbeNtPath(target_path);

            // Remove the filename from the path
            char *last_lash = strrchr(target_path, '\\');
            *last_lash = '\0';

            if (strcmp(target_path, "\\Device\\CdRom0") == 0) {
                continue;
            }

            // Push an event to the main loop to trigger the autolaunch
            SDL_Event event = {
                .type = DVD_LAUNCH_EVENT,
            };
            SDL_PushEvent(&event);
        }
    }
    return 0;
}

void autolaunch_dvd_runner(void)
{
    dvd_autolaunch_thread_handle = CreateThread(NULL, 0, dvd_autolaunch_thread, NULL, 0, NULL);
}
