/**
 * @file receiver.c
 * @implements SRS-MDM-005
 */
#include "satlink/modem/receiver.h"

#include <math.h>

#include "satlink/common/ccsds_randomizer.h"
#include "satlink/common/crc16_ccitt.h"
#include "satlink/modem/conv.h"
#include "satlink/modem/mapper.h"
#include "satlink/modem/prbs.h"
#include "satlink/modem/rrc.h"

/* PLL_KP / PLL_KI: carrier loop gains per symbol (loop bandwidth about 0.5 % of the symbol
 * rate, damping 0.7). The detector is decision-directed, so its self-noise at low Es/N0 sets the
 * bandwidth: 0.08 / 0.002 cost 2 dB on QPSK 1/2 and 8PSK 2/3 (docs/performance). */
#define TWO_PI_F        (6.28318531F)
#define PI_F            (3.14159265F)
#define HALF_STEP       ((float)SATLINK_MODEM_SPS / 2.0F)
#define AGC_RATE        (0.002F)
#define TIMING_KP       (0.02F)
#define TIMING_KI       (0.0001F)
#define PLL_KP          (0.02F)
#define PLL_KI          (0.0002F)
#define SYNC_THRESHOLD  (0.35F)
#define ESN0_SMOOTHING  (0.2F)
#define CODED_INFO_BITS ((size_t)(SATLINK_FRAME_INFO_BYTES + 2U) * 8U)
#define MAX_SOFT        ((size_t)3U * SATLINK_FRAME_MAX_PAYLOAD_SYMBOLS)

static int8_t g_soft[MAX_SOFT];
/* Polyphase interpolator: phase p holds the taps for a fractional delay of p / PHASES. */
static float g_interp[SATLINK_RX_INTERP_PHASES + 1U][SATLINK_RX_INTERP_TAPS];
static bool g_interp_ready = false;
static uint8_t g_block[SATLINK_FRAME_INFO_BYTES + 2U];
static uint8_t g_pn9[SATLINK_FRAME_INFO_BYTES];
static bool g_pn9_ready = false;

static satlink_cf_t cmul(satlink_cf_t a, satlink_cf_t b)
{
    satlink_cf_t r;
    r.re = (a.re * b.re) - (a.im * b.im);
    r.im = (a.re * b.im) + (a.im * b.re);
    return r;
}

static satlink_cf_t cconj(satlink_cf_t a)
{
    satlink_cf_t r = {a.re, -a.im};
    return r;
}

static satlink_cf_t rotate(satlink_cf_t a, float phase)
{
    const satlink_cf_t e = {cosf(phase), sinf(phase)};
    return cmul(a, e);
}

static float wrap_pi(float x)
{
    float y = x;
    while (y > PI_F)
    {
        y -= TWO_PI_F;
    }
    while (y < -PI_F)
    {
        y += TWO_PI_F;
    }
    return y;
}

static void init_interpolator(void)
{
    /* Blackman-windowed sinc, normalised to unit DC gain for every phase. Tap k sits at
     * x = k - 3 relative to hist[3]; phase p interpolates at x = p / PHASES. */
    const float half = (float)SATLINK_RX_INTERP_TAPS / 2.0F;
    for (uint32_t p = 0U; p <= SATLINK_RX_INTERP_PHASES; ++p)
    {
        const float mu = (float)p / (float)SATLINK_RX_INTERP_PHASES;
        float sum = 0.0F;
        for (uint32_t k = 0U; k < SATLINK_RX_INTERP_TAPS; ++k)
        {
            const float x = ((float)k - (half - 1.0F)) - mu;
            const float sinc = (fabsf(x) < 1.0e-6F) ? 1.0F : (sinf(PI_F * x) / (PI_F * x));
            const float w_arg = PI_F * (x + half) / half; /* 0 .. 2 pi across the window */
            const float window = 0.42F - (0.5F * cosf(w_arg)) + (0.08F * cosf(2.0F * w_arg));
            g_interp[p][k] = sinc * window;
            sum += g_interp[p][k];
        }
        for (uint32_t k = 0U; k < SATLINK_RX_INTERP_TAPS; ++k)
        {
            g_interp[p][k] /= sum;
        }
    }
    g_interp_ready = true;
}

void satlink_rx_clear_stats(satlink_receiver_t *rx)
{
    const bool locked = rx->stats.locked;
    const float esn0 = rx->stats.esn0_db;
    rx->stats.frames_ok = 0U;
    rx->stats.frames_crc_error = 0U;
    rx->stats.header_errors = 0U;
    rx->stats.bit_errors = 0U;
    rx->stats.bits_checked = 0U;
    rx->stats.locked = locked;
    rx->stats.esn0_db = esn0;
}

