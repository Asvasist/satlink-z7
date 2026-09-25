/**
 * @file receiver.h
 * @brief Streaming software receiver for SatLink frames.
 *
 * Input: complex baseband at SATLINK_MODEM_SPS (2) samples per symbol after the RRC matched
 * filter (the PL's output, or satlink_fir_push() in simulation). Chain:
 *
 * 1. AGC on the symbol strobes.
 * 2. Symbol timing: Gardner detector, 8-tap windowed-sinc polyphase interpolator (64
 *    phases), PI loop.
 * 3. Frame sync: correlation with the BPSK ASM (phase-independent), which also gives the
 *    carrier phase and resolves the 180 degree ambiguity.
 * 4. Carrier phase: second-order PLL, decision-directed on data, data-aided on ASM and pilots.
 * 5. Header (soft-combined repetition, CRC-4), then the payload of the announced MODCOD.
 * 6. Es/N0 from the known symbols (ASM + pilots), soft demapping scaled by it, Viterbi,
 *    de-randomizing, CRC-16. Idle frames are compared with PN9 for a bit error count.
 *
 * No heap; one receiver object holds every buffer (about 30 KiB).
 *
 * @implements SRS-MDM-005
 */
#ifndef SATLINK_MODEM_RECEIVER_H
#define SATLINK_MODEM_RECEIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/modem/frame.h"
#include "satlink/modem/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One received frame, as handed to the callback. */
typedef struct
{
    satlink_frame_header_t header;
    bool crc_ok;
    uint8_t info[SATLINK_FRAME_INFO_BYTES];
    float esn0_db;         /**< Estimate from this frame's known symbols. */
    uint32_t bit_errors;   /**< IDLE frames: errors against PN9 (info part). */
    uint32_t bits_checked; /**< IDLE frames: 1024, otherwise 0. */
} satlink_rx_frame_t;

/** Running statistics. */
typedef struct
{
    uint32_t frames_ok;
    uint32_t frames_crc_error;
    uint32_t header_errors;
    uint32_t bit_errors; /**< Accumulated over IDLE frames. */
    uint32_t bits_checked;
    float esn0_db; /**< Smoothed over frames. */
    bool locked;   /**< A valid frame was received within the last two frame times. */
} satlink_rx_stats_t;

typedef void (*satlink_rx_frame_cb)(void *ctx, const satlink_rx_frame_t *frame);

typedef enum
{
    SATLINK_RX_HUNT = 0,
    SATLINK_RX_HEADER,
    SATLINK_RX_PAYLOAD,
} satlink_rx_state_t;

/** Taps and phases of the timing interpolator. */
#define SATLINK_RX_INTERP_TAPS   (8U)
#define SATLINK_RX_INTERP_PHASES (64U)

/** Receiver state. Treat as opaque. */
typedef struct
{
    satlink_rx_frame_cb cb;
    void *cb_ctx;

    /* AGC and timing */
    float agc_gain;
    satlink_cf_t hist[SATLINK_RX_INTERP_TAPS];
    float t_next;
    bool on_time_next;
    satlink_cf_t prev_on;
    satlink_cf_t mid;
    float timing_integ;

    /* carrier */
    float phase;
    float freq;

    /* frame state */
    satlink_rx_state_t state;
    satlink_cf_t window[SATLINK_FRAME_ASM_SYMBOLS];
    uint32_t window_fill;
    uint32_t window_pos;
    float hdr_soft[SATLINK_FRAME_HDR_BITS];
    uint32_t count;
    satlink_frame_header_t header;
    uint32_t payload_len;   /**< data symbols expected */
    uint32_t payload_count; /**< data symbols received */
    uint32_t pilot_count;   /**< pilot symbols received in the current block */
    satlink_cf_t payload[SATLINK_FRAME_MAX_PAYLOAD_SYMBOLS];
    /* known-symbol statistics for Es/N0 */
    float known_corr;
    float known_power;
    uint32_t known_n;
    satlink_cf_t known_z[SATLINK_FRAME_ASM_SYMBOLS + (4U * SATLINK_FRAME_PILOT_LEN)];
    float known_ref[SATLINK_FRAME_ASM_SYMBOLS + (4U * SATLINK_FRAME_PILOT_LEN)];

    uint32_t symbols_since_frame;
    satlink_rx_stats_t stats;
} satlink_receiver_t;

void satlink_rx_init(satlink_receiver_t *rx, satlink_rx_frame_cb cb, void *ctx);

/** Process @p count matched-filter output samples. */
void satlink_rx_push(satlink_receiver_t *rx, const satlink_cf_t *samples, size_t count);

/** Copy of the running statistics. */
satlink_rx_stats_t satlink_rx_stats(const satlink_receiver_t *rx);

/** Reset the statistics counters (not the synchronisation). */
void satlink_rx_clear_stats(satlink_receiver_t *rx);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_RECEIVER_H */
