/**
 * @file ab_slot.c
 * @implements SRS-BOOT-003
 */
#include "satlink/boot/ab_slot.h"

#include <stddef.h>

satlink_slot_t satlink_slot_other(satlink_slot_t slot)
{
    return (slot == SATLINK_SLOT_A) ? SATLINK_SLOT_B : SATLINK_SLOT_A;
}

satlink_status_t satlink_ab_init(satlink_ab_t *ab, satlink_slot_t active, uint8_t bootlimit)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if (ab != NULL)
    {
        if ((bootlimit == 0U) || ((active != SATLINK_SLOT_A) && (active != SATLINK_SLOT_B)))
        {
            status = SATLINK_ERR_RANGE;
        }
        else
        {
            ab->active = active;
            ab->previous = active;
            ab->bootcount = 0U;
            ab->bootlimit = bootlimit;
            ab->trial = false;
            status = SATLINK_OK;
        }
    }

    return status;
}

satlink_slot_t satlink_ab_select(satlink_ab_t *ab, bool *rolled_back)
{
    satlink_slot_t slot = SATLINK_SLOT_A;

    if (rolled_back != NULL)
    {
        *rolled_back = false;
    }

    if (ab != NULL)
    {
        if (ab->trial)
        {
            if (ab->bootcount >= ab->bootlimit)
            {
                /* The trial slot had its attempts and was never confirmed: give up on it. */
                ab->active = ab->previous;
                ab->trial = false;
                ab->bootcount = 0U;
                if (rolled_back != NULL)
                {
                    *rolled_back = true;
                }
            }
            else
            {
                ab->bootcount = (uint8_t)(ab->bootcount + 1U);
            }
        }
        slot = ab->active;
    }

    return slot;
}

void satlink_ab_mark_good(satlink_ab_t *ab)
{
    if (ab != NULL)
    {
        ab->trial = false;
        ab->bootcount = 0U;
        ab->previous = ab->active;
    }
}

satlink_status_t satlink_ab_begin_update(satlink_ab_t *ab)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if (ab != NULL)
    {
        if (ab->trial)
        {
            status = SATLINK_ERR_STATE;
        }
        else
        {
            ab->previous = ab->active;
            ab->active = satlink_slot_other(ab->active);
            ab->bootcount = 0U;
            ab->trial = true;
            status = SATLINK_OK;
        }
    }

    return status;
}
