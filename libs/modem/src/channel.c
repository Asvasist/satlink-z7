/**
 * @file channel.c
 * @implements SRS-MDM-006
 */
#include "satlink/modem/channel.h"

#include <math.h>

#define TWO_PI_F (6.28318531F)
#define PI_F     (3.14159265F)

void satlink_channel_init(satlink_channel_t *ch, uint32_t seed)
{
    ch->gain = 1.0F;
    ch->phase_rad = 0.0F;
    ch->freq_cps = 0.0F;
    ch->noise_sigma = 0.0F;
    ch->delay = 0.0F;
    for (uint32_t i = 0U; i < SATLINK_CHANNEL_DELAY_TAPS; ++i)
    {
        ch->hist[i].re = 0.0F;
        ch->hist[i].im = 0.0F;
        ch->taps[i] = 0.0F;
    }
    ch->taps_delay = -1.0F;
    ch->rng = (seed != 0U) ? seed : 1U;
    ch->has_spare = 0;
    ch->spare = 0.0F;
}

void satlink_channel_set_esn0(satlink_channel_t *ch, float esn0_db)
{
    /* Es = 1 per symbol before the matched filter (unit-energy taps), so N0 = 10^(-Es/N0 / 10)
     * and the complex noise per sample has variance N0. */
    ch->noise_sigma = sqrtf(powf(10.0F, -esn0_db / 10.0F));
}

static float uniform(satlink_channel_t *ch)
{
    uint32_t x = ch->rng;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    ch->rng = x;
    return ((float)(x >> 8U) + 0.5F) / 16777216.0F; /* (0, 1) */
}

float satlink_channel_gauss(satlink_channel_t *ch)
{
    if (ch->has_spare != 0)
    {
        ch->has_spare = 0;
        return ch->spare;
    }
    const float u1 = uniform(ch);
    const float u2 = uniform(ch);
    const float r = sqrtf(-2.0F * logf(u1));
    ch->spare = r * sinf(TWO_PI_F * u2);
    ch->has_spare = 1;
    return r * cosf(TWO_PI_F * u2);
}

static void design_delay(satlink_channel_t *ch)
{
    /* Blackman-windowed sinc centred at (N/2 - 1) + delay: a band-limited fractional delay
     * that leaves the signal undistorted up to about 0.8 of Nyquist. */
    const float n = (float)SATLINK_CHANNEL_DELAY_TAPS;
    const float centre = ((n / 2.0F) - 1.0F) + ch->delay;
    float sum = 0.0F;
    for (uint32_t k = 0U; k < SATLINK_CHANNEL_DELAY_TAPS; ++k)
    {
        const float x = (float)k - centre;
        const float sinc = (fabsf(x) < 1.0e-6F) ? 1.0F : (sinf(PI_F * x) / (PI_F * x));
        const float w = TWO_PI_F * ((float)k + 0.5F) / n;
        ch->taps[k] = sinc * (0.42F - (0.5F * cosf(w)) + (0.08F * cosf(2.0F * w)));
        sum += ch->taps[k];
    }
    for (uint32_t k = 0U; k < SATLINK_CHANNEL_DELAY_TAPS; ++k)
    {
        ch->taps[k] /= sum;
    }
    ch->taps_delay = ch->delay;
}

void satlink_channel_apply(satlink_channel_t *ch, satlink_cf_t *samples, size_t count)
{
    const float sigma = ch->noise_sigma * 0.70710678F; /* per real dimension */
    if (ch->taps_delay != ch->delay)
    {
        design_delay(ch);
    }
    for (size_t i = 0U; i < count; ++i)
    {
        /* Fractional delay: hist[0] is the oldest sample, tap k weights hist[k]. */
        for (uint32_t k = 0U; (k + 1U) < SATLINK_CHANNEL_DELAY_TAPS; ++k)
        {
            ch->hist[k] = ch->hist[k + 1U];
        }
        ch->hist[SATLINK_CHANNEL_DELAY_TAPS - 1U] = samples[i];
        satlink_cf_t x = {0.0F, 0.0F};
        for (uint32_t k = 0U; k < SATLINK_CHANNEL_DELAY_TAPS; ++k)
        {
            const float t = ch->taps[SATLINK_CHANNEL_DELAY_TAPS - 1U - k];
            x.re += t * ch->hist[k].re;
            x.im += t * ch->hist[k].im;
        }

        const float c = cosf(ch->phase_rad) * ch->gain;
        const float s = sinf(ch->phase_rad) * ch->gain;
        satlink_cf_t y;
        y.re = (x.re * c) - (x.im * s);
        y.im = (x.re * s) + (x.im * c);
        if (sigma > 0.0F)
        {
            y.re += sigma * satlink_channel_gauss(ch);
            y.im += sigma * satlink_channel_gauss(ch);
        }
        samples[i] = y;

        ch->phase_rad += TWO_PI_F * ch->freq_cps;
        if (ch->phase_rad > TWO_PI_F)
        {
            ch->phase_rad -= TWO_PI_F;
        }
        else if (ch->phase_rad < -TWO_PI_F)
        {
            ch->phase_rad += TWO_PI_F;
        }
    }
}
