#include <hal/video.h>
#include <stdlib.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include "main.h"

#define MAX_LINES      32
#define MAX_CHARACTERS 512
static char char_pool[MAX_CHARACTERS];
static int pool_offset;

static int dvd_region_index = 0;
static int language_index = 0;
static ULONG video_flags = 0;
static ULONG audio_flags = 0;
static ULONG av_region = 0;
static int dirty = 0;

// FIXME probably should be in nxdk
#define AUDIO_FLAG_ENCODING_AC3     0x00010000
#define AUDIO_FLAG_ENCODING_DTS     0x00020000
#define AUDIO_FLAG_ENCODING_MASK    (AUDIO_FLAG_ENCODING_AC3 | AUDIO_FLAG_ENCODING_DTS)
#define AUDIO_FLAG_CHANNEL_MONO     0x00000001
#define AUDIO_FLAG_CHANNEL_SURROUND 0x00000002
#define AUDIO_FLAG_CHANNEL_MASK     (AUDIO_FLAG_CHANNEL_MONO | AUDIO_FLAG_CHANNEL_SURROUND)
#define AV_REGION_NTSC              0x00400100
#define AV_REGION_NTSCJ             0x00400200
#define AV_REGION_PAL               0x00800300
#define AV_REGION_PALM              0x00400400

static void update_eeprom_text(void);

static void apply_settings(void)
{
    ULONG type = 4;
    ExSaveNonVolatileSetting(XC_DVD_REGION, type, &dvd_region_index, sizeof(dvd_region_index));
    ExSaveNonVolatileSetting(XC_LANGUAGE, type, &language_index, sizeof(language_index));
    ExSaveNonVolatileSetting(XC_VIDEO, type, &video_flags, sizeof(video_flags));
    ExSaveNonVolatileSetting(XC_AUDIO, type, &audio_flags, sizeof(audio_flags));
    ExSaveNonVolatileSetting(XC_FACTORY_AV_REGION, type, &av_region, sizeof(av_region));
    dirty = 0;
    update_eeprom_text();
}

static void increment_dvd_region(void)
{
    dvd_region_index = (dvd_region_index + 1) % 7;
    dirty = 1;
    update_eeprom_text();
}

static void increment_language(void)
{
    language_index = (language_index + 1) % 10;
    dirty = 1;
    update_eeprom_text();
}

static void increment_video_region(void)
{
    static const ULONG regions[4] = {AV_REGION_NTSC, AV_REGION_PAL, AV_REGION_NTSCJ, AV_REGION_PALM};
    int index = 0;
    for (int i = 0; i < 4; i++) {
        if (regions[i] == av_region) {
            index = i;
            index = (index + 1) % 4;
            break;
        }
    }
    av_region = regions[index];
    dirty = 1;
    update_eeprom_text();
}

static void video_increment_aspect_ratio(void)
{
    if (video_flags & VIDEO_WIDESCREEN) {
        video_flags &= ~VIDEO_WIDESCREEN;
        video_flags |= VIDEO_LETTERBOX;
    } else if (video_flags & VIDEO_LETTERBOX) {
        video_flags &= ~VIDEO_LETTERBOX;
    } else {
        video_flags |= VIDEO_WIDESCREEN;
    }
    dirty = 1;
    update_eeprom_text();
}

static void video_increment_refresh_rate(void)
{
    ULONG index = (video_flags >> 22) & 0x03;
    index = (index + 1) % 4;
    video_flags &= ~(VIDEO_50Hz | VIDEO_60Hz);
    video_flags |= index << 22;
    dirty = 1;
    update_eeprom_text();
}

static void video_toggle_480p(void)
{
    video_flags ^= VIDEO_MODE_480P;
    dirty = 1;
    update_eeprom_text();
}

static void video_toggle_720p(void)
{
    video_flags ^= VIDEO_MODE_720P;
    dirty = 1;
    update_eeprom_text();
}

static void video_toggle_1080i(void)
{
    video_flags ^= VIDEO_MODE_1080I;
    dirty = 1;
    update_eeprom_text();
}

static void audio_increment_channel(void)
{
    ULONG index = (audio_flags & AUDIO_FLAG_CHANNEL_MASK) >> 0;
    index = (index + 1) % 3;
    audio_flags &= ~AUDIO_FLAG_CHANNEL_MASK;
    audio_flags |= index << 0;
    dirty = 1;
    update_eeprom_text();
}

