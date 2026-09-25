/**
 * @file modem_app.h
 * @brief The modem application that runs on Core 1: message handling, TX frame scheduling,
 *        RX frame delivery, status reporting and a software loopback. Free of OS and register
 *        access, so it runs unchanged in the FreeRTOS firmware, in the host simulator of the
 *        payload (Stage 5) and in unit tests.
 *
 * Data flow on the target:
 *
 * ```
 * Linux --TX_FRAME--> queue --> frame builder --symbols--> AXI DMA --> PL: RRC x8, NCO, I2S
 * Linux <--RX_FRAME-- receiver <--samples (2/sym)-- AXI DMA <-- PL: I2S, NCO, RRC matched, decimate
 * ```
 *
 * With nothing queued the transmitter sends IDLE frames (PN9 payload), so the receiver always
 * has a signal to track and measures the bit error rate continuously. With ACM enabled
 * (ACM_CONFIG) the MODCOD of every frame follows the receiver's Es/N0 (libs/acm), and every
 * change is reported to Linux as a LOG message.
 *
 * @implements SRS-MDM-007
 */
#ifndef SATLINK_MODEM_APP_H
#define SATLINK_MODEM_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/acm/acm.h"
#include "satlink/amp/msg.h"
#include "satlink/common/status.h"
#include "satlink/modem/channel.h"
#include "satlink/modem/frame.h"
#include "satlink/modem/receiver.h"
#include "satlink/modem/rrc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** TX frames buffered from Linux. */
#define SATLINK_MODEM_APP_TX_QUEUE (16U)

/** Samples per software-loopback step (one DMA block's worth). */
#define SATLINK_MODEM_APP_LOOP_SYMBOLS (64U)

/** Hardware and IPC access the application needs; members may be NULL where noted. */
typedef struct
{
    void *ctx;
    /** Send one message to Linux. Required. */
    satlink_status_t (*send_msg)(void *ctx, uint16_t type, const uint8_t *payload, uint16_t len);
    /** Program the PL channel emulator (payload_ctrl NOISE_LEVEL / ATTEN). May be NULL. */
    void (*set_channel)(void *ctx, uint16_t noise_level, uint16_t gain_q15);
    /** Select the PL loopback (payload_ctrl DIG_LOOPBACK). May be NULL. */
    void (*set_loopback)(void *ctx, uint8_t mode);
} satlink_modem_app_hw_t;

/** Counters beyond the receiver's own. */
typedef struct
{
    uint32_t tx_data_frames;
    uint32_t tx_idle_frames;
    uint32_t tx_queue_overflows;
    uint32_t rx_frames_forwarded;
    uint32_t msg_errors;
} satlink_modem_app_counters_t;

/** Application state (large: holds the receiver and a frame of symbols). Treat as opaque. */
typedef struct
{
    satlink_modem_app_hw_t hw;
    satlink_msg_modem_config_t config;
    satlink_msg_channel_t channel_cfg;
    bool acm_enabled;
    satlink_acm_t acm; /**< Picks the MODCOD at every frame boundary while acm_enabled. */

    /* TX */
    uint8_t queue[SATLINK_MODEM_APP_TX_QUEUE][SATLINK_FRAME_INFO_BYTES];
    uint32_t q_head;
    uint32_t q_tail;
    satlink_cf_t tx_frame[SATLINK_FRAME_MAX_SYMBOLS];
    size_t tx_len;
    size_t tx_pos;
    uint8_t tx_seq;
    uint8_t tx_modcod;

    /* RX */
    satlink_receiver_t rx;

    /* software loopback */
    satlink_fir_t loop_tx_fir;
    satlink_fir_t loop_rx_fir;
    satlink_channel_t loop_channel;

    uint32_t uptime_ms;
    uint32_t next_status_ms;
    uint64_t unix_offset_ms;
    uint32_t latency_max_us;
    uint32_t latency_sum_us;
    uint32_t latency_count;
    uint16_t cpu_load_permille;
    uint32_t dma_underruns;
    uint32_t rx_overruns;
    satlink_modem_app_counters_t counters;
} satlink_modem_app_t;

/** Defaults: TX and RX on, QPSK 1/2, analog loopback, ACM off. */
satlink_status_t satlink_modem_app_init(satlink_modem_app_t *app, const satlink_modem_app_hw_t *hw);

/** Handle one message from Linux. Unknown or malformed messages are counted and logged. */
void satlink_modem_app_on_msg(satlink_modem_app_t *app, uint16_t type, const uint8_t *payload,
                              uint16_t len);

/** Fill @p out with the next @p count TX symbols (for the TX DMA). Zeros while TX is off. */
void satlink_modem_app_tx_symbols(satlink_modem_app_t *app, satlink_cf_t *out, size_t count);

/** Process @p count RX samples (matched-filter output, 2 per symbol) from the RX DMA. */
void satlink_modem_app_rx_samples(satlink_modem_app_t *app, const satlink_cf_t *in, size_t count);

/**
 * @brief Software loopback: produce @p symbols TX symbols, pulse-shape them, apply the
 *        configured channel (noise and gain from the CHANNEL message) and feed the receiver.
 * Only does something while the loopback mode is SATLINK_LOOP_SOFTWARE.
 */
void satlink_modem_app_run_loopback(satlink_modem_app_t *app, size_t symbols);

/** Advance time; sends STATUS once per second. */
void satlink_modem_app_tick(satlink_modem_app_t *app, uint32_t now_ms);

/** Record the processing time of one RX block (for the latency statistics). */
void satlink_modem_app_note_latency(satlink_modem_app_t *app, uint32_t us);

/** Firmware-provided load and DMA fault counters for STATUS. */
void satlink_modem_app_note_platform(satlink_modem_app_t *app, uint16_t cpu_load_permille,
                                     uint32_t dma_underruns, uint32_t rx_overruns);

/** Build the STATUS message content from the current state. */
void satlink_modem_app_status(const satlink_modem_app_t *app, satlink_msg_status_t *status);

/** Software-loopback noise for a CHANNEL noise level: sigma = level / 4096 (unit symbols). */
float satlink_modem_app_noise_sigma(uint16_t noise_level);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_APP_H */
