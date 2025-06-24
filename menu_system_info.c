#include <stdlib.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include "main.h"

typedef struct _PCI_SLOT_NUMBER
{
    union
    {
        struct
        {
            ULONG DeviceNumber : 5;
            ULONG FunctionNumber : 3;
            ULONG Reserved : 24;
        } bits;
        ULONG AsULONG;
    } u;
} PCI_SLOT_NUMBER, *PPCI_SLOT_NUMBER;

HANDLE system_info_thread_handle = NULL;
static char system_info_buffer[512];

static MenuItem menu_items[] = {
    {"System Information", NULL},
    {system_info_buffer, NULL}};

static Menu menu = {
    .item = menu_items,
    .item_count = sizeof(menu_items) / sizeof(MenuItem),
    .selected_index = 0};

static DWORD WINAPI update_system_info(LPVOID lpParameter)
{
    (void) lpParameter;
    char intermediate_buffer[512];
    while (1) {
        int index = 0;
        // Kernel version
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "Kernel Version: %d.%02d.%d.%02d\n", XboxKrnlVersion.Major, XboxKrnlVersion.Minor, XboxKrnlVersion.Build, XboxKrnlVersion.Qfe);

        // System clocks
        ULONG nvclk_reg = *((volatile ULONG *)0xFD680500);
        ULONG gpu_clock = 16667 * ((nvclk_reg & 0xFF00) >> 8);
        gpu_clock /= 1 << ((nvclk_reg & 0x70000) >> 16);
        gpu_clock /= nvclk_reg & 0xFF;
        gpu_clock /= 1000;
        LARGE_INTEGER cpu_clock;
        QueryPerformanceFrequency(&cpu_clock);
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "CPU Clock: %llu MHz, GPU Clock: %lu Mhz\n", cpu_clock.QuadPart / 1000000, gpu_clock);

        // Free space on partitions
        const char *drive_letters[] = {"C:\\", "E:\\", "F:\\", "G:\\", "X:\\", "Y:\\", "Z:\\"};
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "Free Space: ");
        for (unsigned int i = 0; i < sizeof(drive_letters) / sizeof(drive_letters[0]); i++) {
            ULARGE_INTEGER bytes_available, total_bytes;
            if (GetDiskFreeSpaceEx(drive_letters[i], &bytes_available, &total_bytes, NULL)) {
                ULONGLONG total_mib = total_bytes.QuadPart / 1024ULL;
                ULONGLONG mib_available = bytes_available.QuadPart / 1024ULL;
                index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "%c (%llu%%) ", drive_letters[i][0],
                                  (mib_available * 100ULL) / total_mib);
            }
        }
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "\n");

        // Active encoder
        static const char *encoder_string = NULL;
        ULONG encoder_check;
        if (HalReadSMBusValue(0xd4, 0x00, FALSE, &encoder_check) == 0) {
            encoder_string = "Focus FS454";
        } else if (HalReadSMBusValue(0xe0, 0x00, FALSE, &encoder_check) == 0) {
            encoder_string = "Microsoft Xcalibur";
        } else if (HalReadSMBusValue(0x8a, 0x00, FALSE, &encoder_check) == 0) {
            encoder_string = "Conexant CX25871";
        } else {
            encoder_string = "Unknown";
        }
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "Encoder: %s\n", encoder_string);

        // SMC Version
        char smc_version[4];
        HalWriteSMBusValue(0x20, 0x01, FALSE, 0x00); // Reset SMC version counter
        HalReadSMBusValue(0x20, 0x01, FALSE, (ULONG *)&smc_version[0]);
        HalReadSMBusValue(0x20, 0x01, FALSE, (ULONG *)&smc_version[1]);
        HalReadSMBusValue(0x20, 0x01, FALSE, (ULONG *)&smc_version[2]);
        smc_version[3] = '\0';
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "SMC Version: %s, ", smc_version);

        // MCPX Version
        PCI_SLOT_NUMBER SlotNumber;
        SlotNumber.u.AsULONG = 0;
        SlotNumber.u.bits.DeviceNumber = 1;
        SlotNumber.u.bits.FunctionNumber = 0;
        uint32_t mcpx_revision = 0;
        HalReadWritePCISpace(0, SlotNumber.u.AsULONG, 0x08, &mcpx_revision, sizeof(mcpx_revision), FALSE);
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "MCPX Version: 0x%02x\n", mcpx_revision & 0xFF);

        // MAC Address and Serial Number
        UCHAR mac_address[0x06], serial_number[0x0D];
        ULONG type;
        ExQueryNonVolatileSetting(XC_FACTORY_SERIAL_NUMBER, &type, &serial_number, sizeof(serial_number), NULL);
        ExQueryNonVolatileSetting(XC_FACTORY_ETHERNET_ADDR, &type, &mac_address, sizeof(mac_address), NULL);
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
                          mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "Serial Number: %s\n", serial_number);

        // Online key
        ULONG online_key[4];
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "Online Key: ");
        ExQueryNonVolatileSetting(XC_FACTORY_ONLINE_KEY, &type, &online_key, sizeof(online_key), NULL);
        for (unsigned int i = 0; i < sizeof(online_key); i++) {
            index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "%02x", ((unsigned char *)&online_key)[i]);
        }
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "\n");

        // Connected AV Pack
        ULONG av_pack_type;
        HalReadSMBusValue(0x20, 0x04, FALSE, &av_pack_type);
        const char *av_pack_string;
        switch (av_pack_type) {
            case 0x00:
                av_pack_string = "SCART";
                break;
            case 0x01:
                av_pack_string = "HDTV";
                break;
            case 0x02:
                av_pack_string = "VGA";
                break;
            case 0x03:
                av_pack_string = "RFU";
                break;
            case 0x04:
                av_pack_string = "S-Video";
                break;
            case 0x05:
                av_pack_string = "Undefined";
                break;
            case 0x06:
                av_pack_string = "Composite";
                break;
            case 0x07:
                av_pack_string = "Missing/Disconnected";
                break;
            default:
                av_pack_string = "Unknown";
        }
        index += snprintf(&intermediate_buffer[index], sizeof(intermediate_buffer) - index, "AV Pack Type: %s\n", av_pack_string);

        WaitForSingleObject(text_render_mutex, INFINITE);
        memcpy(system_info_buffer, intermediate_buffer, sizeof(system_info_buffer));
        ReleaseMutex(text_render_mutex);

        // Wait some time to avoid thrashing
        int delay = 5000;
        while (delay > 0) {
            if (menu_peak() != &menu) {
                printf("System info menu is not active, stopping update thread.\n");
                return 0;
            }
            Sleep(10);
            delay -= 10;
        }
    }
    return 0;
}

void menu_system_info_activate(void)
{
    menu_push(&menu);

    if (system_info_thread_handle != NULL) {
        WaitForSingleObject(system_info_thread_handle, INFINITE);
        CloseHandle(system_info_thread_handle);
        system_info_thread_handle = NULL;
    }

    system_info_thread_handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)update_system_info, NULL, 0, NULL);
}
