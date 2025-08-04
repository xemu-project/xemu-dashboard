// SPDX-License-Identifier: GPL-2.0-or-later
// This is almost entirely based on https://github.com/Ernegien/XboxEepromEditor

#include "xbox_eeprom.h"
#include "rc4.h"
#include "sha1.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// https://github.com/xemu-project/xemu/blob/9d5cf0926aa6f8eb2221e63a2e92bd86b02afae0/hw/xbox/eeprom_generation.c#L25
static uint32_t xbox_eeprom_crc(const void *data, uint32_t len)
{
    uint32_t high = 0;
    uint32_t low = 0;
    for (uint32_t i = 0; i < len / 4; i++) {
        uint32_t val = ((uint32_t *)data)[i];
        uint64_t sum = ((uint64_t)high << 32) | low;

        high = (sum + val) >> 32;
        low += val;
    }
    return ~(high + low);
}

static int do_eeprom_sha1_loop(const uint8_t hardware_revision, const void *data, size_t data_length, uint8_t sha1_output[20])
{
    SHA1Context sha1_context;
    uint8_t sha1_buffer[20];

    // Let's do the encryption stages
    const uint32_t sha1_intermediate_debug_first[] = {0x85F9E51A, 0xE04613D2, 0x6D86A50C, 0x77C32E3C, 0x4BD717A4};
    const uint32_t sha1_intermediate_debug_second[] = {0x5D7A9C6B, 0xE1922BEB, 0xB82CCDBC, 0x3137AB34, 0x486B52B3};

    // 1.0
    const uint32_t sha1_intermedia_retail1_first[] = {0x72127625, 0x336472B9, 0xBE609BEA, 0xF55E226B, 0x99958DAC};
    const uint32_t sha1_intermedia_retail1_second[] = {0x76441D41, 0x4DE82659, 0x2E8EF85E, 0xB256FACA, 0xC4FE2DE8};

    // 1.1 to 1.5
    const uint32_t sha1_intermedia_retail2_first[] = {0x39B06E79, 0xC9BD25E8, 0xDBC6B498, 0x40B4389D, 0x86BBD7ED};
    const uint32_t sha1_intermedia_retail2_second[] = {0x9B49BED3, 0x84B430FC, 0x6B8749CD, 0xEBFE5FE5, 0xD96E7393};

    // 1.6
    const uint32_t sha1_intermedia_retail3_first[] = {0x8058763A, 0xF97D4E0E, 0x865A9762, 0x8A3D920D, 0x08995B2C};
    const uint32_t sha1_intermedia_retail3_second[] = {0x01075307, 0xA2f1E037, 0x1186EEEA, 0x88DA9992, 0x168A5609};

    // Determine which SHA1 intermediate values to use based on hardware revision
    const uint32_t *sha1_h_a;
    const uint32_t *sha1_h_b;
    if (hardware_revision == 0x09) {
        sha1_h_a = sha1_intermediate_debug_first;
        sha1_h_b = sha1_intermediate_debug_second;
    } else if (hardware_revision == 0x0A) {
        sha1_h_a = sha1_intermedia_retail1_first;
        sha1_h_b = sha1_intermedia_retail1_second;
    } else if (hardware_revision == 0x0B) {
        sha1_h_a = sha1_intermedia_retail2_first;
        sha1_h_b = sha1_intermedia_retail2_second;
    } else if (hardware_revision == 0x0C) {
        sha1_h_a = sha1_intermedia_retail3_first;
        sha1_h_b = sha1_intermedia_retail3_second;
    } else {
        return -1;
    }

    // Reset the SHA1 context with the first intermediate values
    sha1_fill(&sha1_context, sha1_h_a[0], sha1_h_a[1], sha1_h_a[2], sha1_h_a[3], sha1_h_a[4]);
    sha1_context.msg_blk_index = 0;
    sha1_context.computed = false;
    sha1_context.length = 512;

    sha1_input(&sha1_context, (uint8_t *)data, data_length);
    sha1_result(&sha1_context, sha1_buffer);

    sha1_fill(&sha1_context, sha1_h_b[0], sha1_h_b[1], sha1_h_b[2], sha1_h_b[3], sha1_h_b[4]);
    sha1_context.msg_blk_index = 0;
    sha1_context.computed = false;
    sha1_context.length = 512;

    sha1_input(&sha1_context, (uint8_t *)sha1_buffer, sizeof(sha1_buffer));
    sha1_result(&sha1_context, sha1_buffer);

    memcpy(sha1_output, sha1_buffer, 20);
    return 0;
}

