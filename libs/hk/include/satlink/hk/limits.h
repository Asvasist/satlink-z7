/**
 * @file limits.h
 * @brief Warning and alarm evaluation with hysteresis.
 *
 * A reading that crosses a threshold raises the level at once; it only drops back after the
 * reading has moved past the threshold by the hysteresis, so a value hovering at a limit does
 * not make the level chatter.
 *
 * @implements SRS-HKC-001
 */
#ifndef SATLINK_HK_LIMITS_H
#define SATLINK_HK_LIMITS_H

#include <stdint.h>

#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SATLINK_HK_OK = 0,
    SATLINK_HK_WARN = 1,
    SATLINK_HK_ALARM = 2
} satlink_hk_level_t;

typedef enum
{
    SATLINK_HK_LIMIT_HIGH = 0, /**< too large is bad (temperature); alarm >= warn */
    SATLINK_HK_LIMIT_LOW = 1   /**< too small is bad (a supply); alarm <= warn */
} satlink_hk_direction_t;

typedef struct
{
    satlink_hk_direction_t direction;
    int32_t warn;
    int32_t alarm;
    int32_t hysteresis; /**< distance past a threshold needed to release the level, >= 0 */
    satlink_hk_level_t level;
} satlink_hk_limit_t;

/**
 * @brief Set up a limit at level OK.
 * @return ::SATLINK_ERR_RANGE if the thresholds are on the wrong side of each other for
 *         @p direction, or the hysteresis is negative.
 */
satlink_status_t satlink_hk_limit_init(satlink_hk_limit_t *limit, satlink_hk_direction_t direction,
                                       int32_t warn, int32_t alarm, int32_t hysteresis);

/** Feed a new reading and get the resulting level. */
satlink_hk_level_t satlink_hk_limit_update(satlink_hk_limit_t *limit, int32_t value);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_HK_LIMITS_H */
