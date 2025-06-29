#include <hal/video.h>
#include <stdlib.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#include "main.h"

static char eeprom_setting_dvd_region[64] = "";
static char eeprom_setting_language[64] = "";
static char eeprom_setting_video_flags[64] = "";
static char eeprom_setting_audio_flags[64] = "";
static char eeprom_setting_video_region[64] = "";
static char eeprom_setting_video_aspect_ratio[64] = "";
static char eeprom_setting_video_refresh_rate[64] = "";
static char eeprom_setting_video_enable_480p[64] = "";
static char eeprom_setting_video_enable_720p[64] = "";
static char eeprom_setting_video_enable_1080i[64] = "";
static char eeprom_setting_audio_channels[64] = "";
static char eeprom_setting_audio_encoding[64] = "";
static char eeprom_setting_apply_text[64] = "";
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

static MenuItem menu_items[] = {
    {"EEPROM Settings", NULL},
    {eeprom_setting_apply_text, apply_settings},
    {eeprom_setting_dvd_region, increment_dvd_region},
    {eeprom_setting_language, increment_language},
    {eeprom_setting_video_region, increment_video_region},
    {eeprom_setting_video_flags, NULL},
    {eeprom_setting_video_aspect_ratio, video_increment_aspect_ratio},
    {eeprom_setting_video_refresh_rate, video_increment_refresh_rate},
    {"  Resolutions:", NULL},
    {eeprom_setting_video_enable_480p, video_toggle_480p},
    {eeprom_setting_video_enable_720p, video_toggle_720p},
    {eeprom_setting_video_enable_1080i, video_toggle_1080i},
    {eeprom_setting_audio_flags, NULL},
    {eeprom_setting_audio_channels, audio_increment_channel},
    {eeprom_setting_audio_encoding, audio_increment_encoding}};

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

static void update_eeprom_text(void)
{
    if (dirty) {
        strncpy(eeprom_setting_apply_text, "Apply unsaved changes", sizeof(eeprom_setting_apply_text));
    } else {
        strncpy(eeprom_setting_apply_text, "Apply", sizeof(eeprom_setting_apply_text));
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
    snprintf(eeprom_setting_dvd_region, sizeof(eeprom_setting_dvd_region), "DVD Region: %s", dvd_region);

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
    snprintf(eeprom_setting_language, sizeof(eeprom_setting_language), "Language: %s", language);

    const char *region;
    switch (av_region) {
        case AV_REGION_NTSC: region = "NTSC"; break;
        case AV_REGION_NTSCJ: region = "NTSC Japan"; break;
        case AV_REGION_PAL: region = "PAL"; break;
        case AV_REGION_PALM: region = "PAL Brazil"; break;
        default: region = "Invalid Region";
    }
    snprintf(eeprom_setting_video_region, sizeof(eeprom_setting_video_region), "Video Region: %s", region);
    // clang-format on

    snprintf(eeprom_setting_video_flags, sizeof(eeprom_setting_video_flags), "Video Flags: 0x%08lx ", video_flags);
    {
        int index = 0;

        const char *ratio;
        if (video_flags & VIDEO_WIDESCREEN) {
            ratio = "Widescreen";
        } else if (video_flags & VIDEO_LETTERBOX) {
            ratio = "Letterbox";
        } else {
            ratio = "Normal";
        }
        snprintf(eeprom_setting_video_aspect_ratio, sizeof(eeprom_setting_video_aspect_ratio), "  Aspect Ratio: %s", ratio);

        index = 0;
        index += snprintf(&eeprom_setting_video_refresh_rate[index], sizeof(eeprom_setting_video_refresh_rate) - index, "  Refresh Rate: ");
        if ((video_flags & (VIDEO_50Hz | VIDEO_60Hz)) == 0) {
            index += snprintf(&eeprom_setting_video_refresh_rate[index], sizeof(eeprom_setting_video_refresh_rate) - index, "Not set");
        } else {
            if (video_flags & VIDEO_50Hz) {
                index += snprintf(&eeprom_setting_video_refresh_rate[index], sizeof(eeprom_setting_video_refresh_rate) - index, "50Hz");
            }
            if (video_flags & VIDEO_60Hz) {
                if (video_flags & VIDEO_50Hz) {
                    index += snprintf(&eeprom_setting_video_refresh_rate[index], sizeof(eeprom_setting_video_refresh_rate) - index, " / ");
                }
                index += snprintf(&eeprom_setting_video_refresh_rate[index], sizeof(eeprom_setting_video_refresh_rate) - index, "60Hz");
            }
        }

        snprintf(eeprom_setting_video_enable_480p, sizeof(eeprom_setting_video_enable_480p), "    480p [%c]",
                 (video_flags & VIDEO_MODE_480P) ? 'x' : ' ');

        snprintf(eeprom_setting_video_enable_720p, sizeof(eeprom_setting_video_enable_720p), "    720p [%c]",
                 (video_flags & VIDEO_MODE_720P) ? 'x' : ' ');

        snprintf(eeprom_setting_video_enable_1080i, sizeof(eeprom_setting_video_enable_1080i), "    1080i [%c]",
                 (video_flags & VIDEO_MODE_1080I) ? 'x' : ' ');
    }

    snprintf(eeprom_setting_audio_flags, sizeof(eeprom_setting_audio_flags), "Audio Flags: 0x%08lx ", audio_flags);
    {
        const char *channels;
        if (audio_flags & AUDIO_FLAG_CHANNEL_MONO) {
            channels = "Mono";
        } else if (audio_flags & AUDIO_FLAG_CHANNEL_SURROUND) {
            channels = "Surround";
        } else {
            channels = "Stereo";
        }
        snprintf(eeprom_setting_audio_channels, sizeof(eeprom_setting_audio_channels), "  Channels: %s", channels);

        int index = 0;
        index += snprintf(&eeprom_setting_audio_encoding[index], sizeof(eeprom_setting_audio_encoding) - index, "  Encoding: ");
        if ((audio_flags & AUDIO_FLAG_ENCODING_MASK) == 0) {
            index += snprintf(&eeprom_setting_audio_encoding[index], sizeof(eeprom_setting_audio_encoding) - index, " None");
        } else {
            if (audio_flags & AUDIO_FLAG_ENCODING_AC3) {
                index += snprintf(&eeprom_setting_audio_encoding[index], sizeof(eeprom_setting_audio_encoding) - index, "AC3");
            }
            if (audio_flags & AUDIO_FLAG_ENCODING_DTS) {
                if (audio_flags & AUDIO_FLAG_ENCODING_AC3) {
                    index += snprintf(&eeprom_setting_audio_encoding[index], sizeof(eeprom_setting_audio_encoding) - index, " / ");
                }
                index += snprintf(&eeprom_setting_audio_encoding[index], sizeof(eeprom_setting_audio_encoding) - index, "DTS");
            }
        }
    }
}

void menu_eeprom_activate(void)
{
    query_eeprom();
    update_eeprom_text();

    menu_push(&menu);
}
