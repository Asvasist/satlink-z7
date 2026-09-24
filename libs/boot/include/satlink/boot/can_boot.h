/**
 * @file can_boot.h
 * @brief Firmware upload protocol over classic CAN: receiver (bootloader) and sender (host).
 *
 * The host sends BEGIN (image size and CRC-16), a stream of DATA frames and END; the bootloader
 * answers with cumulative acknowledgements. It is a go-back-N protocol: the sender may have at
 * most `window` DATA frames in flight, a gap makes the receiver answer NAK with the sequence
 * number it expects, and a timeout makes the sender start again from the last acknowledged
 * frame. Duplicates are harmless. The whole image is protected by a CRC-16-CCITT that the
 * receiver checks at END; BOOT is only accepted after that check passed.
 *
 * Frame layouts (identifiers are 11-bit, see SATLINK_CANBOOT_ID_*):
 *
 *   host -> node   byte 0 = opcode
 *     PING   [01]
 *     BEGIN  [02][size u32 LE][crc16 LE]
 *     DATA   [03][seq u16 LE][up to 5 payload bytes]
 *     END    [04]
 *     BOOT   [05]
 *     ABORT  [06]
 *     ENTER  [07]   ask a running application to restart into the bootloader; a bootloader
 *                   that receives it simply acknowledges
 *
 *   node -> host   always 8 bytes: [kind][arg][value u16 LE][aux u32 LE]
 *     ACK   kind 0x40, arg = opcode acknowledged, value = next expected sequence number
 *           (BEGIN: 0), aux = largest image the node accepts
 *     NAK   kind 0x41, arg = opcode refused, value = SATLINK_CANBOOT_ERR_*, aux = next expected
 *           sequence number
 *     PONG  kind 0x42, arg = receiver state, value = protocol version, aux = largest image
 *
 * Uses no dynamic memory; the same code runs in the MicroBlaze V bootloader and in the Linux
 * uploader, and both are tested against each other on the host.
 *
 * @implements SRS-HKC-003
 */
#ifndef SATLINK_BOOT_CAN_BOOT_H
#define SATLINK_BOOT_CAN_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/can/can_frame.h"
#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_CANBOOT_ID_CMD         (0x7E0U) /**< host -> node */
#define SATLINK_CANBOOT_ID_RSP         (0x7E1U) /**< node -> host */
#define SATLINK_CANBOOT_VERSION        (1U)
#define SATLINK_CANBOOT_CHUNK          (5U)     /**< image bytes carried by one DATA frame */
#define SATLINK_CANBOOT_DEFAULT_WINDOW (16U)    /**< DATA frames per acknowledgement */
#define SATLINK_CANBOOT_MAX_FRAMES     (65535U) /**< sequence numbers are 16 bits */
#define SATLINK_CANBOOT_MAX_IMAGE      (SATLINK_CANBOOT_MAX_FRAMES * SATLINK_CANBOOT_CHUNK)

/** Command opcodes (byte 0 of a host -> node frame). */
typedef enum
{
    SATLINK_CANBOOT_OP_PING = 0x01,
    SATLINK_CANBOOT_OP_BEGIN = 0x02,
    SATLINK_CANBOOT_OP_DATA = 0x03,
    SATLINK_CANBOOT_OP_END = 0x04,
    SATLINK_CANBOOT_OP_BOOT = 0x05,
    SATLINK_CANBOOT_OP_ABORT = 0x06,
    SATLINK_CANBOOT_OP_ENTER = 0x07
} satlink_canboot_op_t;

/** Response kinds (byte 0 of a node -> host frame). */
typedef enum
{
    SATLINK_CANBOOT_RSP_ACK = 0x40,
    SATLINK_CANBOOT_RSP_NAK = 0x41,
    SATLINK_CANBOOT_RSP_PONG = 0x42
} satlink_canboot_rsp_t;

/** Reasons carried by a NAK. */
typedef enum
{
    SATLINK_CANBOOT_ERR_NONE = 0,
    SATLINK_CANBOOT_ERR_OPCODE = 1,      /**< unknown command */
    SATLINK_CANBOOT_ERR_STATE = 2,       /**< command not allowed in the current state */
    SATLINK_CANBOOT_ERR_SIZE = 3,        /**< image empty or larger than the node accepts */
    SATLINK_CANBOOT_ERR_SEQ = 4,         /**< gap in the DATA sequence; aux = expected number */
    SATLINK_CANBOOT_ERR_LENGTH = 5,      /**< DATA frame with the wrong payload length */
    SATLINK_CANBOOT_ERR_WRITE = 6,       /**< the node could not store the data */
    SATLINK_CANBOOT_ERR_CRC = 7,         /**< image CRC mismatch at END */
    SATLINK_CANBOOT_ERR_INCOMPLETE = 8,  /**< END before all bytes arrived */
    SATLINK_CANBOOT_ERR_TIMEOUT = 0xFFFF /**< sender only: no answer after all retries */
} satlink_canboot_err_t;

/** Build the ENTER command, for a host that wants to update an application that is running. */
void satlink_canboot_make_enter(satlink_can_frame_t *frame);

