/**
 * @file canboot.h
 * @brief CAN bootloader protocol: Linux loads the HKC application into LMB BRAM over CAN.
 *
 * Request frames use ID 0x7E0 (Linux -> bootloader), responses 0x7E8 (bootloader -> Linux).
 * Byte 0 of a request is the opcode; byte 0 of a response is the opcode with bit 7 set and
 * byte 1 is a ::satlink_canboot_status_t. Every request gets exactly one response.
 *
 * | Opcode | Request bytes                      | Response bytes 2..                          |
 * |--------|------------------------------------|---------------------------------------------|
 * | 0x01 PING  | -                              | bl major, bl minor, app valid, session state |
 * | 0x02 START | image size (u32)               | -                                           |
 * | 0x03 DATA  | seq (u8), 1..6 data bytes      | seq                                         |
 * | 0x04 END   | CRC-32 of the whole image (u32) | CRC-32 the bootloader computed (u32)       |
 * | 0x05 BOOT  | -                              | -  (then jumps to the application)          |
 * | 0x06 ABORT | -                              | -                                           |
 *
 * DATA frames carry consecutive chunks; seq counts frames modulo 256. A repeat of the last
 * accepted seq (the host missed the ACK and retransmitted) is acknowledged again but not
 * written twice, so a lost response never corrupts the image.
 *
 * The session is pure logic over a byte region: the firmware passes its LMB application window,
 * the host unit tests pass an array.
 *
 * @implements SRS-HKC-002
 */
#ifndef SATLINK_HKC_CANBOOT_H
#define SATLINK_HKC_CANBOOT_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/common/status.h"
#include "satlink/hkc/can_frame.h"
#include "satlink/hkc/image.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_CANBOOT_ID_REQUEST  (0x7E0U)
#define SATLINK_CANBOOT_ID_RESPONSE (0x7E8U)

/** Data bytes carried by one DATA frame. */
#define SATLINK_CANBOOT_CHUNK (6U)

/** Response flag OR-ed into the opcode. */
#define SATLINK_CANBOOT_RESPONSE_FLAG (0x80U)

typedef enum
{
    SATLINK_CANBOOT_OP_PING = 0x01U,
    SATLINK_CANBOOT_OP_START = 0x02U,
    SATLINK_CANBOOT_OP_DATA = 0x03U,
    SATLINK_CANBOOT_OP_END = 0x04U,
    SATLINK_CANBOOT_OP_BOOT = 0x05U,
    SATLINK_CANBOOT_OP_ABORT = 0x06U,
} satlink_canboot_op_t;

typedef enum
{
    SATLINK_CANBOOT_ST_OK = 0x00U,
    SATLINK_CANBOOT_ST_BAD_STATE = 0x01U,  /**< Opcode not allowed in the current state. */
    SATLINK_CANBOOT_ST_BAD_SEQ = 0x02U,    /**< DATA frame out of order. */
    SATLINK_CANBOOT_ST_TOO_LARGE = 0x03U,  /**< Image larger than the application region. */
    SATLINK_CANBOOT_ST_CRC = 0x04U,        /**< CRC in END does not match the received bytes. */
    SATLINK_CANBOOT_ST_BAD_IMAGE = 0x05U,  /**< Received bytes are not a valid image. */
    SATLINK_CANBOOT_ST_BAD_LENGTH = 0x06U, /**< Malformed frame or data beyond the image size. */
    SATLINK_CANBOOT_ST_UNKNOWN = 0x07U,    /**< Unknown opcode. */
} satlink_canboot_status_t;

typedef enum
{
    SATLINK_CANBOOT_IDLE = 0U,      /**< Waiting for START (or BOOT of an already valid image). */
    SATLINK_CANBOOT_RECEIVING = 1U, /**< Between START and END. */
    SATLINK_CANBOOT_COMPLETE = 2U,  /**< END accepted: region holds a verified image. */
} satlink_canboot_state_t;

/** Bootloader-side session. Treat as opaque. */
typedef struct
{
    uint8_t *region;      /**< Application window (written by DATA). */
    uint32_t region_base; /**< Address of region[0] as the application sees it. */
    uint32_t region_size;
    uint8_t version_major; /**< Bootloader version reported by PING. */
    uint8_t version_minor;
    satlink_canboot_state_t state;
    uint32_t image_size; /**< Announced by START. */
    uint32_t received;   /**< Bytes written so far. */
    uint8_t next_seq;
    bool have_last_seq;
    bool image_valid;    /**< Region holds an image that passed satlink_hkc_image_verify(). */
    bool boot_requested; /**< Set by an accepted BOOT; the caller then jumps to @ref entry. */
    uint32_t entry;
} satlink_canboot_target_t;

/**
 * @brief Start a session over @p region.
 *
 * Checks whether the region already holds a valid image (a warm reset keeps BRAM contents), so
 * the bootloader can start it after its wait window without a download.
 */
satlink_status_t satlink_canboot_target_init(satlink_canboot_target_t *target, uint8_t *region,
                                             uint32_t region_base, uint32_t region_size,
                                             uint8_t version_major, uint8_t version_minor);

/**
 * @brief Handle one received frame.
 *
 * Frames with an ID other than ::SATLINK_CANBOOT_ID_REQUEST are ignored (returns false).
 * Otherwise the response is written to @p response and true is returned.
 */
bool satlink_canboot_target_handle(satlink_canboot_target_t *target,
                                   const satlink_can_frame_t *request,
                                   satlink_can_frame_t *response);

/** Encode a request. @p payload may be NULL when @p len is 0; @p len must be <= 7. */
satlink_status_t satlink_canboot_encode_request(uint8_t opcode, const uint8_t *payload, uint8_t len,
                                                satlink_can_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_HKC_CANBOOT_H */
