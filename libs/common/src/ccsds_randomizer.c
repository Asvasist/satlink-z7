/**
 * @file ccsds_randomizer.c
 * @brief CCSDS pseudo-randomizer, h(x) = x^8 + x^7 + x^5 + x^3 + 1.
 *
 * Fibonacci LFSR with the register stored MSB-first: bit 7 is the output bit, and the
 * feedback taps are bits 7, 4, 2 and 0.
 *
 * @implements SRS-LIB-002
 */
#include "satlink/common/ccsds_randomizer.h"

void satlink_ccsds_randomizer_reset(satlink_ccsds_randomizer_t *ctx)
{
    if (ctx != NULL)
    {
        ctx->lfsr = (uint8_t)SATLINK_CCSDS_RANDOMIZER_SEED;
    }
}

uint8_t satlink_ccsds_randomizer_next(satlink_ccsds_randomizer_t *ctx)
{
    uint32_t out = 0U;

    if (ctx != NULL)
    {
        uint32_t state = (uint32_t)ctx->lfsr;

        for (uint32_t bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t feedback = ((state >> 7U) ^ (state >> 4U) ^ (state >> 2U) ^ state) & 1U;
            out = (out << 1U) | ((state >> 7U) & 1U);
            state = ((state << 1U) | feedback) & 0xFFU;
        }

        ctx->lfsr = (uint8_t)state;
    }

    return (uint8_t)out;
}

satlink_status_t satlink_ccsds_randomizer_apply(satlink_ccsds_randomizer_t *ctx, uint8_t *data,
                                                size_t len)
{
    satlink_status_t status = SATLINK_OK;

    if ((ctx == NULL) || ((data == NULL) && (len > 0U)))
    {
        status = SATLINK_ERR_NULL;
    }
    else
    {
        for (size_t i = 0U; i < len; ++i)
        {
            data[i] = (uint8_t)(data[i] ^ satlink_ccsds_randomizer_next(ctx));
        }
    }

    return status;
}

satlink_status_t satlink_ccsds_randomize_frame(uint8_t *frame, size_t len)
{
    satlink_ccsds_randomizer_t ctx;
    satlink_ccsds_randomizer_reset(&ctx);
    return satlink_ccsds_randomizer_apply(&ctx, frame, len);
}
