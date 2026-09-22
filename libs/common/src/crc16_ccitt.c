/**
 * @file crc16_ccitt.c
 * @brief Nibble-table CRC-16-CCITT (32-byte table: small enough for the MicroBlaze V
 *        bootloader, fast enough for Linux and FreeRTOS).
 *
 * @implements SRS-LIB-001
 */
#include "satlink/common/crc16_ccitt.h"

/* CRC of each 4-bit value shifted into an all-zero register (polynomial 0x1021). */
static const uint16_t k_crc16_nibble_table[16] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
    0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU};

static uint16_t crc16_process_nibble(uint16_t crc, uint32_t nibble)
{
    const uint32_t index = (((uint32_t)crc >> 12U) ^ nibble) & 0x0FU;
    return (uint16_t)((((uint32_t)crc << 4U) ^ (uint32_t)k_crc16_nibble_table[index]) & 0xFFFFU);
}

uint16_t satlink_crc16_ccitt_update(uint16_t crc, const uint8_t *data, size_t len)
{
    uint16_t result = crc;

    if (data != NULL)
    {
        for (size_t i = 0U; i < len; ++i)
        {
            const uint32_t byte = (uint32_t)data[i];
            result = crc16_process_nibble(result, byte >> 4U);
            result = crc16_process_nibble(result, byte & 0x0FU);
        }
    }

    return result;
}

uint16_t satlink_crc16_ccitt(const uint8_t *data, size_t len)
{
    return satlink_crc16_ccitt_update((uint16_t)SATLINK_CRC16_CCITT_INIT, data, len);
}
