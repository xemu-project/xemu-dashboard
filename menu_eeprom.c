#include <hal/video.h>
#include <stdlib.h>
#include <windows.h>
#include <xbox_eeprom.h>
#include <xboxkrnl/xboxkrnl.h>

#include "main.h"

#define MAX_LINES      32
#define MAX_CHARACTERS 1024
static char char_pool[MAX_CHARACTERS];
static int pool_offset;

static int dirty = 0;
static int closing = 0;
static char eeprom_version;

#define EEPROM_SMBUS_ADDRESS     0xA8
#define AUDIO_FLAG_ENCODING_MASK (XBOX_EEPROM_AUDIO_SETTINGS_ENABLE_AC3 | XBOX_EEPROM_AUDIO_SETTINGS_ENABLE_DTS)
#define AUDIO_FLAG_CHANNEL_MASK  (XBOX_EEPROM_AUDIO_SETTINGS_MONO | XBOX_EEPROM_AUDIO_SETTINGS_SURROUND)

static xbox_eeprom_t encrypted_eeprom;
static xbox_eeprom_t eeprom;

static void update_eeprom_text(void);
static void query_eeprom(void);

static void restore_backup()
{
    HANDLE eeprom_file = CreateFileA("E:\\eeprom.bin", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (eeprom_file != INVALID_HANDLE_VALUE) {
        unsigned char eeprom_data[256];
        DWORD bytes_read;
        ReadFile(eeprom_file, eeprom_data, sizeof(eeprom_data), &bytes_read, NULL);
        CloseHandle(eeprom_file);

        for (unsigned int i = 0; i < sizeof(eeprom_data); i++) {
            HalWriteSMBusValue(EEPROM_SMBUS_ADDRESS, i, FALSE, eeprom_data[i]);
        }

        query_eeprom();
        update_eeprom_text();
        dirty = 0;

        static MenuItem menu_item = {"Backup restored successfully", NULL};
        static Menu menu = {
            .item = &menu_item,
            .item_count = 1,
            .selected_index = 0,
            .scroll_offset = 0,
            .close_callback = NULL};
        menu_push(&menu);
    }
}

static void apply_settings(void)
{
    // First create a full backup of the EEPROM to E:\\eeprom.bin
    SetFileAttributesA("E:\\eeprom.bin", FILE_ATTRIBUTE_NORMAL);
    HANDLE eeprom_file = CreateFileA("E:\\eeprom.bin", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (eeprom_file != INVALID_HANDLE_VALUE) {
        DWORD bytes_written;
        WriteFile(eeprom_file, &encrypted_eeprom, sizeof(encrypted_eeprom), &bytes_written, NULL);
        CloseHandle(eeprom_file);
    }

    xbox_eeprom_encrypt(0x0A, &eeprom, &encrypted_eeprom);

    // Write the encrypted EEPROM back to the Xbox
    for (unsigned int i = 0; i < sizeof(encrypted_eeprom); i++) {
        HalWriteSMBusValue(EEPROM_SMBUS_ADDRESS, i, FALSE, ((unsigned char *)&encrypted_eeprom)[i]);
    }

    dirty = 0;

    if (closing) {
        menu_pop();
        closing = 0;
    }
    query_eeprom();
    update_eeprom_text();
}

static void cancel(void)
{
    closing = 0;
    menu_pop();
}

static void eeprom_menu_close_callback(void)
{
    if (dirty) {
        static MenuItem menu_items[] = {
            {"Do you wish to apply unsaved EEPROM changes?", NULL},
            {"Yes", apply_settings},
            {"No", cancel},
        };
        static Menu menu = {
            .item = menu_items,
            .item_count = 3,
            .selected_index = 0,
            .scroll_offset = 0,
            .close_callback = NULL};
        menu_push(&menu);
        closing = 1;
    }
}

static void increment_xbox_region(void)
{
    static const ULONG regions[4] = {XBOX_EEPROM_XBOX_REGION_NA, XBOX_EEPROM_XBOX_REGION_JP,
                                     XBOX_EEPROM_XBOX_REGION_EU, XBOX_EEPROM_XBOX_REGION_MANUFACTURING};
    int index = 0;
    for (int i = 0; i < 4; i++) {
        if (regions[i] == eeprom.encrypted.xbox_region) {
            index = i;
            index = (index + 1) % 4;
            break;
        }
    }
    eeprom.encrypted.xbox_region = regions[index];
    dirty = 1;
    update_eeprom_text();
}

static void increment_dvd_region(void)
{
    eeprom.user.dvd_region = (eeprom.user.dvd_region + 1) % 7;
    dirty = 1;
    update_eeprom_text();
}

static void increment_language(void)
{
    eeprom.user.language = (eeprom.user.language + 1) % 10;
    dirty = 1;
    update_eeprom_text();
}

static void increment_video_region(void)
{
    static const ULONG regions[4] = {XBOX_EEPROM_VIDEO_STANDARD_NTSC_M, XBOX_EEPROM_VIDEO_STANDARD_PAL,
                                     XBOX_EEPROM_VIDEO_STANDARD_NTSC_J, XBOX_EEPROM_VIDEO_STANDARD_PAL_M};
    int index = 0;
    for (int i = 0; i < 4; i++) {
        if (regions[i] == eeprom.factory.video_standard) {
            index = i;
            index = (index + 1) % 4;
            break;
        }
    }
    eeprom.factory.video_standard = regions[index];
    dirty = 1;
    update_eeprom_text();
}

static void video_increment_aspect_ratio(void)
{
    if (eeprom.user.video_settings & VIDEO_WIDESCREEN) {
        eeprom.user.video_settings &= ~VIDEO_WIDESCREEN;
        eeprom.user.video_settings |= VIDEO_LETTERBOX;
    } else if (eeprom.user.video_settings & VIDEO_LETTERBOX) {
        eeprom.user.video_settings &= ~VIDEO_LETTERBOX;
    } else {
        eeprom.user.video_settings |= VIDEO_WIDESCREEN;
    }
    dirty = 1;
    update_eeprom_text();
}

static void video_increment_refresh_rate(void)
{
    ULONG index = (eeprom.user.video_settings >> 22) & 0x03;
    index = (index + 1) % 4;
    eeprom.user.video_settings &= ~(VIDEO_50Hz | VIDEO_60Hz);
    eeprom.user.video_settings |= index << 22;
    dirty = 1;
    update_eeprom_text();
}

static void video_toggle_480p(void)
{
    eeprom.user.video_settings ^= VIDEO_MODE_480P;
    dirty = 1;
    update_eeprom_text();
}

static void video_toggle_720p(void)
{
    eeprom.user.video_settings ^= VIDEO_MODE_720P;
    dirty = 1;
    update_eeprom_text();
}

static void video_toggle_1080i(void)
{
    eeprom.user.video_settings ^= VIDEO_MODE_1080I;
    dirty = 1;
    update_eeprom_text();
}

static void audio_increment_channel(void)
{
    ULONG index = (eeprom.user.audio_settings & AUDIO_FLAG_CHANNEL_MASK) >> 0;
    index = (index + 1) % 3;
    eeprom.user.audio_settings &= ~AUDIO_FLAG_CHANNEL_MASK;
    eeprom.user.audio_settings |= index << 0;
    dirty = 1;
    update_eeprom_text();
}

static void audio_increment_encoding(void)
{
    ULONG index = (eeprom.user.audio_settings & AUDIO_FLAG_ENCODING_MASK) >> 16;
    index = (index + 1) % 4;
    eeprom.user.audio_settings &= ~AUDIO_FLAG_ENCODING_MASK;
    eeprom.user.audio_settings |= index << 16;
    dirty = 1;
    update_eeprom_text();
}

static void mac_address_generate(void)
{
    // One Xbox consoles the first byte of the MAC address is always 0x00,
    // the second and third byte seem to be the same few patterns.
    // The last three bytes are random.
    eeprom.factory.mac_address[0] = 0x00;
    int r = rand() % 3;
    if (r == 0) {
        eeprom.factory.mac_address[1] = 0x50;
        eeprom.factory.mac_address[2] = 0xf2;
    } else if (r == 1) {
        eeprom.factory.mac_address[1] = 0x0d;
        eeprom.factory.mac_address[2] = 0x3a;
    } else {
        eeprom.factory.mac_address[1] = 0x12;
        eeprom.factory.mac_address[2] = 0x5a;
    }
    eeprom.factory.mac_address[3] = rand() % 256;
    eeprom.factory.mac_address[4] = rand() % 256;
    eeprom.factory.mac_address[5] = rand() % 256;
    dirty = 1;
    update_eeprom_text();
}

static void increment_timezone_bios(void)
{
    // The time zone offset is in minutes, so we can increment it by 30 minutes at a time
    // to account for time zones with 30 minute offsets.
    eeprom.user.timezone_bias -= 30;
    if (eeprom.user.timezone_bias < -720) { // -12 hours
        eeprom.user.timezone_bias = 720;    // wrap around to 12 hours
    }
    dirty = 1;
    update_eeprom_text();
}

static void generate_serial_number(void)
{
    char production_line = rand() % 10;                       // 0-9
    int week_production = (rand() | (rand() << 16)) % 200000; // 0-1999999
    int production_year = (rand() % 5) + 1;                   // 1-5
    int production_week = (rand() % 52) + 1;                  // 1-52
    char factory_id = ((char[]){2, 3, 5, 6})[rand() % 4];

    char serial_number[13];
    snprintf(serial_number, sizeof(serial_number), "%1d%06d%1d%02d%02d",
             production_line, week_production, production_year, production_week, factory_id);

    memcpy(eeprom.factory.serial_number, serial_number, sizeof(eeprom.factory.serial_number));

    dirty = 1;
    update_eeprom_text();
}

static MenuItem menu_items[MAX_LINES];
static Menu menu = {
    .item = menu_items,
    .item_count = sizeof(menu_items) / sizeof(MenuItem),
    .selected_index = 0,
    .close_callback = eeprom_menu_close_callback};

static void query_eeprom(void)
{
    // Read in the encrypted EEPROM from the Xbox
    for (unsigned int i = 0; i < sizeof(encrypted_eeprom); i++) {
        HalReadSMBusValue(EEPROM_SMBUS_ADDRESS, i, FALSE, (ULONG *)&((unsigned char *)&encrypted_eeprom)[i]);
    }
    // Decrypt the EEPROM
    eeprom_version = xbox_eeprom_decrypt(&encrypted_eeprom, &eeprom);
}

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

static void update_eeprom_text(void)
{
    int line = 0;
    pool_offset = 0;
    memset(menu_items, 0, sizeof(menu_items));

    push_line(line++, NULL, "EEPROM Settings");

    push_line(line++, (dirty) ? apply_settings : NULL, "Apply unsaved changes");

    // Read the EEPROM backup if it exists and get its creation time
    SYSTEMTIME systemTime;
    BOOL eeprom_backup_exists = FALSE;
    HANDLE eeprom_file = CreateFileA("E:\\eeprom.bin", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (eeprom_file != INVALID_HANDLE_VALUE) {
        FILETIME creationTime;
        FILETIME creationTimeLocal;
        GetFileTime(eeprom_file, &creationTime, NULL, NULL);
        CloseHandle(eeprom_file);
        FileTimeToLocalFileTime(&creationTime, &creationTimeLocal);
        FileTimeToSystemTime(&creationTimeLocal, &systemTime);
        eeprom_backup_exists = TRUE;
    }

    if (eeprom_backup_exists) {
        push_line(line++, restore_backup, "Restore backup \nfrom E:\\eeprom.bin (Created %04d/%02d/%02d %02d:%02d:%02d)",
                  systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond);
        push_line(line++, NULL, "");
    }

    // clang-format off
    const char *eeprom_version_str = "Unknown";
    switch (eeprom_version) {
        case 0x09: eeprom_version_str = "Debug Xbox"; break;
        case 0x0A: eeprom_version_str = "Xbox 1.0"; break;
        case 0x0B: eeprom_version_str = "Xbox 1.1 to 1.5"; break;
        case 0x0C: eeprom_version_str = "Xbox 1.6"; break;
        default: eeprom_version_str = "Unknown"; break;
    }
    push_line(line++, NULL, "EEPROM Version: %s", eeprom_version_str);

    char hdd_key[33];
    for (int i = 0; i < 16; i++) {
        snprintf(&hdd_key[i * 2], sizeof(hdd_key) - i * 2, "%02x", eeprom.encrypted.hdd_key[i]);
    }
    push_line(line++, NULL, "HDD Key: %s", hdd_key);

    const char *xbox_region;
    switch (eeprom.encrypted.xbox_region) {
        case XBOX_EEPROM_XBOX_REGION_NA: xbox_region = "North America"; break;
        case XBOX_EEPROM_XBOX_REGION_JP: xbox_region = "Japan"; break;
        case XBOX_EEPROM_XBOX_REGION_EU: xbox_region = "Europe and Australia"; break;
        case XBOX_EEPROM_XBOX_REGION_MANUFACTURING: xbox_region = "Manufacturing"; break;
        default: xbox_region = "Unknown";
    }
    push_line(line++, increment_xbox_region, "Game Region: %s", xbox_region);

    const char *dvd_region;
    switch (eeprom.user.dvd_region) {
        case 0: dvd_region = "0 None"; break;
        case 1: dvd_region = "1 USA, Canada"; break;
        case 2: dvd_region = "2 Europe, Japan, Middle East"; break;
        case 3: dvd_region = "3 Southeast Asia, South Korea"; break;
        case 4: dvd_region = "4 Latin America, Australia"; break;
        case 5: dvd_region = "5 Eastern Europe, Russia, Africa"; break;
        case 6: dvd_region = "6 China"; break;
        default: dvd_region = "Unknown";
    }
    push_line(line++, increment_dvd_region, "DVD Region: %s", dvd_region);

    const char *language;
    switch (eeprom.user.language) {
        case 0: language = "0 Not Set"; break;
        case 1: language = "1 English"; break;
        case 2: language = "2 Japanese"; break;
        case 3: language = "3 German"; break;
        case 4: language = "4 French"; break;
        case 5: language = "5 Spanish"; break;
        case 6: language = "6 Italian"; break;
        case 7: language = "7 Korean"; break;
        case 8: language = "8 Chinese"; break;
        case 9: language = "9 Portuguese"; break;
        default: language = "Unknown";
    }
    push_line(line++, increment_language, "Language: %s", language);

    const char *region;
    switch (eeprom.factory.video_standard) {
        case XBOX_EEPROM_VIDEO_STANDARD_NTSC_M: region = "NTSC"; break;
        case XBOX_EEPROM_VIDEO_STANDARD_NTSC_J: region = "NTSC Japan"; break;
        case XBOX_EEPROM_VIDEO_STANDARD_PAL: region = "PAL"; break;
        case XBOX_EEPROM_VIDEO_STANDARD_PAL_M: region = "PAL Brazil"; break;
        default: region = "Invalid Region";
    }
    push_line(line++, increment_video_region, "Video Region: %s", region);

    push_line(line++, NULL, "Video Flags: 0x%08lx", eeprom.user.video_settings);

    push_line(line++, video_increment_aspect_ratio, "  Aspect Ratio: %s",
                   (eeprom.user.video_settings & VIDEO_WIDESCREEN) ? "Widescreen" :
                   (eeprom.user.video_settings & VIDEO_LETTERBOX) ? "Letterbox" : "Normal");

    push_line(line++, video_increment_refresh_rate, "  Refresh Rate: %s",
                   (eeprom.user.video_settings & VIDEO_50Hz && eeprom.user.video_settings & VIDEO_60Hz) ? "50Hz / 60Hz" :
                   (eeprom.user.video_settings & VIDEO_50Hz) ? "50Hz" :
                   (eeprom.user.video_settings & VIDEO_60Hz) ? "60Hz" : "Not set");

    push_line(line++, video_toggle_480p, "  480p: [%c]", (eeprom.user.video_settings & VIDEO_MODE_480P) ? 'x' : ' ');
    push_line(line++, video_toggle_720p, "  720p: [%c]", (eeprom.user.video_settings & VIDEO_MODE_720P) ? 'x' : ' ');
    push_line(line++, video_toggle_1080i, "  1080i: [%c]", (eeprom.user.video_settings & VIDEO_MODE_1080I) ? 'x' : ' ');

    push_line(line++, NULL, "Audio Flags: 0x%08lx", eeprom.user.audio_settings);

    push_line(line++, audio_increment_channel, "  Channel Configuration: %s",
                (eeprom.user.audio_settings & XBOX_EEPROM_AUDIO_SETTINGS_MONO) ? "Mono" :
                (eeprom.user.audio_settings & XBOX_EEPROM_AUDIO_SETTINGS_SURROUND) ? "Surround" : "Stereo");

    push_line(line++, audio_increment_encoding, "  Encoding: %s",
                (eeprom.user.audio_settings & XBOX_EEPROM_AUDIO_SETTINGS_ENABLE_AC3 && eeprom.user.audio_settings & XBOX_EEPROM_AUDIO_SETTINGS_ENABLE_DTS) ? "AC3 / DTS" :
                (eeprom.user.audio_settings & XBOX_EEPROM_AUDIO_SETTINGS_ENABLE_AC3) ? "AC3" :
                (eeprom.user.audio_settings & XBOX_EEPROM_AUDIO_SETTINGS_ENABLE_DTS) ? "DTS" : "None");
    // clang-format on

    push_line(line++, mac_address_generate, "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
              eeprom.factory.mac_address[0], eeprom.factory.mac_address[1], eeprom.factory.mac_address[2],
              eeprom.factory.mac_address[3], eeprom.factory.mac_address[4], eeprom.factory.mac_address[5]);

    push_line(line++, increment_timezone_bios, "Time Zone Offset: %.1f hours", ((float)-eeprom.user.timezone_bias) / 60.0f);

    push_line(line++, generate_serial_number, "Serial Number: %12s", eeprom.factory.serial_number);

    menu.item_count = line;
}

void menu_eeprom_activate(void)
{
    dirty = 0;
    query_eeprom();
    update_eeprom_text();

    menu_push(&menu);
}
