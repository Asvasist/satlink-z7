/**
 * @file prbs.c
 * @implements SRS-MDM-004
 */
#include "satlink/modem/prbs.h"

void satlink_pn9_fill(uint8_t *out, size_t len)
{
    uint32_t lfsr = 0x1FFU;
    for (size_t i = 0U; i < len; ++i)
    {
        uint32_t byte = 0U;
        for (uint32_t b = 0U; b < 8U; ++b)
        {
            const uint32_t bit = ((lfsr >> 8U) ^ (lfsr >> 4U)) & 1U; /* taps 9 and 5 */
            byte = (byte << 1U) | (lfsr >> 8U);
            lfsr = ((lfsr << 1U) | bit) & 0x1FFU;
        }
        out[i] = (uint8_t)byte;
    }
}

uint32_t satlink_bit_errors(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint32_t errors = 0U;
    for (size_t i = 0U; i < len; ++i)
    {
        uint32_t x = (uint32_t)(a[i] ^ b[i]);
        while (x != 0U)
        {
            errors += x & 1U;
            x >>= 1U;
        }
    }
    return errors;
}
