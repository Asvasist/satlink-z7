/**
 * @file frame.c
 * @implements SRS-MDM-004
 */
#include "satlink/modem/frame.h"

#include "satlink/common/ccsds_randomizer.h"
#include "satlink/common/crc16_ccitt.h"
#include "satlink/modem/conv.h"
#include "satlink/modem/mapper.h"
#include "satlink/modem/prbs.h"

#define CODED_INFO_BITS ((size_t)(SATLINK_FRAME_INFO_BYTES + 2U) * 8U)
#define MAX_CODED_BITS  (2U * (CODED_INFO_BITS + SATLINK_CONV_TAIL_BITS) + 2U)

static float g_asm[SATLINK_FRAME_ASM_SYMBOLS];
static bool g_asm_ready = false;

const float *satlink_frame_asm_symbols(void)
{
    if (!g_asm_ready)
    {
        for (uint32_t i = 0U; i < SATLINK_FRAME_ASM_SYMBOLS; ++i)
        {
            const uint32_t bit = (uint32_t)(SATLINK_FRAME_ASM >> (31U - i)) & 1U;
            g_asm[i] = (bit != 0U) ? -1.0F : 1.0F;
        }
        g_asm_ready = true;
    }
    return g_asm;
}

static uint8_t crc4(uint16_t data12)
{
    /* x^4 + x + 1 over the 12 data bits, MSB first. */
    uint32_t reg = 0U;
    for (int32_t i = 11; i >= 0; --i)
    {
        const uint32_t in = ((uint32_t)data12 >> (uint32_t)i) & 1U;
        const uint32_t fb = ((reg >> 3U) & 1U) ^ in;
        reg = ((reg << 1U) & 0xFU) ^ ((fb != 0U) ? 0x3U : 0U);
    }
    return (uint8_t)reg;
}

uint16_t satlink_frame_header_encode(const satlink_frame_header_t *header)
{
    const uint32_t data = (((uint32_t)header->modcod & 0x7U) << 9U) |
                          (((uint32_t)header->type & 0x3U) << 7U) | ((uint32_t)header->seq & 0x7FU);
    return (uint16_t)((data << 4U) | (uint32_t)crc4((uint16_t)data));
}

bool satlink_frame_header_decode(uint16_t bits, satlink_frame_header_t *header)
{
    const uint16_t data = (uint16_t)((uint32_t)bits >> 4U);
    if (crc4(data) != (uint8_t)(bits & 0xFU))
    {
        return false;
    }
    header->modcod = (uint8_t)(((uint32_t)data >> 9U) & 0x7U);
    header->type = (uint8_t)(((uint32_t)data >> 7U) & 0x3U);
    header->seq = (uint8_t)((uint32_t)data & 0x7FU);
    return satlink_modcod_get(header->modcod) != NULL;
}

size_t satlink_frame_payload_symbols(uint8_t modcod)
{
    const satlink_modcod_t *mc = satlink_modcod_get(modcod);
    if (mc == NULL)
    {
        return 0U;
    }
    const size_t bits = satlink_conv_coded_bits(CODED_INFO_BITS, mc->rate);
    const size_t bps = (size_t)mc->modulation;
    return (bits + bps - 1U) / bps;
}

size_t satlink_frame_symbols(uint8_t modcod)
{
    const size_t payload = satlink_frame_payload_symbols(modcod);
    if (payload == 0U)
    {
        return 0U;
    }
    const size_t pilots = ((payload - 1U) / SATLINK_FRAME_PILOT_PERIOD) * SATLINK_FRAME_PILOT_LEN;
    return SATLINK_FRAME_ASM_SYMBOLS + SATLINK_FRAME_HDR_SYMBOLS + payload + pilots;
}

