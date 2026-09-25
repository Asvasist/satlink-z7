/**
 * @file can_frame.h
 * @brief Classic CAN frame as seen by the portable protocol code (no OS or driver types).
 *
 * The MicroBlaze V firmware fills it from the MCP2515 receive buffers; the Linux tools convert
 * it to and from `struct can_frame` (SocketCAN) at the edge.
 *
 * @implements SRS-HKC-003
 */
#ifndef SATLINK_HKC_CAN_FRAME_H
#define SATLINK_HKC_CAN_FRAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Largest payload of a classic CAN frame. */
#define SATLINK_CAN_MAX_DLC (8U)

/** Largest 11-bit identifier. SatLink uses standard identifiers only. */
#define SATLINK_CAN_MAX_STD_ID (0x7FFU)

/** One classic CAN data frame with an 11-bit identifier. */
typedef struct
{
    uint16_t id;                       /**< 11-bit identifier. */
    uint8_t dlc;                       /**< Number of valid bytes in @ref data (0..8). */
    uint8_t data[SATLINK_CAN_MAX_DLC]; /**< Payload; bytes beyond @ref dlc are zero. */
} satlink_can_frame_t;

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_HKC_CAN_FRAME_H */
