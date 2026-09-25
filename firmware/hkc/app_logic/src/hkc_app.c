/**
 * @file hkc_app.c
 * @implements SRS-HKC-005
 */
#include "hkc/hkc_app.h"

#include <stddef.h>

#include "satlink/common/byte_order.h"
#include "satlink/hkc/hk_proto.h"

#define ENTER_BOOT_KEY0 (0xB0U)
#define ENTER_BOOT_KEY1 (0x07U)

/* Wrap-safe "now has reached deadline" for a free-running millisecond counter. */
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void send(hkc_app_t *app, const satlink_can_frame_t *frame)
{
    if (app->hw.send(app->hw.ctx, frame) != SATLINK_OK)
    {
        app->error_flags |= (uint8_t)SATLINK_HK_ERR_CAN_TX;
    }
}

static void send_ack(hkc_app_t *app, uint8_t opcode, uint8_t status, const uint8_t *data,
                     uint8_t len)
{
    satlink_hk_ack_t ack;
    ack.opcode = opcode;
    ack.status = status;
    ack.datac = len;
    for (uint32_t i = 0U; i < (SATLINK_CAN_MAX_DLC - 2U); ++i)
    {
        ack.datav[i] = (i < (uint32_t)len) ? data[i] : 0U;
    }
    satlink_can_frame_t frame;
    if (satlink_hk_encode_ack(&ack, &frame) == SATLINK_OK)
    {
        send(app, &frame);
    }
}