int xbox_eeprom_decrypt(const xbox_eeprom_t *encrypted_eeprom, xbox_eeprom_t *decrypted_eeprom)
{
    RC4Context rc4_context;

    if (encrypted_eeprom == NULL || decrypted_eeprom == NULL) {
        return -1; // Invalid arguments
    }

    // Copy the encrypted EEPROM to the decrypted EEPROM
    memcpy(decrypted_eeprom, encrypted_eeprom, sizeof(xbox_eeprom_t));

    // We don't know what revision the EEPROM is yet, so we have to try all of them until one works.
    const uint8_t hardware_revision[] = {0x09, 0x0A, 0x0B, 0x0C};

    for (size_t i = 0; i < sizeof(hardware_revision) / sizeof(hardware_revision[0]); i++) {

        // Determine the decryption key
        uint8_t decryption_key[20];
        do_eeprom_sha1_loop(hardware_revision[i], &encrypted_eeprom->encrypted.sha1_hash,
                            sizeof(encrypted_eeprom->encrypted.sha1_hash), decryption_key);

        // Initialise a RC4 context using the decryption key
        rc4_init(&rc4_context, decryption_key, sizeof(decryption_key));

        // Decrypt the encrypted EEPROM section which does not include the SHA1 hash
        xbox_eeprom_encrypted_t decrypted_result;

        // First copy the encrypted data to the decrypted array
        memcpy(&decrypted_result, &encrypted_eeprom->encrypted, sizeof(decrypted_result));

        // Then decrypt the data using RC4. The decrypted data will be written back to the same memory location.
        rc4_crypt(&rc4_context, XBOX_EEPROM_ENCRYPTED_START_PTR(&decrypted_result), XBOX_EEPROM_ENCRYPTED_LENGTH);

        // To verify it's valid, do a SHA1 over the decrypted data and verify it matches the SHA1 hash in the EEPROM.
        uint8_t sha1_test[20];
        do_eeprom_sha1_loop(hardware_revision[i], XBOX_EEPROM_ENCRYPTED_START_PTR(&decrypted_result), XBOX_EEPROM_ENCRYPTED_LENGTH, sha1_test);
        if (memcmp(sha1_test, decrypted_result.sha1_hash, sizeof(decrypted_result.sha1_hash)) == 0) {
            // Okay we have a match, copy the decrypted result back to the decrypted EEPROM
            memcpy(&decrypted_eeprom->encrypted, &decrypted_result, sizeof(decrypted_eeprom->encrypted));
            return hardware_revision[i];
        } else {
            // Try the next hardware revision
            continue;
        }
    }

    return -1;
}

int xbox_eeprom_encrypt(uint8_t xbox_revision, const xbox_eeprom_t *decrypted_eeprom, xbox_eeprom_t *encrypted_eeprom)
{
    if (decrypted_eeprom == NULL || encrypted_eeprom == NULL) {
        return -1;
    }

    if (xbox_revision < 0x09 || xbox_revision > 0x0C) {
        return -1;
    }

    // Copy the decrypted EEPROM to the encrypted EEPROM
    memcpy(encrypted_eeprom, decrypted_eeprom, sizeof(xbox_eeprom_t));

    // Write out the factory and user section checksums
    encrypted_eeprom->factory.checksum = xbox_eeprom_crc(XBOX_EEPROM_FACTORY_START_PTR(&encrypted_eeprom->factory),
                                                         XBOX_EEPROM_FACTORY_LENGTH);
    encrypted_eeprom->user.checksum = xbox_eeprom_crc(XBOX_EEPROM_USER_START_PTR(&encrypted_eeprom->user),
                                                      XBOX_EEPROM_USER_LENGTH);

    // SHA1 Stage 1 - perform the first hash calculation over the currently unencrypted area (confounder, hdd key, region)
    // to determine the main sha1 hash
    uint8_t sha1_hash[20];
    do_eeprom_sha1_loop(xbox_revision, XBOX_EEPROM_ENCRYPTED_START_PTR(&encrypted_eeprom->encrypted), XBOX_EEPROM_ENCRYPTED_LENGTH, sha1_hash);

    // SHA1 Stage 2 - perform the second hash calculation over the main sha1 hash to determine the RC4 key
    uint8_t rc4_key[20];
    do_eeprom_sha1_loop(xbox_revision, sha1_hash, sizeof(sha1_hash), rc4_key);

    // Stage 3 - RC4 using the the new key to encrypt the data
    RC4Context rc4_context;
    xbox_eeprom_encrypted_t encrypted_result;
    rc4_init(&rc4_context, rc4_key, sizeof(rc4_key));
    // First copy the decrypted data to the encrypted array
    memcpy(&encrypted_result, &decrypted_eeprom->encrypted, sizeof(encrypted_result));

    // Encrypt the data using RC4. The encrypted data will be written back to the same memory location
    rc4_crypt(&rc4_context, XBOX_EEPROM_ENCRYPTED_START_PTR(&encrypted_result), XBOX_EEPROM_ENCRYPTED_LENGTH);

    // Stage 4 - Write it out
    // Copy over the sha1 hash
    memcpy(encrypted_result.sha1_hash, sha1_hash, sizeof(encrypted_result.sha1_hash));

    // Write the encypted result back to the encrypted EEPROM
    memcpy(&encrypted_eeprom->encrypted, &encrypted_result, sizeof(encrypted_result));

    return 0;
}
