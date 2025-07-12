#include <stdint.h>

enum xbox_eeprom_region_code
{
    XBOX_EEPROM_XBOX_REGION_NA = 0x00000001,
    XBOX_EEPROM_XBOX_REGION_JP = 0x00000002,
    XBOX_EEPROM_XBOX_REGION_EU = 0x00000004,
    XBOX_EEPROM_XBOX_REGION_MANUFACTURING = 0x80000000
};

enum xbox_eeprom_video_standard
{
    XBOX_EEPROM_VIDEO_STANDARD_INVALID = 0x00000000,
    XBOX_EEPROM_VIDEO_STANDARD_NTSC_M = 0x00400100,
    XBOX_EEPROM_VIDEO_STANDARD_NTSC_J = 0x00400200,
    XBOX_EEPROM_VIDEO_STANDARD_PAL = 0x00800300,
    XBOX_EEPROM_VIDEO_STANDARD_PAL_M = 0x00400400
};

enum xbox_eeprom_language_id
{
    XBOX_EEPROM_LANGUAGE_ID_INVALID = 0x00000000,
    XBOX_EEPROM_LANGUAGE_ID_ENGLISH = 0x00000001,
    XBOX_EEPROM_LANGUAGE_ID_JAPANESE = 0x00000002,
    XBOX_EEPROM_LANGUAGE_ID_GERMAN = 0x00000003,
    XBOX_EEPROM_LANGUAGE_ID_FRENCH = 0x00000004,
    XBOX_EEPROM_LANGUAGE_ID_SPANISH = 0x00000005,
    XBOX_EEPROM_LANGUAGE_ID_ITALIAN = 0x00000006,
    XBOX_EEPROM_LANGUAGE_ID_KOREAN = 0x00000007,
    XBOX_EEPROM_LANGUAGE_ID_CHINESE = 0x00000008,
    XBOX_EEPROM_LANGUAGE_ID_PORTUGUESE = 0x00000009
};

enum xbox_eeprom_video_settings_mask
{
    XBOX_EEPROM_VIDEO_SETTINGS_480P = 0x00080000,
    XBOX_EEPROM_VIDEO_SETTINGS_720P = 0x00020000,
    XBOX_EEPROM_VIDEO_SETTINGS_1080I = 0x00040000,
    XBOX_EEPROM_VIDEO_SETTINGS_WIDESCREEN = 0x00010000,
    XBOX_EEPROM_VIDEO_SETTINGS_LETTERBOX = 0x00100000,
    XBOX_EEPROM_VIDEO_SETTINGS_60HZ = 0x00400000,
    XBOX_EEPROM_VIDEO_SETTINGS_50HZ = 0x00800000
};

enum xbox_eeprom_audio_settings_mask
{
    XBOX_EEPROM_AUDIO_SETTINGS_STEREO = 0x00000000,
    XBOX_EEPROM_AUDIO_SETTINGS_MONO = 0x00000001,
    XBOX_EEPROM_AUDIO_SETTINGS_SURROUND = 0x00000002,
    XBOX_EEPROM_AUDIO_SETTINGS_ENABLE_AC3 = 0x00010000,
    XBOX_EEPROM_AUDIO_SETTINGS_ENABLE_DTS = 0x00020000
};

typedef struct __attribute__((packed))
{
    uint8_t sha1_hash[20]; // 0x00 - 0x13
    uint8_t confounder[8]; // 0x14 - 0x1B
    uint8_t hdd_key[16];   // 0x1C - 0x2B
    uint32_t xbox_region;  // 0x2C - 0x2F
} xbox_eeprom_encrypted_t;
static_assert(sizeof(xbox_eeprom_encrypted_t) == (0x2F - 0x00 + 1), "xbox_eeprom_encrypted_t size mismatch");

