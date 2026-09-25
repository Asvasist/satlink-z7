/**
 * @file rrc.h
 * @brief Root-raised-cosine pulse shaping and matched filtering.
 *
 * The PL shapes the TX symbols at 48 kS/s (8 samples per symbol) and delivers the RX baseband
 * matched-filtered and decimated to 12 kS/s (2 samples per symbol, docs/icd section 2). The
 * software side works at the RX rate: the receiver, the software loopback and the host
 * simulation all run at SATLINK_MODEM_SPS = 2. satlink_rrc_design() also produces the PL's
 * 8-samples-per-symbol taps, so it is the reference model for both.
 *
 * Taps are normalised to unit energy, so a unit symbol through the TX and RX filters peaks at 1
 * and white noise keeps its variance: Es/N0 = 1 / sigma^2 for complex noise of variance
 * sigma^2 per sample.
 *
 * @implements SRS-MDM-003
 */
#ifndef SATLINK_MODEM_RRC_H
#define SATLINK_MODEM_RRC_H

#include <stddef.h>
#include <stdint.h>

#include "satlink/common/status.h"
#include "satlink/modem/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Samples per symbol of the RX baseband stream: 12 kS/s / 6 kSym/s. */
#define SATLINK_MODEM_SPS (2U)
/** Samples per symbol of the PL's TX pulse shaper (48 kS/s codec rate). */
#define SATLINK_MODEM_PL_TX_SPS (8U)
/** Filter span in symbols. */
#define SATLINK_RRC_SPAN (8U)
/** Number of taps (span * sps + 1). */
#define SATLINK_RRC_TAPS ((SATLINK_RRC_SPAN * SATLINK_MODEM_SPS) + 1U)
/** Roll-off factor. */
#define SATLINK_RRC_ALPHA (0.35F)

/** Design @p ntaps RRC taps (odd count), roll-off @p alpha, @p sps samples per symbol. */
satlink_status_t satlink_rrc_design(float alpha, uint32_t sps, float *taps, size_t ntaps);

/** Complex FIR with real taps. */
typedef struct
{
    float taps[SATLINK_RRC_TAPS];
    satlink_cf_t hist[SATLINK_RRC_TAPS];
    size_t ntaps;
    size_t pos;
} satlink_fir_t;

/** Initialise with the standard RRC taps. */
void satlink_fir_init_rrc(satlink_fir_t *fir);

/** Push one input sample, return one output sample. */
satlink_cf_t satlink_fir_push(satlink_fir_t *fir, satlink_cf_t x);

/**
 * @brief Pulse-shape @p count symbols: each symbol becomes SATLINK_MODEM_SPS samples.
 * @p out must hold count * SATLINK_MODEM_SPS samples.
 */
void satlink_rrc_interpolate(satlink_fir_t *fir, const satlink_cf_t *symbols, size_t count,
                             satlink_cf_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_RRC_H */