satlink_status_t satlink_frame_build(const satlink_frame_header_t *header, const uint8_t *info,
                                     satlink_cf_t *symbols, size_t max_symbols, size_t *count)
{
    static uint8_t block[SATLINK_FRAME_INFO_BYTES + 2U];
    static uint8_t coded[MAX_CODED_BITS];
    static satlink_cf_t payload[SATLINK_FRAME_MAX_PAYLOAD_SYMBOLS];

    if ((header == NULL) || (symbols == NULL) || (count == NULL) ||
        ((info == NULL) && (header->type == (uint8_t)SATLINK_FRAME_DATA)))
    {
        return SATLINK_ERR_NULL;
    }
    const satlink_modcod_t *mc = satlink_modcod_get(header->modcod);
    const size_t total = satlink_frame_symbols(header->modcod);
    if ((mc == NULL) || (total > max_symbols) || (header->seq > 0x7FU) || (header->type > 1U))
    {
        return SATLINK_ERR_RANGE;
    }

    /* Information block + CRC, randomized. */
    if (header->type == (uint8_t)SATLINK_FRAME_IDLE)
    {
        satlink_pn9_fill(block, SATLINK_FRAME_INFO_BYTES);
    }
    else
    {
        for (size_t i = 0U; i < SATLINK_FRAME_INFO_BYTES; ++i)
        {
            block[i] = info[i];
        }
    }
    const uint16_t crc = satlink_crc16_ccitt(block, SATLINK_FRAME_INFO_BYTES);
    block[SATLINK_FRAME_INFO_BYTES] = (uint8_t)(crc >> 8U);
    block[SATLINK_FRAME_INFO_BYTES + 1U] = (uint8_t)(crc & 0xFFU);
    satlink_ccsds_randomizer_t rnd;
    satlink_ccsds_randomizer_reset(&rnd);
    (void)satlink_ccsds_randomizer_apply(&rnd, block, sizeof(block));

    /* FEC, pad, map. */
    const size_t nbits = satlink_conv_coded_bits(CODED_INFO_BITS, mc->rate);
    satlink_status_t status =
        satlink_conv_encode(block, CODED_INFO_BITS, mc->rate, coded, sizeof(coded));
    const size_t bps = (size_t)mc->modulation;
    const size_t npayload = (nbits + bps - 1U) / bps;
    for (size_t i = nbits; i < (npayload * bps); ++i)
    {
        coded[i] = 0U;
    }
    if (status == SATLINK_OK)
    {
        status = satlink_map(mc->modulation, coded, npayload * bps, payload,
                             SATLINK_FRAME_MAX_PAYLOAD_SYMBOLS);
    }
    if (status != SATLINK_OK)
    {
        return status;
    }

    /* Assemble: ASM, header, payload with pilots. */
    size_t n = 0U;
    const float *asm_sym = satlink_frame_asm_symbols();
    for (uint32_t i = 0U; i < SATLINK_FRAME_ASM_SYMBOLS; ++i)
    {
        symbols[n].re = asm_sym[i];
        symbols[n].im = 0.0F;
        ++n;
    }
    const uint16_t hdr = satlink_frame_header_encode(header);
    for (uint32_t i = 0U; i < SATLINK_FRAME_HDR_SYMBOLS; ++i)
    {
        const uint32_t bit = ((uint32_t)hdr >> (15U - (i % SATLINK_FRAME_HDR_BITS))) & 1U;
        symbols[n].re = (bit != 0U) ? -1.0F : 1.0F;
        symbols[n].im = 0.0F;
        ++n;
    }
    for (size_t i = 0U; i < npayload; ++i)
    {
        if ((i > 0U) && ((i % SATLINK_FRAME_PILOT_PERIOD) == 0U))
        {
            for (uint32_t p = 0U; p < SATLINK_FRAME_PILOT_LEN; ++p)
            {
                symbols[n].re = 1.0F;
                symbols[n].im = 0.0F;
                ++n;
            }
        }
        symbols[n] = payload[i];
        ++n;
    }
    *count = n;
    return SATLINK_OK;
}