typedef struct __attribute__((packed))
{
    uint32_t checksum;         // 0x30 - 0x33
    uint8_t serial_number[12]; // 0x34 - 0x3F
    uint8_t mac_address[6];    // 0x40 - 0x45
    uint8_t padding1[2];       // 0x46 - 0x47
    uint8_t online_key[16];    // 0x48 - 0x57
    uint32_t video_standard;   // 0x58 - 0x5B
    uint8_t padding2[4];       // 0x5C - 0x5F
} xbox_eeprom_factory_t;
static_assert(sizeof(xbox_eeprom_encrypted_t) == (0x5F - 0x30 + 1), "xbox_eeprom_encrypted_t size mismatch");

typedef struct __attribute__((packed))
{
    uint32_t checksum;                  // 0x60 - 0x63
    int32_t timezone_bias;              // 0x64 - 0x67
    char timezone_std_name[4];          // 0x68 - 0x6B
    char timezone_dlt_name[4];          // 0x6C - 0x6F
    uint8_t padding1[8];                // 0x70 - 0x77
    uint32_t timezone_std_start;        // 0x78 - 0x7B
    uint32_t timezone_dlt_start;        // 0x7C - 0x7F
    uint8_t timezone_padding2[8];       // 0x80 - 0x87
    int32_t timezone_std_bias;          // 0x88 - 0x8B
    int32_t timezone_dlt_bias;          // 0x8C - 0x8F
    uint32_t language;                  // 0x90 - 0x93
    uint32_t video_settings;            // 0x94 - 0x97
    uint32_t audio_settings;            // 0x98 - 0x9B
    uint32_t parental_control_games;    // 0x9C - 0x9F
    uint32_t parental_control_passcode; // 0xA0 - 0xA3
    uint32_t parental_control_movies;   // 0xA4 - 0xA7
    uint32_t xlive_ip_address;          // 0xA8 - 0xAB
    uint32_t xlive_dns_address;         // 0xAC - 0xAF
    uint32_t xlive_default_gateway;     // 0xB0 - 0xB3
    uint32_t xlive_subnet_mask;         // 0xB4 - 0xB7
    uint32_t misc_flags;                // 0xB8 - 0xBB
    uint32_t dvd_region;                // 0xBC - 0xBF
} xbox_eeprom_user_t;
static_assert(sizeof(xbox_eeprom_user_t) == (0xBF - 0x60 + 1), "xbox_eeprom_user_t size mismatch");

#define XBOX_EEPROM_ENCRYPTED_START_PTR(a) ((void *)&((a)->confounder[0]))
#define XBOX_EEPROM_ENCRYPTED_LENGTH       (0x2F - 0x14 + 1)
#define XBOX_EEPROM_FACTORY_START_PTR(a)   ((void *)&((a)->serial_number[0]))
#define XBOX_EEPROM_FACTORY_LENGTH         (0x5F - 0x34 + 1)
#define XBOX_EEPROM_USER_START_PTR(a)      ((void *)&((a)->timezone_bias))
#define XBOX_EEPROM_USER_LENGTH            (0xBF - 0x60 + 1)

// now the whole eeprom structure
typedef struct __attribute__((packed))
{
    xbox_eeprom_encrypted_t encrypted; // 0x00 - 0x2F
    xbox_eeprom_factory_t factory;     // 0x30 - 0x5F
    xbox_eeprom_user_t user;           // 0x60 - 0xBF
    uint8_t padding[0x100 - 0xC0];     // 0xC0 - 0x0FF
} xbox_eeprom_t;
static_assert(sizeof(xbox_eeprom_t) == 0x100, "xbox_eeprom_t size mismatch");

int xbox_eeprom_decrypt(const xbox_eeprom_t *encrypted_eeprom, xbox_eeprom_t *decrypted_eeprom);
int xbox_eeprom_encrypt(uint8_t xbox_revision, const xbox_eeprom_t *decrypted_eeprom, xbox_eeprom_t *encrypted_eeprom);
