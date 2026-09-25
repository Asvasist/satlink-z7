/**
 * @file msg.h
 * @brief Messages between the payload manager (Linux, Core 0) and the modem firmware
 *        (FreeRTOS, Core 1). Carried in the IPC rings; all fields little-endian.
 *
 * | Type   | Direction     | Payload                                                      |
 * |--------|---------------|--------------------------------------------------------------|
 * | 0x0001 | Linux -> RTOS | PING: token                                                  |
 * | 0x8001 | RTOS -> Linux | PONG: token, firmware uptime                                 |
 * | 0x0010 | Linux -> RTOS | MODEM_CONFIG: TX/RX enable, MODCOD, loopback                 |
 * | 0x0011 | Linux -> RTOS | CHANNEL: emulator noise and gain (LEO pass profile)          |
 * | 0x0012 | Linux -> RTOS | ACM_CONFIG: adaptive MODCOD limits, margin, hysteresis       |
 * | 0x0013 | Linux -> RTOS | TIME: Unix time in ms (Linux is the time master)             |
 * | 0x0020 | Linux -> RTOS | TX_FRAME: 128 bytes to send in the next data frame           |
 * | 0x8020 | RTOS -> Linux | RX_FRAME: MODCOD, CRC result, Es/N0, 128 bytes               |
 * | 0x8030 | RTOS -> Linux | STATUS: link and firmware counters, once per second          |
 * | 0x8040 | RTOS -> Linux | LOG: level and text                                          |
 *
 * @implements SRS-AMP-003
 */
#ifndef SATLINK_AMP_MSG_H
#define SATLINK_AMP_MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_MSG_PING         (0x0001U)
#define SATLINK_MSG_PONG         (0x8001U)
#define SATLINK_MSG_MODEM_CONFIG (0x0010U)
#define SATLINK_MSG_CHANNEL      (0x0011U)
#define SATLINK_MSG_ACM_CONFIG   (0x0012U)
#define SATLINK_MSG_TIME         (0x0013U)
#define SATLINK_MSG_TX_FRAME     (0x0020U)
#define SATLINK_MSG_RX_FRAME     (0x8020U)
#define SATLINK_MSG_STATUS       (0x8030U)
#define SATLINK_MSG_LOG          (0x8040U)

#define SATLINK_MSG_FRAME_BYTES (128U)
#define SATLINK_MSG_LOG_MAX     (120U)
#define SATLINK_MSG_MAX_BYTES   (160U)

/** Loopback modes of MODEM_CONFIG. */
typedef enum
{
    SATLINK_LOOP_ANALOG = 0,  /**< Normal: through the codec and the 3.5 mm cable. */
    SATLINK_LOOP_DIGITAL = 1, /**< PL loops I2S TX to RX (payload_ctrl DIG_LOOPBACK). */
    SATLINK_LOOP_SOFTWARE = 2 /**< Firmware feeds its TX samples to its RX (no PL needed). */
} satlink_loopback_t;

typedef struct
{
    uint32_t token;
    uint32_t uptime_ms;
} satlink_msg_ping_t;

typedef struct
{
    bool tx_enable;
    bool rx_enable;
    uint8_t modcod;   /**< MODCOD when ACM is off. */
    uint8_t loopback; /**< satlink_loopback_t */
} satlink_msg_modem_config_t;

typedef struct
{
    uint16_t noise_level; /**< payload_ctrl NOISE_LEVEL (0 = off). */
    uint16_t gain_q15;    /**< payload_ctrl ATTEN (0x7FFF = 0 dB). */
} satlink_msg_channel_t;

typedef struct
{
    bool enabled;
    uint8_t min_modcod;
    uint8_t max_modcod;
    int16_t margin_cdb;     /**< Extra Es/N0 above the MODCOD threshold, 0.01 dB. */
    int16_t hysteresis_cdb; /**< Es/N0 must rise this much above the switch-up point. */
} satlink_msg_acm_config_t;