static void enter_hunt(satlink_receiver_t *rx)
{
    rx->state = SATLINK_RX_HUNT;
    rx->window_fill = 0U;
    rx->window_pos = 0U;
}

void satlink_rx_init(satlink_receiver_t *rx, satlink_rx_frame_cb cb, void *ctx)
{
    rx->cb = cb;
    rx->cb_ctx = ctx;
    rx->agc_gain = 1.0F;
    for (uint32_t i = 0U; i < SATLINK_RX_INTERP_TAPS; ++i)
    {
        rx->hist[i].re = 0.0F;
        rx->hist[i].im = 0.0F;
    }
    if (!g_interp_ready)
    {
        init_interpolator();
    }
    rx->t_next = 0.0F;
    rx->on_time_next = true;
    rx->prev_on.re = 0.0F;
    rx->prev_on.im = 0.0F;
    rx->mid = rx->prev_on;
    rx->timing_integ = 0.0F;
    rx->phase = 0.0F;
    rx->freq = 0.0F;
    rx->symbols_since_frame = 0U;
    rx->stats.esn0_db = 0.0F;
    rx->stats.locked = false;
    satlink_rx_clear_stats(rx);
    enter_hunt(rx);
    if (!g_pn9_ready)
    {
        satlink_pn9_fill(g_pn9, sizeof(g_pn9));
        g_pn9_ready = true;
    }
}

satlink_rx_stats_t satlink_rx_stats(const satlink_receiver_t *rx)
{
    return rx->stats;
}

/* Carrier PLL update with the error between @p z (derotated) and the reference @p ref. */
static void pll_update(satlink_receiver_t *rx, satlink_cf_t z, satlink_cf_t ref)
{
    const satlink_cf_t e = cmul(z, cconj(ref));
    const float err = atan2f(e.im, e.re);
    rx->freq += PLL_KI * err;
    rx->phase = wrap_pi(rx->phase + (PLL_KP * err));
}

static void add_known(satlink_receiver_t *rx, satlink_cf_t z, float ref)
{
    const uint32_t cap = (uint32_t)(sizeof(rx->known_ref) / sizeof(rx->known_ref[0]));
    if (rx->known_n < cap)
    {
        rx->known_z[rx->known_n] = z;
        rx->known_ref[rx->known_n] = ref;
        ++rx->known_n;
    }
}

static void finish_frame(satlink_receiver_t *rx)
{
    const satlink_modcod_t *mc = satlink_modcod_get(rx->header.modcod);
    satlink_rx_frame_t out;
    out.header = rx->header;
    out.crc_ok = false;
    out.bit_errors = 0U;
    out.bits_checked = 0U;

    /* Es/N0 from the known symbols: amplitude and noise around it. */
    float g = 0.0F;
    for (uint32_t i = 0U; i < rx->known_n; ++i)
    {
        g += rx->known_z[i].re * rx->known_ref[i];
    }
    g /= (float)rx->known_n;
    float noise = 0.0F;
    for (uint32_t i = 0U; i < rx->known_n; ++i)
    {
        const float dr = rx->known_z[i].re - (g * rx->known_ref[i]);
        const float di = rx->known_z[i].im;
        noise += (dr * dr) + (di * di);
    }
    noise /= (float)rx->known_n;
    if (noise < 1.0e-9F)
    {
        noise = 1.0e-9F;
    }
    if (g < 1.0e-6F)
    {
        g = 1.0e-6F;
    }
    const float snr = (g * g) / noise;
    out.esn0_db = 10.0F * log10f(snr);

    /* Normalise the payload to unit amplitude; LLR scale 4/N0 limited to keep int8 useful. */
    const float inv_g = 1.0F / g;
    for (uint32_t i = 0U; i < rx->payload_len; ++i)
    {
        rx->payload[i].re *= inv_g;
        rx->payload[i].im *= inv_g;
    }
    float scale = snr;
    if (scale > 16.0F)
    {
        scale = 16.0F;
    }
    const size_t nbits = satlink_conv_coded_bits(CODED_INFO_BITS, mc->rate);
    (void)satlink_demap(mc->modulation, rx->payload, rx->payload_len, scale, g_soft, MAX_SOFT);
    if (satlink_conv_decode(g_soft, nbits, CODED_INFO_BITS, mc->rate, g_block, NULL) == SATLINK_OK)
    {
        satlink_ccsds_randomizer_t rnd;
        satlink_ccsds_randomizer_reset(&rnd);
        (void)satlink_ccsds_randomizer_apply(&rnd, g_block, sizeof(g_block));
        const uint16_t crc = satlink_crc16_ccitt(g_block, sizeof(g_block));
        out.crc_ok = (crc == 0U);
        for (uint32_t i = 0U; i < SATLINK_FRAME_INFO_BYTES; ++i)
        {
            out.info[i] = g_block[i];
        }
    }
    if (out.header.type == (uint8_t)SATLINK_FRAME_IDLE)
    {
        out.bit_errors = satlink_bit_errors(out.info, g_pn9, SATLINK_FRAME_INFO_BYTES);
        out.bits_checked = SATLINK_FRAME_INFO_BYTES * 8U;
        rx->stats.bit_errors += out.bit_errors;
        rx->stats.bits_checked += out.bits_checked;
    }
    if (out.crc_ok)
    {
        ++rx->stats.frames_ok;
    }
    else
    {
        ++rx->stats.frames_crc_error;
    }
    const float smoothed =
        (rx->stats.frames_ok + rx->stats.frames_crc_error) == 1U
            ? out.esn0_db
            : rx->stats.esn0_db + (ESN0_SMOOTHING * (out.esn0_db - rx->stats.esn0_db));
    rx->stats.esn0_db = smoothed;
    if (out.crc_ok)
    {
        /* Locked means frames get through, not just that headers decode. */
        rx->stats.locked = true;
        rx->symbols_since_frame = 0U;
    }
    if (rx->cb != NULL)
    {
        rx->cb(rx->cb_ctx, &out);
    }
}

