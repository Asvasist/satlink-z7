/**
 * @file ab_slot.h
 * @brief Reference model of the A/B boot-slot logic used by the U-Boot script.
 *
 * Two root file system / kernel slots, A and B. An update is written to the slot that is not
 * running and then put "on trial": the boot script counts every attempt to start it, and if it
 * has not been confirmed good after `bootlimit` attempts it falls back to the previous slot. Once
 * Linux is up and healthy it confirms the slot, which ends the trial.
 *
 * The U-Boot script (yocto/meta-satlink/recipes-bsp/satlink-boot-scr/files/boot-ab.cmd) keeps the
 * same state in environment variables and does the same steps in the same order; this model is
 * the executable specification that logic is written and reviewed against.
 *
 *   U-Boot variable      field
 *   boot_slot            active            (a or b)
 *   prev_slot            previous
 *   bootcount            bootcount
 *   bootlimit            bootlimit
 *   upgrade_available    trial             (1 while the active slot is on trial)
 *
 * @implements SRS-BOOT-003
 */
#ifndef SATLINK_BOOT_AB_SLOT_H
#define SATLINK_BOOT_AB_SLOT_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SATLINK_SLOT_A = 0,
    SATLINK_SLOT_B = 1
} satlink_slot_t;

typedef struct
{
    satlink_slot_t active;
    satlink_slot_t previous;
    uint8_t bootcount;
    uint8_t bootlimit;
    bool trial;
} satlink_ab_t;

/** Steady state: @p active is confirmed good, no update pending. @p bootlimit must be >= 1. */
satlink_status_t satlink_ab_init(satlink_ab_t *ab, satlink_slot_t active, uint8_t bootlimit);

/** The other slot. */
satlink_slot_t satlink_slot_other(satlink_slot_t slot);

/**
 * @brief Decide which slot to boot; this is what the boot script does on every power-up.
 * @param rolled_back  Set to true if this call gave up on a trial slot; may be NULL.
 */
satlink_slot_t satlink_ab_select(satlink_ab_t *ab, bool *rolled_back);

/** The running slot is healthy: end the trial and make it the fallback for the next update. */
void satlink_ab_mark_good(satlink_ab_t *ab);

/**
 * @brief The inactive slot now holds a new image: switch to it, on trial.
 * @return ::SATLINK_ERR_STATE if the current slot is still on trial (finish or roll back first).
 */
satlink_status_t satlink_ab_begin_update(satlink_ab_t *ab);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_BOOT_AB_SLOT_H */