satlink_status_t hkc_app_init(hkc_app_t *app, const hkc_app_hw_t *hw, uint8_t reset_cause)
{
    if ((app == NULL) || (hw == NULL) || (hw->read_sensor == NULL) || (hw->sensor_alarm == NULL) ||
        (hw->switches == NULL) || (hw->set_rgb == NULL) || (hw->send == NULL) ||
        (hw->enter_bootloader == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    app->hw = *hw;
    app->reset_cause = reset_cause;
    app->period_ms = (uint16_t)HKC_APP_PERIOD_DEFAULT_MS;
    app->next_hk_ms = 0U;
    app->next_heartbeat_ms = 0U;
    app->cmd_count = 0U;
    app->error_flags = 0U;
    app->led_override = false;
    app->led = 0U;
    app->boot_pending = false;
    app->unix_offset_s = 0U;
    return SATLINK_OK;
}

static uint8_t handle_command(hkc_app_t *app, const satlink_hk_command_t *cmd, uint8_t *data,
                              uint8_t *len, uint32_t now_ms)
{
    *len = 0U;
    switch (cmd->opcode)
    {
    case (uint8_t)SATLINK_HK_CMD_SET_LED:
        if (cmd->argc < 1U)
        {
            return (uint8_t)SATLINK_HK_ACK_BAD_ARG;
        }
        app->led_override = true;
        app->led = (uint8_t)(cmd->argv[0] & 0x07U);
        app->hw.set_rgb(app->hw.ctx, app->led);
        return (uint8_t)SATLINK_HK_ACK_OK;

    case (uint8_t)SATLINK_HK_CMD_SET_PERIOD:
    {
        if (cmd->argc < 2U)
        {
            return (uint8_t)SATLINK_HK_ACK_BAD_ARG;
        }
        const uint16_t period = satlink_get_le16(&cmd->argv[0]);
        if ((period < HKC_APP_PERIOD_MIN_MS) || (period > HKC_APP_PERIOD_MAX_MS))
        {
            return (uint8_t)SATLINK_HK_ACK_BAD_ARG;
        }
        app->period_ms = period;
        app->next_hk_ms = now_ms; /* report with the new period straight away */
        return (uint8_t)SATLINK_HK_ACK_OK;
    }

    case (uint8_t)SATLINK_HK_CMD_ENTER_BOOT:
        if ((cmd->argc < 2U) || (cmd->argv[0] != ENTER_BOOT_KEY0) ||
            (cmd->argv[1] != ENTER_BOOT_KEY1))
        {
            return (uint8_t)SATLINK_HK_ACK_BAD_ARG;
        }
        app->boot_pending = true; /* after the ACK has gone out */
        return (uint8_t)SATLINK_HK_ACK_OK;

    case (uint8_t)SATLINK_HK_CMD_GET_VERSION:
        data[0] = (uint8_t)HKC_APP_VERSION_MAJOR;
        data[1] = (uint8_t)HKC_APP_VERSION_MINOR;
        data[2] = (uint8_t)HKC_APP_VERSION_PATCH;
        *len = 3U;
        return (uint8_t)SATLINK_HK_ACK_OK;

    case (uint8_t)SATLINK_HK_CMD_CLEAR_ERRORS:
        app->error_flags = 0U;
        return (uint8_t)SATLINK_HK_ACK_OK;

    default:
        return (uint8_t)SATLINK_HK_ACK_UNKNOWN;
    }
}

void hkc_app_on_frame(hkc_app_t *app, const satlink_can_frame_t *frame, uint32_t now_ms)
{
    if ((app == NULL) || (frame == NULL))
    {
        return;
    }
    if (frame->id == SATLINK_HK_ID_TIME_SYNC)
    {
        satlink_hk_time_t time;
        if (satlink_hk_decode_time(frame, &time) == SATLINK_OK)
        {
            app->unix_offset_s = time.unix_s - (now_ms / 1000U);
        }
        return;
    }

    satlink_hk_command_t cmd;
    if (satlink_hk_decode_command(frame, &cmd) != SATLINK_OK)
    {
        return;
    }
    uint8_t data[SATLINK_CAN_MAX_DLC - 2U] = {0U};
    uint8_t len = 0U;
    const uint8_t status = handle_command(app, &cmd, data, &len, now_ms);
    if (status == (uint8_t)SATLINK_HK_ACK_OK)
    {
        app->cmd_count = (uint8_t)(app->cmd_count + 1U);
    }
    send_ack(app, cmd.opcode, status, data, len);
}

static uint16_t sensor_mv(hkc_app_t *app, hkc_sensor_t sensor)
{
    return satlink_hk_xadc_supply_mv(app->hw.read_sensor(app->hw.ctx, sensor));
}

static void send_housekeeping(hkc_app_t *app, uint32_t now_ms)
{
    satlink_can_frame_t frame;

    satlink_hk_env_t env;
    env.die_temp_centi_c =
        satlink_hk_xadc_temp_centi_c(app->hw.read_sensor(app->hw.ctx, HKC_SENSOR_TEMP));
    env.vccint_mv = sensor_mv(app, HKC_SENSOR_VCCINT);
    env.vccaux_mv = sensor_mv(app, HKC_SENSOR_VCCAUX);
    env.vbram_mv = sensor_mv(app, HKC_SENSOR_VBRAM);
    (void)satlink_hk_encode_env(&env, &frame);
    send(app, &frame);

    satlink_hk_supply_t supply;
    supply.vccpint_mv = sensor_mv(app, HKC_SENSOR_VCCPINT);
    supply.vccpaux_mv = sensor_mv(app, HKC_SENSOR_VCCPAUX);
    supply.vcco_ddr_mv = sensor_mv(app, HKC_SENSOR_VCCODDR);
    (void)satlink_hk_encode_supply(&supply, &frame);
    send(app, &frame);

    satlink_hk_status_t status;
    status.uptime_s = now_ms / 1000U;
    status.reset_cause = app->reset_cause;
    status.switches = app->hw.switches(app->hw.ctx);
    status.cmd_count = app->cmd_count;
    status.error_flags = app->error_flags;
    (void)satlink_hk_encode_status(&status, &frame);
    send(app, &frame);
}

void hkc_app_poll(hkc_app_t *app, uint32_t now_ms)
{
    if (app == NULL)
    {
        return;
    }
    if (app->boot_pending)
    {
        app->boot_pending = false;
        app->hw.enter_bootloader(app->hw.ctx);
        return;
    }
    if (app->hw.sensor_alarm(app->hw.ctx))
    {
        app->error_flags |= (uint8_t)SATLINK_HK_ERR_XADC_ALARM;
    }
    if (time_reached(now_ms, app->next_hk_ms))
    {
        send_housekeeping(app, now_ms);
        app->next_hk_ms = now_ms + app->period_ms;
    }
    if (!app->led_override && time_reached(now_ms, app->next_heartbeat_ms))
    {
        /* Green heartbeat while healthy, red while any error flag is set. */
        const uint8_t colour = (app->error_flags != 0U) ? HKC_LED_RED : HKC_LED_GREEN;
        app->led = ((app->led & colour) != 0U) ? 0U : colour;
        app->hw.set_rgb(app->hw.ctx, app->led);
        app->next_heartbeat_ms = now_ms + HKC_APP_HEARTBEAT_MS;
    }
}