static void hunt(satlink_receiver_t *rx, satlink_cf_t y)
{
    rx->window[rx->window_pos] = y;
    rx->window_pos = (rx->window_pos + 1U) % SATLINK_FRAME_ASM_SYMBOLS;
    if (rx->window_fill < SATLINK_FRAME_ASM_SYMBOLS)
    {
        ++rx->window_fill;
        if (rx->window_fill < SATLINK_FRAME_ASM_SYMBOLS)
        {
            return;
        }
    }
    const float *asm_sym = satlink_frame_asm_symbols();
    satlink_cf_t c = {0.0F, 0.0F};
    float energy = 0.0F;
    for (uint32_t i = 0U; i < SATLINK_FRAME_ASM_SYMBOLS; ++i)
    {
        const satlink_cf_t w = rx->window[(rx->window_pos + i) % SATLINK_FRAME_ASM_SYMBOLS];
        c.re += asm_sym[i] * w.re;
        c.im += asm_sym[i] * w.im;
        energy += (w.re * w.re) + (w.im * w.im);
    }
    const float metric =
        ((c.re * c.re) + (c.im * c.im)) / (((float)SATLINK_FRAME_ASM_SYMBOLS * energy) + 1.0e-12F);
    if (metric < SYNC_THRESHOLD)
    {
        return;
    }
    /* ASM found: phase from the correlation, known symbols for the SNR estimate. */
    rx->phase = atan2f(c.im, c.re);
    rx->known_n = 0U;
    for (uint32_t i = 0U; i < SATLINK_FRAME_ASM_SYMBOLS; ++i)
    {
        const satlink_cf_t w = rx->window[(rx->window_pos + i) % SATLINK_FRAME_ASM_SYMBOLS];
        add_known(rx, rotate(w, -rx->phase), asm_sym[i]);
    }
    for (uint32_t i = 0U; i < SATLINK_FRAME_HDR_BITS; ++i)
    {
        rx->hdr_soft[i] = 0.0F;
    }
    rx->count = 0U;
    rx->state = SATLINK_RX_HEADER;
}

static void header_symbol(satlink_receiver_t *rx, satlink_cf_t y)
{
    const satlink_cf_t z = rotate(y, -rx->phase);
    const satlink_cf_t d = satlink_slice(SATLINK_MOD_BPSK, z);
    pll_update(rx, z, d);
    rx->hdr_soft[rx->count % SATLINK_FRAME_HDR_BITS] += z.re;
    ++rx->count;
    if (rx->count < SATLINK_FRAME_HDR_SYMBOLS)
    {
        return;
    }
    uint16_t bits = 0U;
    for (uint32_t i = 0U; i < SATLINK_FRAME_HDR_BITS; ++i)
    {
        bits = (uint16_t)((uint32_t)bits << 1U);
        if (rx->hdr_soft[i] < 0.0F)
        {
            bits |= 1U;
        }
    }
    if (!satlink_frame_header_decode(bits, &rx->header))
    {
        ++rx->stats.header_errors;
        enter_hunt(rx);
        return;
    }
    rx->payload_len = (uint32_t)satlink_frame_payload_symbols(rx->header.modcod);
    rx->payload_count = 0U;
    rx->pilot_count = 0U;
    rx->state = SATLINK_RX_PAYLOAD;
}

