/**
 * @file can_frame.h
 * @brief Classic CAN frame as used by every SatLink CAN component.
 *
 * @implements SRS-HKC-002
 */
#ifndef SATLINK_CAN_CAN_FRAME_H
#define SATLINK_CAN_CAN_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_CAN_MAX_DLC    (8U)
#define SATLINK_CAN_STD_ID_MAX (0x7FFU)
#define SATLINK_CAN_EXT_ID_MAX (0x1FFFFFFFU)

/** One classic CAN frame (up to 8 data bytes). */
typedef struct
{
    uint32_t id;                       /**< 11-bit or 29-bit identifier. */
    uint8_t dlc;                       /**< Number of data bytes, 0..8. */
    bool extended;                     /**< True for a 29-bit identifier. */
    bool rtr;                          /**< True for a remote transmission request. */
    uint8_t data[SATLINK_CAN_MAX_DLC]; /**< Payload; bytes beyond dlc are unused. */
} satlink_can_frame_t;

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_CAN_CAN_FRAME_H */
