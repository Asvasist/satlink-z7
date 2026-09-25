/**
 * @file hk_proto.h
 * @brief Housekeeping CAN protocol between Linux (Core 0) and the MicroBlaze V housekeeping
 *        controller (HKC).
 *
 * All multi-byte fields are little-endian. Identifiers, lowest (highest priority) first:
 *
 * | ID    | Direction    | Content                                                      |
 * |-------|--------------|--------------------------------------------------------------|
 * | 0x080 | Linux -> HKC | Time sync: Unix seconds (u32), milliseconds (u16)            |
 * | 0x100 | HKC -> Linux | Environment: die temp (0.01 degC, s16), VCCINT, VCCAUX, VBRAM (mV) |
 * | 0x101 | HKC -> Linux | PS supplies: VCCPINT, VCCPAUX, VCCO_DDR (mV)                 |
 * | 0x102 | HKC -> Linux | Status: uptime (s, u32), reset cause, switches, cmd count, error flags |
 * | 0x180 | Linux -> HKC | Command: opcode, arguments                                   |
 * | 0x181 | HKC -> Linux | Command acknowledgement: opcode, status, data                |
 *
 * The bootloader uses 0x7E0/0x7E8 (see canboot.h).
 *
 * @implements SRS-HKC-003
 */
#ifndef SATLINK_HKC_HK_PROTO_H
#define SATLINK_HKC_HK_PROTO_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/common/status.h"
#include "satlink/hkc/can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_HK_ID_TIME_SYNC   (0x080U) /**< Linux -> HKC time sync. */
#define SATLINK_HK_ID_ENV         (0x100U) /**< HKC -> Linux temperatures and PL supplies. */
#define SATLINK_HK_ID_SUPPLY      (0x101U) /**< HKC -> Linux PS supplies. */
#define SATLINK_HK_ID_STATUS      (0x102U) /**< HKC -> Linux status and heartbeat. */
#define SATLINK_HK_ID_COMMAND     (0x180U) /**< Linux -> HKC command. */
#define SATLINK_HK_ID_COMMAND_ACK (0x181U) /**< HKC -> Linux command acknowledgement. */

/** Command opcodes (first byte of a ::SATLINK_HK_ID_COMMAND frame). */
typedef enum
{
    SATLINK_HK_CMD_SET_LED = 0x01U,      /**< arg: RGB bits (bit0 R, bit1 G, bit2 B). */
    SATLINK_HK_CMD_SET_PERIOD = 0x02U,   /**< arg: housekeeping period in ms (u16, 100..10000). */
    SATLINK_HK_CMD_ENTER_BOOT = 0x03U,   /**< arg: key 0xB0 0x07; resets into the bootloader. */
    SATLINK_HK_CMD_GET_VERSION = 0x04U,  /**< ack data: major, minor, patch. */
    SATLINK_HK_CMD_CLEAR_ERRORS = 0x05U, /**< Clears the sticky error flags. */
} satlink_hk_cmd_t;

/** Status byte of a command acknowledgement. */
typedef enum
{
    SATLINK_HK_ACK_OK = 0x00U,
    SATLINK_HK_ACK_UNKNOWN = 0x01U, /**< Unknown opcode. */
    SATLINK_HK_ACK_BAD_ARG = 0x02U, /**< Argument missing or out of range. */
} satlink_hk_ack_status_t;

/** Reason for the last HKC reset (status frame). */
typedef enum
{
    SATLINK_HK_RESET_POWER_ON = 0U,
    SATLINK_HK_RESET_WATCHDOG = 1U,
    SATLINK_HK_RESET_COMMAND = 2U,
} satlink_hk_reset_cause_t;

/** Sticky error flags (status frame). */
#define SATLINK_HK_ERR_XADC_ALARM                                                                  \
    (0x01U) /**< The XADC raised an over-temperature or supply alarm. */
#define SATLINK_HK_ERR_CAN_TX     (0x02U) /**< A CAN transmission failed or timed out. */
#define SATLINK_HK_ERR_CAN_RX_OVR (0x04U) /**< The MCP2515 dropped a received frame. */