static void payload_symbol(satlink_receiver_t *rx, satlink_cf_t y)
{
    const satlink_cf_t z = rotate(y, -rx->phase);
    const bool pilot_due = (rx->payload_count > 0U) &&
                           ((rx->payload_count % SATLINK_FRAME_PILOT_PERIOD) == 0U) &&
                           (rx->pilot_count < SATLINK_FRAME_PILOT_LEN);
    if (pilot_due)
    {
        const satlink_cf_t ref = {1.0F, 0.0F};
        pll_update(rx, z, ref);
        add_known(rx, z, 1.0F);
        ++rx->pilot_count;
        return;
    }
    const satlink_modcod_t *mc = satlink_modcod_get(rx->header.modcod);
    pll_update(rx, z, satlink_slice(mc->modulation, z));
    rx->payload[rx->payload_count] = z;
    ++rx->payload_count;
    rx->pilot_count = 0U;
    if (rx->payload_count == rx->payload_len)
    {
        finish_frame(rx);
        enter_hunt(rx);
    }
}

static void symbol(satlink_receiver_t *rx, satlink_cf_t y)
{
    rx->phase = wrap_pi(rx->phase + rx->freq);
    ++rx->symbols_since_frame;
    if (rx->symbols_since_frame > (2U * SATLINK_FRAME_MAX_SYMBOLS))
    {
        rx->stats.locked = false;
    }
    switch (rx->state)
    {
    case SATLINK_RX_HUNT:
        hunt(rx, y);
        break;
    case SATLINK_RX_HEADER:
        header_symbol(rx, y);
        break;
    default:
        payload_symbol(rx, y);
        break;
    }
}

static satlink_cf_t interpolate(const satlink_cf_t h[SATLINK_RX_INTERP_TAPS], float mu)
{
    /* mu in [0, 1] between h[3] and h[4]: nearest of the 64 phases (error < 1/128 sample). */
    const uint32_t p = (uint32_t)lrintf(mu * (float)SATLINK_RX_INTERP_PHASES);
    const float *taps = g_interp[(p > SATLINK_RX_INTERP_PHASES) ? SATLINK_RX_INTERP_PHASES : p];
    satlink_cf_t y = {0.0F, 0.0F};
    for (uint32_t k = 0U; k < SATLINK_RX_INTERP_TAPS; ++k)
    {
        y.re += taps[k] * h[k].re;
        y.im += taps[k] * h[k].im;
    }
    return y;
}

void satlink_rx_push(satlink_receiver_t *rx, const satlink_cf_t *samples, size_t count)
{
    for (size_t n = 0U; n < count; ++n)
    {
        for (uint32_t k = 0U; (k + 1U) < SATLINK_RX_INTERP_TAPS; ++k)
        {
            rx->hist[k] = rx->hist[k + 1U];
        }
        rx->hist[SATLINK_RX_INTERP_TAPS - 1U].re = samples[n].re * rx->agc_gain;
        rx->hist[SATLINK_RX_INTERP_TAPS - 1U].im = samples[n].im * rx->agc_gain;
        rx->t_next -= 1.0F;
        /* Strobe times are relative to the newest sample (hist[7]); the interpolator works
         * between hist[3] and hist[4], i.e. t in [-4, -3]. At 2 samples per symbol a strobe is
         * due about every sample; after a negative correction two can fall into one sample. */
        while (rx->t_next <= -3.0F)
        {
            if (rx->t_next < -4.0F)
            {
                rx->t_next = -4.0F;
            }
            const satlink_cf_t y = interpolate(rx->hist, rx->t_next + 4.0F);
            if (!rx->on_time_next)
            {
                rx->mid = y;
                rx->on_time_next = true;
                rx->t_next += HALF_STEP;
                continue;
            }
            rx->on_time_next = false;

            /* Gardner: e > 0 when the strobes are early, so the next one is pushed later. */
            const float e =
                ((rx->prev_on.re - y.re) * rx->mid.re) + ((rx->prev_on.im - y.im) * rx->mid.im);
            rx->prev_on = y;
            rx->timing_integ += TIMING_KI * e;
            rx->t_next += HALF_STEP + (TIMING_KP * e) + rx->timing_integ;

            const float power = (y.re * y.re) + (y.im * y.im);
            rx->agc_gain *= 1.0F + (AGC_RATE * (1.0F - power));
            if (rx->agc_gain < 1.0e-3F)
            {
                rx->agc_gain = 1.0e-3F;
            }
            symbol(rx, y);
        }
    }
}