static void audio_increment_encoding(void)
{
    ULONG index = (audio_flags & AUDIO_FLAG_ENCODING_MASK) >> 16;
    index = (index + 1) % 4;
    audio_flags &= ~AUDIO_FLAG_ENCODING_MASK;
    audio_flags |= index << 16;
    dirty = 1;
    update_eeprom_text();
}

static MenuItem menu_items[32];
static Menu menu = {
    .item = menu_items,
    .item_count = sizeof(menu_items) / sizeof(MenuItem),
    .selected_index = 0};

static void query_eeprom(void)
{
    ULONG data, type;
    ExQueryNonVolatileSetting(XC_DVD_REGION, &type, &data, sizeof(data), NULL);
    dvd_region_index = data & 0xFF;

    ExQueryNonVolatileSetting(XC_LANGUAGE, &type, &data, sizeof(data), NULL);
    language_index = data & 0xFF;

    ExQueryNonVolatileSetting(XC_VIDEO, &type, &data, sizeof(data), NULL);
    video_flags = data;

    ExQueryNonVolatileSetting(XC_FACTORY_AV_REGION, &type, &data, sizeof(data), NULL);
    av_region = data;
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

    if (dirty) {
        push_line(line++, apply_settings, "Apply unsaved changes");
    } else {
        push_line(line++, apply_settings, "Apply");
    }

    // clang-format off
    const char *dvd_region;
    switch (dvd_region_index) {
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
    switch (language_index) {
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
    switch (av_region) {
        case AV_REGION_NTSC: region = "NTSC"; break;
        case AV_REGION_NTSCJ: region = "NTSC Japan"; break;
        case AV_REGION_PAL: region = "PAL"; break;
        case AV_REGION_PALM: region = "PAL Brazil"; break;
        default: region = "Invalid Region";
    }
    push_line(line++, increment_video_region, "Video Region: %s", region);

    push_line(line++, NULL, "Video Flags: 0x%08lx", video_flags);

    push_line(line++, video_increment_aspect_ratio, "  Aspect Ratio: %s",
                   (video_flags & VIDEO_WIDESCREEN) ? "Widescreen" :
                   (video_flags & VIDEO_LETTERBOX) ? "Letterbox" : "Normal");

    push_line(line++, video_increment_refresh_rate, "  Refresh Rate: %s",
                   (video_flags & VIDEO_50Hz && video_flags & VIDEO_60Hz) ? "50Hz / 60Hz" :
                   (video_flags & VIDEO_50Hz) ? "50Hz" :
                   (video_flags & VIDEO_60Hz) ? "60Hz" : "Not set");

    push_line(line++, video_toggle_480p, "  480p: [%c]", (video_flags & VIDEO_MODE_480P) ? 'x' : ' ');
    push_line(line++, video_toggle_720p, "  720p: [%c]", (video_flags & VIDEO_MODE_720P) ? 'x' : ' ');
    push_line(line++, video_toggle_1080i, "  1080i: [%c]", (video_flags & VIDEO_MODE_1080I) ? 'x' : ' ');

    push_line(line++, NULL, "Audio Flags: 0x%08lx", audio_flags);

    push_line(line++, audio_increment_channel, "  Channel Configuration: %s",
                (audio_flags & AUDIO_FLAG_CHANNEL_MONO) ? "Mono" :
                (audio_flags & AUDIO_FLAG_CHANNEL_SURROUND) ? "Surround" : "Stereo");

    push_line(line++, audio_increment_encoding, "  Encoding: %s",
                (audio_flags & AUDIO_FLAG_ENCODING_AC3 && audio_flags & AUDIO_FLAG_ENCODING_DTS) ? "AC3 / DTS" :
                (audio_flags & AUDIO_FLAG_ENCODING_AC3) ? "AC3" :
                (audio_flags & AUDIO_FLAG_ENCODING_DTS) ? "DTS" : "None");
    // clang-format on

    menu.item_count = line;
    printf("pool_offset: %d, menu.item_count: %d\n", pool_offset, menu.item_count);
}

void menu_eeprom_activate(void)
{
    query_eeprom();
    update_eeprom_text();

    menu_push(&menu);
}