/** Content of ::SATLINK_HK_ID_ENV. */
typedef struct
{
    int16_t die_temp_centi_c; /**< Die temperature in 0.01 degC. */
    uint16_t vccint_mv;
    uint16_t vccaux_mv;
    uint16_t vbram_mv;
} satlink_hk_env_t;

/** Content of ::SATLINK_HK_ID_SUPPLY. */
typedef struct
{
    uint16_t vccpint_mv;
    uint16_t vccpaux_mv;
    uint16_t vcco_ddr_mv;
} satlink_hk_supply_t;

/** Content of ::SATLINK_HK_ID_STATUS. */
typedef struct
{
    uint32_t uptime_s;
    uint8_t reset_cause; /**< ::satlink_hk_reset_cause_t */
    uint8_t switches;    /**< SW0..SW3 in bits 0..3. */
    uint8_t cmd_count;   /**< Commands accepted since reset (wraps). */
    uint8_t error_flags; /**< SATLINK_HK_ERR_* */
} satlink_hk_status_t;

/** Content of ::SATLINK_HK_ID_TIME_SYNC. */
typedef struct
{
    uint32_t unix_s;
    uint16_t millis;
} satlink_hk_time_t;

/** A decoded command frame. */
typedef struct
{
    uint8_t opcode; /**< ::satlink_hk_cmd_t */
    uint8_t argc;   /**< Number of argument bytes. */
    uint8_t argv[SATLINK_CAN_MAX_DLC - 1U];
} satlink_hk_command_t;

/** A decoded command acknowledgement. */
typedef struct
{
    uint8_t opcode;
    uint8_t status; /**< ::satlink_hk_ack_status_t */
    uint8_t datac;
    uint8_t datav[SATLINK_CAN_MAX_DLC - 2U];
} satlink_hk_ack_t;

/* Encoders fill @p frame completely (id, dlc, zero padding). All return SATLINK_ERR_NULL for a
 * NULL pointer. Decoders check id and dlc and return SATLINK_ERR_RANGE on mismatch. */

satlink_status_t satlink_hk_encode_env(const satlink_hk_env_t *env, satlink_can_frame_t *frame);
satlink_status_t satlink_hk_decode_env(const satlink_can_frame_t *frame, satlink_hk_env_t *env);

satlink_status_t satlink_hk_encode_supply(const satlink_hk_supply_t *supply,
                                          satlink_can_frame_t *frame);
satlink_status_t satlink_hk_decode_supply(const satlink_can_frame_t *frame,
                                          satlink_hk_supply_t *supply);

satlink_status_t satlink_hk_encode_status(const satlink_hk_status_t *status,
                                          satlink_can_frame_t *frame);
satlink_status_t satlink_hk_decode_status(const satlink_can_frame_t *frame,
                                          satlink_hk_status_t *status);

satlink_status_t satlink_hk_encode_time(const satlink_hk_time_t *time, satlink_can_frame_t *frame);
satlink_status_t satlink_hk_decode_time(const satlink_can_frame_t *frame, satlink_hk_time_t *time);

/** @p argc > 7 returns SATLINK_ERR_RANGE. @p argv may be NULL when @p argc is 0. */
satlink_status_t satlink_hk_encode_command(uint8_t opcode, const uint8_t *argv, uint8_t argc,
                                           satlink_can_frame_t *frame);
/** A command frame needs at least the opcode byte. */
satlink_status_t satlink_hk_decode_command(const satlink_can_frame_t *frame,
                                           satlink_hk_command_t *command);

satlink_status_t satlink_hk_encode_ack(const satlink_hk_ack_t *ack, satlink_can_frame_t *frame);
satlink_status_t satlink_hk_decode_ack(const satlink_can_frame_t *frame, satlink_hk_ack_t *ack);

/** Converts a raw 12-bit XADC temperature code (UG480) to 0.01 degC. */
int16_t satlink_hk_xadc_temp_centi_c(uint16_t code12);

/** Converts a raw 12-bit XADC supply code (UG480, 3 V full scale) to mV. */
uint16_t satlink_hk_xadc_supply_mv(uint16_t code12);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_HKC_HK_PROTO_H */