/* ------------------------------------------------------------------------------------------ */
/* Receiver (runs in the bootloader)                                                          */
/* ------------------------------------------------------------------------------------------ */

/** Stores @p len bytes at @p offset of the image. Returns ::SATLINK_OK or an error. */
typedef satlink_status_t (*satlink_canboot_write_fn)(void *ctx, uint32_t offset,
                                                     const uint8_t *data, size_t len);

typedef struct
{
    satlink_canboot_write_fn write; /**< where the image goes */
    void *ctx;
    uint32_t max_size; /**< largest image accepted, in bytes */
    uint16_t window;   /**< DATA frames per acknowledgement; 0 selects the default */
} satlink_canboot_rx_config_t;

typedef enum
{
    SATLINK_CANBOOT_RX_IDLE = 0,
    SATLINK_CANBOOT_RX_RECEIVING = 1,
    SATLINK_CANBOOT_RX_VERIFIED = 2,
    SATLINK_CANBOOT_RX_ERROR = 3
} satlink_canboot_rx_state_t;

typedef struct
{
    satlink_canboot_rx_config_t cfg;
    satlink_canboot_rx_state_t state;
    uint32_t size;
    uint16_t crc_expected;
    uint16_t crc_running;
    uint32_t received;
    uint16_t next_seq;
    uint16_t since_ack;
    bool boot_requested;
} satlink_canboot_rx_t;

/** Prepare a receiver. ::SATLINK_ERR_NULL for a missing write callback, ::SATLINK_ERR_RANGE for
 *  a max_size of 0 or above SATLINK_CANBOOT_MAX_IMAGE. */
satlink_status_t satlink_canboot_rx_init(satlink_canboot_rx_t *rx,
                                         const satlink_canboot_rx_config_t *cfg);

/**
 * @brief Process one received CAN frame.
 *
 * Frames that are not standard data frames with SATLINK_CANBOOT_ID_CMD are ignored. If the
 * frame calls for an answer, @p rsp is filled and @p have_rsp is set.
 */
satlink_status_t satlink_canboot_rx_handle(satlink_canboot_rx_t *rx,
                                           const satlink_can_frame_t *frame,
                                           satlink_can_frame_t *rsp, bool *have_rsp);

satlink_canboot_rx_state_t satlink_canboot_rx_state(const satlink_canboot_rx_t *rx);

/** True once a verified image has been told to boot. */
bool satlink_canboot_rx_boot_requested(const satlink_canboot_rx_t *rx);

/** Size of the image being received or verified, 0 when idle. */
uint32_t satlink_canboot_rx_image_size(const satlink_canboot_rx_t *rx);

/* ------------------------------------------------------------------------------------------ */
/* Sender (runs on the host)                                                                  */
/* ------------------------------------------------------------------------------------------ */

typedef enum
{
    SATLINK_CANBOOT_TX_BEGIN = 0, /**< BEGIN sent or about to be sent */
    SATLINK_CANBOOT_TX_DATA = 1,
    SATLINK_CANBOOT_TX_END = 2,
    SATLINK_CANBOOT_TX_BOOT = 3,
    SATLINK_CANBOOT_TX_DONE = 4,
    SATLINK_CANBOOT_TX_FAILED = 5
} satlink_canboot_tx_state_t;

typedef struct
{
    const uint8_t *image;
    uint32_t size;
    uint16_t crc;
    uint16_t window;
    uint16_t total_frames;
    uint16_t acked_seq;
    uint16_t next_seq;
    uint8_t retries;
    uint8_t max_retries;
    bool boot_after;
    bool control_pending;
    satlink_canboot_tx_state_t state;
    uint16_t error;
} satlink_canboot_tx_t;

/**
 * @brief Start uploading @p image.
 * @param window       DATA frames in flight; 0 selects the default.
 * @param boot_after   Send BOOT once the image is verified.
 * @param max_retries  Timeouts tolerated in a row before giving up.
 * @return ::SATLINK_ERR_RANGE for an empty or oversize image.
 */
satlink_status_t satlink_canboot_tx_init(satlink_canboot_tx_t *tx, const uint8_t *image,
                                         uint32_t size, uint16_t window, bool boot_after,
                                         uint8_t max_retries);

/** Produce the next frame to put on the bus. Returns false when the sender must wait. */
bool satlink_canboot_tx_next(satlink_canboot_tx_t *tx, satlink_can_frame_t *frame);

/** Feed a frame received from the node. Frames that are not responses are ignored. */
satlink_status_t satlink_canboot_tx_on_response(satlink_canboot_tx_t *tx,
                                                const satlink_can_frame_t *frame);

/** Call when no answer arrived in time: retransmits from the last acknowledged point. */
void satlink_canboot_tx_on_timeout(satlink_canboot_tx_t *tx);

satlink_canboot_tx_state_t satlink_canboot_tx_state(const satlink_canboot_tx_t *tx);

/** SATLINK_CANBOOT_ERR_* that made the transfer fail, or 0. */
uint16_t satlink_canboot_tx_error(const satlink_canboot_tx_t *tx);

/** Percentage of DATA frames acknowledged so far, 0..100. */
uint8_t satlink_canboot_tx_progress(const satlink_canboot_tx_t *tx);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_BOOT_CAN_BOOT_H */