typedef struct
{
    uint64_t unix_ms;
} satlink_msg_time_t;

typedef struct
{
    uint8_t data[SATLINK_MSG_FRAME_BYTES];
} satlink_msg_tx_frame_t;

typedef struct
{
    uint8_t modcod;
    bool crc_ok;
    int16_t esn0_cdb;
    uint8_t seq;
    uint8_t data[SATLINK_MSG_FRAME_BYTES];
} satlink_msg_rx_frame_t;

typedef struct
{
    uint32_t uptime_ms;
    uint32_t frames_ok;
    uint32_t frames_crc_error;
    uint32_t header_errors;
    uint32_t bit_errors;
    uint32_t bits_checked;
    uint32_t tx_data_frames;
    uint32_t tx_idle_frames;
    uint32_t dma_underruns;
    uint32_t rx_overruns;
    uint32_t rx_latency_max_us; /**< Worst-case sample-block processing time. */
    uint32_t rx_latency_avg_us;
    int16_t esn0_cdb; /**< Smoothed Es/N0, 0.01 dB. */
    uint8_t locked;
    uint8_t tx_modcod;
    uint8_t acm_enabled;
    uint8_t tx_queue_depth;
    uint16_t cpu_load_permille;
} satlink_msg_status_t;

typedef struct
{
    uint8_t level; /**< 0 debug, 1 info, 2 warning, 3 error */
    char text[SATLINK_MSG_LOG_MAX + 1U];
} satlink_msg_log_t;

/* Encoders return the payload length written to @p out (0 on error: NULL or buffer too small).
 * Decoders return SATLINK_ERR_RANGE if @p len does not match the message size. */

size_t satlink_msg_encode_ping(const satlink_msg_ping_t *m, uint8_t *out, size_t cap);
satlink_status_t satlink_msg_decode_ping(const uint8_t *in, size_t len, satlink_msg_ping_t *m);

size_t satlink_msg_encode_modem_config(const satlink_msg_modem_config_t *m, uint8_t *out,
                                       size_t cap);
satlink_status_t satlink_msg_decode_modem_config(const uint8_t *in, size_t len,
                                                 satlink_msg_modem_config_t *m);

size_t satlink_msg_encode_channel(const satlink_msg_channel_t *m, uint8_t *out, size_t cap);
satlink_status_t satlink_msg_decode_channel(const uint8_t *in, size_t len,
                                            satlink_msg_channel_t *m);

size_t satlink_msg_encode_acm_config(const satlink_msg_acm_config_t *m, uint8_t *out, size_t cap);
satlink_status_t satlink_msg_decode_acm_config(const uint8_t *in, size_t len,
                                               satlink_msg_acm_config_t *m);

size_t satlink_msg_encode_time(const satlink_msg_time_t *m, uint8_t *out, size_t cap);
satlink_status_t satlink_msg_decode_time(const uint8_t *in, size_t len, satlink_msg_time_t *m);

size_t satlink_msg_encode_rx_frame(const satlink_msg_rx_frame_t *m, uint8_t *out, size_t cap);
satlink_status_t satlink_msg_decode_rx_frame(const uint8_t *in, size_t len,
                                             satlink_msg_rx_frame_t *m);

size_t satlink_msg_encode_status(const satlink_msg_status_t *m, uint8_t *out, size_t cap);
satlink_status_t satlink_msg_decode_status(const uint8_t *in, size_t len, satlink_msg_status_t *m);

/** LOG text is truncated to SATLINK_MSG_LOG_MAX characters; decoded text is NUL-terminated. */
size_t satlink_msg_encode_log(const satlink_msg_log_t *m, uint8_t *out, size_t cap);
satlink_status_t satlink_msg_decode_log(const uint8_t *in, size_t len, satlink_msg_log_t *m);

/** Name of a message type for logs ("?" if unknown). */
const char *satlink_msg_name(uint16_t type);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_AMP_MSG_H */
