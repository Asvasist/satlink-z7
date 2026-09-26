/**
 * @file crc32.c
 * @brief Nibble-table CRC-32 (64-byte table: fits in the MicroBlaze V bootloader's BRAM).
 *
 * @implements SRS-LIB-004
 */
#include "satlink/common/crc32.h"

/* Reflected polynomial 0xEDB88320 applied to each 4-bit value. */
static const uint32_t k_crc32_nibble_table[16] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL, 0x76DC4190UL, 0x6B6B51F4UL,
    0x4DB26158UL, 0x5005713CUL, 0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL};

uint32_t satlink_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    if (data == NULL)
    {
        return crc;
    }

    uint32_t reg = ~crc;
    for (size_t i = 0U; i < len; ++i)
    {
        reg ^= (uint32_t)data[i];
        reg = (reg >> 4U) ^ k_crc32_nibble_table[reg & 0x0FU];
        reg = (reg >> 4U) ^ k_crc32_nibble_table[reg & 0x0FU];
    }
    return ~reg;
}

uint32_t satlink_crc32(const uint8_t *data, size_t len)
{
    return satlink_crc32_update((uint32_t)SATLINK_CRC32_INIT, data, len);
}
