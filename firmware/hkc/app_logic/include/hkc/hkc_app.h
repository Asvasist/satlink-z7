/**
 * @file hkc_app.h
 * @brief Housekeeping application logic, free of register access so it runs in host tests.
 *
 * The firmware's main loop feeds it received CAN frames and the millisecond clock; it answers
 * commands, sends the housekeeping frames at the configured period, keeps the sticky error
 * flags and drives the heartbeat LED. All hardware access goes through ::hkc_app_hw_t.
 *
 * @implements SRS-HKC-005
 */
#ifndef HKC_APP_H
#define HKC_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/common/status.h"
#include "satlink/hkc/can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Application version, reported by GET_VERSION and embedded in the image header. */
#define HKC_APP_VERSION_MAJOR (1U)
#define HKC_APP_VERSION_MINOR (0U)
#define HKC_APP_VERSION_PATCH (0U)

#define HKC_APP_PERIOD_DEFAULT_MS (1000U)
#define HKC_APP_PERIOD_MIN_MS     (100U)
#define HKC_APP_PERIOD_MAX_MS     (10000U)
#define HKC_APP_HEARTBEAT_MS      (500U)

/** RGB LED bits. */
#define HKC_LED_RED   (0x1U)
#define HKC_LED_GREEN (0x2U)
#define HKC_LED_BLUE  (0x4U)

/** Sensors the application samples. */
typedef enum
{
    HKC_SENSOR_TEMP = 0,
    HKC_SENSOR_VCCINT,
    HKC_SENSOR_VCCAUX,
    HKC_SENSOR_VBRAM,
    HKC_SENSOR_VCCPINT,
    HKC_SENSOR_VCCPAUX,
    HKC_SENSOR_VCCODDR,
} hkc_sensor_t;

/** Hardware the logic needs. */
typedef struct
{
    void *ctx;
    uint16_t (*read_sensor)(void *ctx, hkc_sensor_t sensor); /**< 12-bit XADC code */
    bool (*sensor_alarm)(void *ctx);
    uint8_t (*switches)(void *ctx);
    void (*set_rgb)(void *ctx, uint8_t rgb);
    satlink_status_t (*send)(void *ctx, const satlink_can_frame_t *frame);
    /** Jump to the bootloader and stay there. Does not return on the target. */
    void (*enter_bootloader)(void *ctx);
} hkc_app_hw_t;

/** Application state. Treat as opaque. */
typedef struct
{
    hkc_app_hw_t hw;
    uint8_t reset_cause;
    uint16_t period_ms;
    uint32_t next_hk_ms;
    uint32_t next_heartbeat_ms;
    uint8_t cmd_count;
    uint8_t error_flags;
    bool led_override;
    uint8_t led;
    bool boot_pending;
    uint32_t unix_offset_s; /**< Unix time at now_ms == 0, from the last time sync (0: none). */
} hkc_app_t;

/** @p reset_cause is a ::satlink_hk_reset_cause_t. */
satlink_status_t hkc_app_init(hkc_app_t *app, const hkc_app_hw_t *hw, uint8_t reset_cause);

/** Handle one received frame (commands and time sync; everything else is ignored). */
void hkc_app_on_frame(hkc_app_t *app, const satlink_can_frame_t *frame, uint32_t now_ms);

/** Periodic work: housekeeping frames, alarm latch, heartbeat LED, pending bootloader entry. */
void hkc_app_poll(hkc_app_t *app, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* HKC_APP_H */
