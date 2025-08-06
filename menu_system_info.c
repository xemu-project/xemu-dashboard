#include <stdlib.h>
#include <windows.h>
#include <xbox_eeprom.h>
#include <xboxkrnl/xboxkrnl.h>

#include "main.h"

#define MAX_LINES      64
#define MAX_CHARACTERS 12024
static char char_pool[MAX_CHARACTERS];
static int pool_offset;

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

static MenuItem menu_items[MAX_LINES];
static Menu menu = {
    .item = menu_items,
    .item_count = sizeof(menu_items) / sizeof(MenuItem),
    .selected_index = 0,
    .scroll_offset = 0,
    .close_callback = NULL};

static void push_line(int line, void *callback, const char *format, ...)
{
    char *text_buffer = &char_pool[pool_offset];

    va_list args;
    va_start(args, format);
    int written = vsnprintf(text_buffer, MAX_CHARACTERS - pool_offset, format, args);
    va_end(args);

    assert(line < MAX_LINES);
    menu_items[line].label = text_buffer;
    menu_items[line].callback = callback;

    pool_offset += written + 1;
    assert(pool_offset < MAX_CHARACTERS);
}

static void callback_stub(void)
{
}

static void update_system_info(void)
{
    int line = 0;
    pool_offset = 0;
    memset(menu_items, 0, sizeof(menu_items));

    push_line(line++, NULL, "System Information");

    // Kernel version
    push_line(line++, callback_stub, "Kernel Version: %d.%02d.%d.%02d\n", XboxKrnlVersion.Major, XboxKrnlVersion.Minor, XboxKrnlVersion.Build, XboxKrnlVersion.Qfe);

    // System clocks
    ULONG nvclk_reg = *((volatile ULONG *)0xFD680500);
    ULONG gpu_clock = 16667 * ((nvclk_reg & 0xFF00) >> 8);
    gpu_clock /= 1 << ((nvclk_reg & 0x70000) >> 16);
    gpu_clock /= nvclk_reg & 0xFF;
    gpu_clock /= 1000;
    LARGE_INTEGER cpu_clock;
    QueryPerformanceFrequency(&cpu_clock);
    push_line(line++, callback_stub, "CPU Clock: %llu MHz, GPU Clock: %lu MHz\n", cpu_clock.QuadPart / 1000000, gpu_clock);

    // Free space on partitions
    push_line(line++, callback_stub, "Free Space on Partitions:");
    const char *drive_letters[] = {"C:\\", "E:\\", "F:\\", "G:\\", "X:\\", "Y:\\", "Z:\\"};
    for (unsigned int i = 0; i < sizeof(drive_letters) / sizeof(drive_letters[0]); i++) {
        ULARGE_INTEGER bytes_available, total_bytes;
        if (GetDiskFreeSpaceEx(drive_letters[i], &bytes_available, &total_bytes, NULL)) {
            ULONGLONG total_mib = total_bytes.QuadPart / 1024ULL;
            ULONGLONG mib_available = bytes_available.QuadPart / 1024ULL;
            ULONGLONG percent_free = (mib_available * 100ULL) / total_mib;
            push_line(line++, callback_stub, "    %c: %llu / %llu MiB (%llu%%)", drive_letters[i][0], mib_available, total_mib, percent_free);
        }
    }

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
    push_line(line++, callback_stub, "Encoder: %s", encoder_string);

    // SMC Version
    char smc_version[4];
    HalWriteSMBusValue(0x20, 0x01, FALSE, 0x00); // Reset SMC version counter
    HalReadSMBusValue(0x20, 0x01, FALSE, (ULONG *)&smc_version[0]);
    HalReadSMBusValue(0x20, 0x01, FALSE, (ULONG *)&smc_version[1]);
    HalReadSMBusValue(0x20, 0x01, FALSE, (ULONG *)&smc_version[2]);
    smc_version[3] = '\0';
    push_line(line++, callback_stub, "SMC Version: %s", smc_version);

    // MCPX Version
    PCI_SLOT_NUMBER SlotNumber;
    SlotNumber.u.AsULONG = 0;
    SlotNumber.u.bits.DeviceNumber = 1;
    SlotNumber.u.bits.FunctionNumber = 0;
    uint32_t mcpx_revision = 0;
    HalReadWritePCISpace(0, SlotNumber.u.AsULONG, 0x08, &mcpx_revision, sizeof(mcpx_revision), FALSE);
    push_line(line++, callback_stub, "MCPX Version: 0x%02x", mcpx_revision & 0xFF);

    // MAC Address and Serial Number
    UCHAR mac_address[0x06], serial_number[0x0D];
    ULONG type;
    ExQueryNonVolatileSetting(XC_FACTORY_SERIAL_NUMBER, &type, &serial_number, sizeof(serial_number), NULL);
    ExQueryNonVolatileSetting(XC_FACTORY_ETHERNET_ADDR, &type, &mac_address, sizeof(mac_address), NULL);
    push_line(line++, callback_stub, "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
              mac_address[0], mac_address[1], mac_address[2],
              mac_address[3], mac_address[4], mac_address[5]);
    push_line(line++, callback_stub, "Serial Number: %s", serial_number);

    // Online key
    ULONG online_key[4];
    ExQueryNonVolatileSetting(XC_FACTORY_ONLINE_KEY, &type, &online_key, sizeof(online_key), NULL);
    push_line(line++, callback_stub, "Online Key: %08x%08x%08x%08x",
              __builtin_bswap32(online_key[0]), __builtin_bswap32(online_key[1]), __builtin_bswap32(online_key[2]), __builtin_bswap32(online_key[3]));

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
    push_line(line++, callback_stub, "AV Pack Type: %s", av_pack_string);

    // Xbox Game Region
    ULONG game_region;
    ExQueryNonVolatileSetting(XC_FACTORY_GAME_REGION, &type, &game_region, sizeof(game_region), NULL);
    const char *game_region_string;
    switch (game_region) {
        case XBOX_EEPROM_XBOX_REGION_NA:
            game_region_string = "North America";
            break;
        case XBOX_EEPROM_XBOX_REGION_JP:
            game_region_string = "Japan";
            break;
        case XBOX_EEPROM_XBOX_REGION_EU:
            game_region_string = "Europe and Australia";
            break;
        case XBOX_EEPROM_XBOX_REGION_MANUFACTURING:
            game_region_string = "Manufacturing";
            break;
        default:
            game_region_string = "Unknown";
    }
    push_line(line++, callback_stub, "Game Region: %s", game_region_string);

    menu.item_count = line;
}

void menu_system_info_activate(void)
{
    update_system_info();
    menu_push(&menu);
}
