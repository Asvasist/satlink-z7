/**
 * @file channel.h
 * @brief Channel model for simulation: gain, carrier phase and frequency offset, fractional
 *        timing offset and complex AWGN. Deterministic (seeded) so tests are reproducible.
 *
 * The same model runs in the PL channel emulator (noise and attenuation) and in the host
 * simulation of the whole link.
 *
 * @implements SRS-MDM-006
 */
#ifndef SATLINK_MODEM_CHANNEL_H
#define SATLINK_MODEM_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#include "satlink/modem/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Taps of the fractional-delay filter (adds SATLINK_CHANNEL_DELAY_TAPS / 2 whole samples). */
#define SATLINK_CHANNEL_DELAY_TAPS (16U)

typedef struct
{
    float gain;        /**< Linear amplitude gain. */
    float phase_rad;   /**< Current carrier phase. */
    float freq_cps;    /**< Carrier frequency offset in cycles per sample. */
    float noise_sigma; /**< Std. deviation of the complex noise (per sample, total). */
    float delay;       /**< Fractional timing offset in samples, 0 <= delay < 1. */
    satlink_cf_t hist[SATLINK_CHANNEL_DELAY_TAPS]; /**< Windowed-sinc delay line. */
    float taps[SATLINK_CHANNEL_DELAY_TAPS];
    float taps_delay; /**< Delay the taps were computed for. */
    uint32_t rng;     /**< xorshift32 state. */
    int has_spare;
    float spare;
} satlink_channel_t;

/** Noise-free unit channel. @p seed must not be 0. */
void satlink_channel_init(satlink_channel_t *ch, uint32_t seed);

/** Set the noise so that Es/N0 = @p esn0_db at the RRC matched-filter output. */
void satlink_channel_set_esn0(satlink_channel_t *ch, float esn0_db);

/** Apply the channel to @p count samples in place. */
void satlink_channel_apply(satlink_channel_t *ch, satlink_cf_t *samples, size_t count);

/** One standard normal random number (Box-Muller on xorshift32). */
float satlink_channel_gauss(satlink_channel_t *ch);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_CHANNEL_H */
