/**
 * @file telemetry.h
 * @brief Housekeeping telemetry frames on the CAN bus.
 *
 * One snapshot is sent as three classic CAN frames (all 8 bytes, little endian):
 *
 *   0x100 thermal  [seq][levels][temperature i32 mdegC][0][0]
 *                  levels: temperature bits 1..0, VCCINT 3..2, VCCAUX 5..4, VCCBRAM 7..6
 *   0x101 supply   [seq][0][VCCINT u16 mV][VCCAUX u16 mV][VCCBRAM u16 mV]
 *   0x102 status   [seq][flags][uptime u32 s][0][0]     flags bit 0: last reset was the watchdog
 *
 * @implements SRS-HKC-001
 */
#ifndef SATLINK_HK_TELEMETRY_H
#define SATLINK_HK_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/can/can_frame.h"
#include "satlink/common/status.h"
#include "satlink/hk/limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_HK_ID_THERMAL  (0x100U)
#define SATLINK_HK_ID_SUPPLY   (0x101U)
#define SATLINK_HK_ID_STATUS   (0x102U)
#define SATLINK_HK_FRAME_COUNT (3U)

typedef struct
{
    int32_t temp_mdegc;
    uint16_t vccint_mv;
    uint16_t vccaux_mv;
    uint16_t vccbram_mv;
    satlink_hk_level_t temp_level;
    satlink_hk_level_t vccint_level;
    satlink_hk_level_t vccaux_level;
    satlink_hk_level_t vccbram_level;
    uint32_t uptime_s;
    bool watchdog_reset; /**< the last reset of the housekeeping subsystem was the watchdog */
} satlink_hk_snapshot_t;

/** Build the three frames of one snapshot. @p seq lets the receiver spot lost snapshots. */
satlink_status_t satlink_hk_pack(const satlink_hk_snapshot_t *snapshot, uint8_t seq,
                                 satlink_can_frame_t frames[SATLINK_HK_FRAME_COUNT]);

/**
 * @brief Merge one received frame into @p snapshot.
 * @param seq  Receives the snapshot sequence number carried by the frame.
 * @return ::SATLINK_ERR_RANGE for a frame that is not a housekeeping frame or has a wrong
 *         length; @p snapshot is unchanged then.
 */
satlink_status_t satlink_hk_decode(const satlink_can_frame_t *frame,
                                   satlink_hk_snapshot_t *snapshot, uint8_t *seq);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_HK_TELEMETRY_H */
