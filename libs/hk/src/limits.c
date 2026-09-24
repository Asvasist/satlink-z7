/**
 * @file limits.c
 * @implements SRS-HKC-001
 */
#include "satlink/hk/limits.h"

#include <stdbool.h>
#include <stddef.h>

/** True if @p value is at or past @p threshold in the bad direction. */
static bool breached(satlink_hk_direction_t direction, int32_t value, int32_t threshold)
{
    return (direction == SATLINK_HK_LIMIT_HIGH) ? (value >= threshold) : (value <= threshold);
}

/** True if @p value is back inside @p threshold by more than the hysteresis. */
static bool released(satlink_hk_direction_t direction, int32_t value, int32_t threshold,
                     int32_t hysteresis)
{
    const int64_t v = (int64_t)value;
    const int64_t t = (int64_t)threshold;
    const int64_t h = (int64_t)hysteresis;

    return (direction == SATLINK_HK_LIMIT_HIGH) ? (v < (t - h)) : (v > (t + h));
}

satlink_status_t satlink_hk_limit_init(satlink_hk_limit_t *limit, satlink_hk_direction_t direction,
                                       int32_t warn, int32_t alarm, int32_t hysteresis)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if (limit != NULL)
    {
        const bool ordered =
            (direction == SATLINK_HK_LIMIT_HIGH) ? (alarm >= warn) : (alarm <= warn);

        if ((!ordered) || (hysteresis < 0))
        {
            status = SATLINK_ERR_RANGE;
        }
        else
        {
            limit->direction = direction;
            limit->warn = warn;
            limit->alarm = alarm;
            limit->hysteresis = hysteresis;
            limit->level = SATLINK_HK_OK;
            status = SATLINK_OK;
        }
    }

    return status;
}

satlink_hk_level_t satlink_hk_limit_update(satlink_hk_limit_t *limit, int32_t value)
{
    satlink_hk_level_t level = SATLINK_HK_OK;

    if (limit != NULL)
    {
        satlink_hk_level_t target = SATLINK_HK_OK;

        if (breached(limit->direction, value, limit->alarm))
        {
            target = SATLINK_HK_ALARM;
        }
        else if (breached(limit->direction, value, limit->warn))
        {
            target = SATLINK_HK_WARN;
        }
        else
        {
            target = SATLINK_HK_OK;
        }

        level = limit->level;
        if ((int32_t)target > (int32_t)level)
        {
            level = target; /* worse: react immediately */
        }
        else
        {
            /* Not worse: only step down once the reading is clear of the threshold by the
             * hysteresis. Coming down from ALARM may pass WARN on the way to OK. */
            if ((level == SATLINK_HK_ALARM) &&
                released(limit->direction, value, limit->alarm, limit->hysteresis))
            {
                level = SATLINK_HK_WARN;
            }
            if ((level == SATLINK_HK_WARN) &&
                released(limit->direction, value, limit->warn, limit->hysteresis))
            {
                level = SATLINK_HK_OK;
            }
        }

        limit->level = level;
    }

    return level;
}
