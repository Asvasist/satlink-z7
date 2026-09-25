/**
 * @file acm.h
 * @brief Adaptive coding and modulation (ACM) controller.
 *
 * Chooses the MODCOD of the next frame from the receiver's smoothed Es/N0, like the DVB-S2 ACM
 * loop in a real payload (here the receiver is the same modem via the loopback, so the
 * "return channel" is internal).
 *
 * Rules, with T(m) the Es/N0 threshold of MODCOD m (modcod.h) and M the margin:
 * - The target is the most efficient MODCOD in [min, max] with T(m) + M <= Es/N0.
 * - Down: immediately when Es/N0 < T(current) + M, straight to the target.
 * - Up: one step at a time, only after Es/N0 >= T(next) + M + hysteresis for up_hold frames in a
 *   row; the counter restarts after every change.
 * - Lost lock: the most robust MODCOD in range (min) until the receiver locks again.
 *
 * Decisions are counted, and every change is reported through the event callback.
 *
 * @implements SRS-ACM-001
 * @implements SRS-SYS-005
 */
#ifndef SATLINK_ACM_ACM_H
#define SATLINK_ACM_ACM_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t min_modcod;
    uint8_t max_modcod;
    float margin_db;     /**< Kept above each MODCOD's threshold. */
    float hysteresis_db; /**< Extra margin before stepping up. */
    uint16_t up_hold;    /**< Consecutive good decisions before stepping up. */
} satlink_acm_config_t;

/** Why the MODCOD changed. */
typedef enum
{
    SATLINK_ACM_UP = 0,
    SATLINK_ACM_DOWN = 1,
    SATLINK_ACM_LOCK_LOST = 2,
} satlink_acm_reason_t;

typedef void (*satlink_acm_event_fn)(void *ctx, uint8_t from, uint8_t to,
                                     satlink_acm_reason_t reason, float esn0_db);

typedef struct
{
    satlink_acm_config_t config;
    uint8_t current;
    uint16_t good_count;
    uint32_t changes_up;
    uint32_t changes_down;
    uint32_t decisions;
    satlink_acm_event_fn on_change;
    void *ctx;
} satlink_acm_t;

/** Defaults: MODCOD 0..4, margin 1 dB, hysteresis 1 dB, 3 frames to step up. */
satlink_acm_config_t satlink_acm_default_config(void);

/** Start at @p config.min_modcod. SATLINK_ERR_RANGE for an invalid range or negative values. */
satlink_status_t satlink_acm_init(satlink_acm_t *acm, const satlink_acm_config_t *config,
                                  satlink_acm_event_fn on_change, void *ctx);

/** Replace the configuration (keeps the current MODCOD, clamped into the new range). */
satlink_status_t satlink_acm_configure(satlink_acm_t *acm, const satlink_acm_config_t *config);

/** One decision (called at every frame boundary). Returns the MODCOD for the next frame. */
uint8_t satlink_acm_update(satlink_acm_t *acm, float esn0_db, bool locked);

/** Most efficient MODCOD in range for @p esn0_db and the configured margin (no hysteresis). */
uint8_t satlink_acm_target(const satlink_acm_t *acm, float esn0_db);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_ACM_ACM_H */
