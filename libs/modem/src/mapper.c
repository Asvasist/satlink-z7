/**
 * @file mapper.c
 * @implements SRS-MDM-002
 */
#include "satlink/modem/mapper.h"

#include <math.h>

#define INV_SQRT2 (0.70710678F)

static const float k_8psk_cos[8] = {1.0F,  INV_SQRT2,  0.0F, -INV_SQRT2,
                                    -1.0F, -INV_SQRT2, 0.0F, INV_SQRT2};
static const float k_8psk_sin[8] = {0.0F, INV_SQRT2,  1.0F,  INV_SQRT2,
                                    0.0F, -INV_SQRT2, -1.0F, -INV_SQRT2};

static uint32_t gray(uint32_t m)
{
    return m ^ (m >> 1U);
}

static int8_t clamp_soft(float v)
{
    if (v > 127.0F)
    {
        return 127;
    }
    if (v < -127.0F)
    {
        return -127;
    }
    return (int8_t)lrintf(v);
}

satlink_status_t satlink_map(satlink_modulation_t mod, const uint8_t *bits, size_t nbits,
                             satlink_cf_t *symbols, size_t max_symbols)
{
    if ((bits == NULL) || (symbols == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    const size_t bps = (size_t)mod;
    if ((bps < 1U) || (bps > 3U) || ((nbits % bps) != 0U) || ((nbits / bps) > max_symbols))
    {
        return SATLINK_ERR_RANGE;
    }
    for (size_t k = 0U; k < (nbits / bps); ++k)
    {
        const uint8_t *b = &bits[k * bps];
        satlink_cf_t s = {0.0F, 0.0F};
        if (mod == SATLINK_MOD_BPSK)
        {
            s.re = (b[0] != 0U) ? -1.0F : 1.0F;
        }
        else if (mod == SATLINK_MOD_QPSK)
        {
            s.re = (b[0] != 0U) ? -INV_SQRT2 : INV_SQRT2;
            s.im = (b[1] != 0U) ? -INV_SQRT2 : INV_SQRT2;
        }
        else
        {
            const uint32_t label = ((uint32_t)(b[0] & 1U) << 2U) | ((uint32_t)(b[1] & 1U) << 1U) |
                                   (uint32_t)(b[2] & 1U);
            for (uint32_t m = 0U; m < 8U; ++m)
            {
                if (gray(m) == label)
                {
                    s.re = k_8psk_cos[m];
                    s.im = k_8psk_sin[m];
                }
            }
        }
        symbols[k] = s;
    }
    return SATLINK_OK;
}

static float dist2(satlink_cf_t y, float re, float im)
{
    const float dr = y.re - re;
    const float di = y.im - im;
    return (dr * dr) + (di * di);
}

satlink_status_t satlink_demap(satlink_modulation_t mod, const satlink_cf_t *symbols, size_t count,
                               float scale, int8_t *soft, size_t max_soft)
{
    if ((symbols == NULL) || (soft == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    const size_t bps = (size_t)mod;
    if ((bps < 1U) || (bps > 3U) || ((count * bps) > max_soft))
    {
        return SATLINK_ERR_RANGE;
    }
    for (size_t k = 0U; k < count; ++k)
    {
        const satlink_cf_t y = symbols[k];
        if (mod == SATLINK_MOD_BPSK)
        {
            /* LLR = (|y+1|^2 - |y-1|^2) / N0 = 4 y.re / N0 */
            soft[k] = clamp_soft(scale * 4.0F * y.re);
        }
        else if (mod == SATLINK_MOD_QPSK)
        {
            soft[2U * k] = clamp_soft(scale * 4.0F * INV_SQRT2 * y.re);
            soft[(2U * k) + 1U] = clamp_soft(scale * 4.0F * INV_SQRT2 * y.im);
        }
        else
        {
            float d[8];
            for (uint32_t m = 0U; m < 8U; ++m)
            {
                d[m] = dist2(y, k_8psk_cos[m], k_8psk_sin[m]);
            }
            for (uint32_t bit = 0U; bit < 3U; ++bit)
            {
                const uint32_t mask = 4U >> bit;
                float best0 = 1.0e9F;
                float best1 = 1.0e9F;
                for (uint32_t m = 0U; m < 8U; ++m)
                {
                    if ((gray(m) & mask) != 0U)
                    {
                        best1 = (d[m] < best1) ? d[m] : best1;
                    }
                    else
                    {
                        best0 = (d[m] < best0) ? d[m] : best0;
                    }
                }
                soft[(3U * k) + bit] = clamp_soft(scale * (best1 - best0));
            }
        }
    }
    return SATLINK_OK;
}

satlink_cf_t satlink_slice(satlink_modulation_t mod, satlink_cf_t y)
{
    satlink_cf_t s = {0.0F, 0.0F};
    if (mod == SATLINK_MOD_BPSK)
    {
        s.re = (y.re >= 0.0F) ? 1.0F : -1.0F;
    }
    else if (mod == SATLINK_MOD_QPSK)
    {
        s.re = (y.re >= 0.0F) ? INV_SQRT2 : -INV_SQRT2;
        s.im = (y.im >= 0.0F) ? INV_SQRT2 : -INV_SQRT2;
    }
    else
    {
        uint32_t best = 0U;
        float best_d = 1.0e9F;
        for (uint32_t m = 0U; m < 8U; ++m)
        {
            const float d = dist2(y, k_8psk_cos[m], k_8psk_sin[m]);
            if (d < best_d)
            {
                best_d = d;
                best = m;
            }
        }
        s.re = k_8psk_cos[best];
        s.im = k_8psk_sin[best];
    }
    return s;
}
