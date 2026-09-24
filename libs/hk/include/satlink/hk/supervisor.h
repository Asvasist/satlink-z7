/**
 * @file supervisor.h
 * @brief Task-alive supervision in front of a hardware watchdog.
 *
 * Every supervised task calls satlink_wdg_sup_checkin() at least once per window. The supervisor
 * is ticked at a fixed rate; when a window has elapsed it tells the caller to kick the hardware
 * watchdog only if every required task checked in. A task that hangs therefore stops the kicks
 * and the hardware resets the subsystem, instead of a healthy main loop keeping a dead task alive.
 *
 * @implements SRS-HKC-001
 */
#ifndef SATLINK_HK_SUPERVISOR_H
#define SATLINK_HK_SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_WDG_MAX_TASKS (32U)

typedef struct
{
    uint32_t required_mask; /**< bit n set: task n must check in every window */
    uint32_t alive_mask;    /**< tasks that checked in during the current window */
    uint32_t window_ticks;
    uint32_t elapsed;
    uint32_t last_missing; /**< tasks that did not check in during the last closed window */
    uint32_t kicks;
    uint32_t missed_windows;
} satlink_wdg_sup_t;

/** ::SATLINK_ERR_RANGE if @p required_mask is 0 or @p window_ticks is 0. */
satlink_status_t satlink_wdg_sup_init(satlink_wdg_sup_t *sup, uint32_t required_mask,
                                      uint32_t window_ticks);

/** Report that task @p task_id (0..31) is alive. Out-of-range ids are ignored. */
void satlink_wdg_sup_checkin(satlink_wdg_sup_t *sup, uint32_t task_id);

/**
 * @brief Advance time by one tick.
 * @return true when the window just closed with every required task alive: kick the hardware
 *         watchdog now. Otherwise false.
 */
bool satlink_wdg_sup_tick(satlink_wdg_sup_t *sup);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_HK_SUPERVISOR_H */
