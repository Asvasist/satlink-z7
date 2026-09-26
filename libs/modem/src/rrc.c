/**
 * @file rrc.c
 * @implements SRS-MDM-003
 */
#include "satlink/modem/rrc.h"

#include <math.h>

#define PI_F (3.14159265F)

satlink_status_t satlink_rrc_design(float alpha, uint32_t sps, float *taps, size_t ntaps)
{
    if (taps == NULL)
    {
        return SATLINK_ERR_NULL;
    }
    if (((ntaps % 2U) == 0U) || (sps == 0U) || (alpha <= 0.0F) || (alpha > 1.0F))
    {
        return SATLINK_ERR_RANGE;
    }
    const float half = (float)(ntaps - 1U) / 2.0F; /* ntaps is odd: the centre tap */
    float energy = 0.0F;
    for (size_t i = 0U; i < ntaps; ++i)
    {
        const float t = ((float)i - half) / (float)sps; /* in symbol periods */
        float h = 0.0F;
        if (fabsf(t) < 1.0e-6F)
        {
            h = 1.0F - alpha + (4.0F * alpha / PI_F);
        }
        else if (fabsf(fabsf(4.0F * alpha * t) - 1.0F) < 1.0e-6F)
        {
            h = (alpha / sqrtf(2.0F)) * (((1.0F + (2.0F / PI_F)) * sinf(PI_F / (4.0F * alpha))) +
                                         ((1.0F - (2.0F / PI_F)) * cosf(PI_F / (4.0F * alpha))));
        }
        else
        {
            const float num = sinf(PI_F * t * (1.0F - alpha)) +
                              (4.0F * alpha * t * cosf(PI_F * t * (1.0F + alpha)));
            const float den = PI_F * t * (1.0F - ((4.0F * alpha * t) * (4.0F * alpha * t)));
            h = num / den;
        }
        taps[i] = h;
        energy += h * h;
    }
    const float norm = 1.0F / sqrtf(energy);
    for (size_t i = 0U; i < ntaps; ++i)
    {
        taps[i] *= norm;
    }
    return SATLINK_OK;
}

void satlink_fir_init_rrc(satlink_fir_t *fir)
{
    (void)satlink_rrc_design(SATLINK_RRC_ALPHA, SATLINK_MODEM_SPS, fir->taps, SATLINK_RRC_TAPS);
    fir->ntaps = SATLINK_RRC_TAPS;
    fir->pos = 0U;
    for (size_t i = 0U; i < SATLINK_RRC_TAPS; ++i)
    {
        fir->hist[i].re = 0.0F;
        fir->hist[i].im = 0.0F;
    }
}

satlink_cf_t satlink_fir_push(satlink_fir_t *fir, satlink_cf_t x)
{
    const size_t n =
        ((fir->ntaps == 0U) || (fir->ntaps > SATLINK_RRC_TAPS)) ? SATLINK_RRC_TAPS : fir->ntaps;
    const size_t pos = (fir->pos < n) ? fir->pos : 0U;
    fir->hist[pos] = x;
    satlink_cf_t acc = {0.0F, 0.0F};
    size_t idx = pos;
    for (size_t k = 0U; k < n; ++k)
    {
        acc.re += fir->taps[k] * fir->hist[idx].re;
        acc.im += fir->taps[k] * fir->hist[idx].im;
        idx = (idx == 0U) ? (n - 1U) : (idx - 1U);
    }
    fir->pos = (pos + 1U) % n;
    return acc;
}

void satlink_rrc_interpolate(satlink_fir_t *fir, const satlink_cf_t *symbols, size_t count,
                             satlink_cf_t *out)
{
    const satlink_cf_t zero = {0.0F, 0.0F};
    for (size_t k = 0U; k < count; ++k)
    {
        for (uint32_t s = 0U; s < SATLINK_MODEM_SPS; ++s)
        {
            out[(k * SATLINK_MODEM_SPS) + s] = satlink_fir_push(fir, (s == 0U) ? symbols[k] : zero);
        }
    }
}
