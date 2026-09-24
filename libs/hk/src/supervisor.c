/**
 * @file supervisor.c
 * @implements SRS-HKC-001
 */
#include "satlink/hk/supervisor.h"

#include <stddef.h>

satlink_status_t satlink_wdg_sup_init(satlink_wdg_sup_t *sup, uint32_t required_mask,
                                      uint32_t window_ticks)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if (sup != NULL)
    {
        if ((required_mask == 0U) || (window_ticks == 0U))
        {
            status = SATLINK_ERR_RANGE;
        }
        else
        {
            sup->required_mask = required_mask;
            sup->alive_mask = 0U;
            sup->window_ticks = window_ticks;
            sup->elapsed = 0U;
            sup->last_missing = 0U;
            sup->kicks = 0U;
            sup->missed_windows = 0U;
            status = SATLINK_OK;
        }
    }

    return status;
}

void satlink_wdg_sup_checkin(satlink_wdg_sup_t *sup, uint32_t task_id)
{
    if ((sup != NULL) && (task_id < SATLINK_WDG_MAX_TASKS))
    {
        sup->alive_mask |= (uint32_t)1U << task_id;
    }
}

bool satlink_wdg_sup_tick(satlink_wdg_sup_t *sup)
{
    bool kick = false;

    if (sup != NULL)
    {
        sup->elapsed++;
        if (sup->elapsed >= sup->window_ticks)
        {
            sup->last_missing = sup->required_mask & ~sup->alive_mask;
            if (sup->last_missing == 0U)
            {
                sup->kicks++;
                kick = true;
            }
            else
            {
                sup->missed_windows++;
            }
            sup->alive_mask = 0U;
            sup->elapsed = 0U;
        }
    }

    return kick;
}
