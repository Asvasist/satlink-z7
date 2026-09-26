/**
 * @file hk_proto.c
 * @implements SRS-HKC-003
 */
#include "satlink/hkc/hk_proto.h"

#include <stddef.h>

#include "satlink/common/byte_order.h"

static void frame_init(satlink_can_frame_t *frame, uint16_t id, uint8_t dlc)
{
    frame->id = id;
    frame->dlc = dlc;
    for (uint32_t i = 0U; i < SATLINK_CAN_MAX_DLC; ++i)
    {
        frame->data[i] = 0U;
    }
}

static bool frame_matches(const satlink_can_frame_t *frame, uint16_t id, uint8_t min_dlc)
{
    return (frame->id == id) && (frame->dlc >= min_dlc) && (frame->dlc <= SATLINK_CAN_MAX_DLC);
}

satlink_status_t satlink_hk_encode_env(const satlink_hk_env_t *env, satlink_can_frame_t *frame)
{
    if ((env == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    frame_init(frame, (uint16_t)SATLINK_HK_ID_ENV, 8U);
    satlink_put_le16(&frame->data[0], (uint16_t)env->die_temp_centi_c);
    satlink_put_le16(&frame->data[2], env->vccint_mv);
    satlink_put_le16(&frame->data[4], env->vccaux_mv);
    satlink_put_le16(&frame->data[6], env->vbram_mv);
    return SATLINK_OK;
}

satlink_status_t satlink_hk_decode_env(const satlink_can_frame_t *frame, satlink_hk_env_t *env)
{
    if ((env == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (!frame_matches(frame, (uint16_t)SATLINK_HK_ID_ENV, 8U))
    {
        return SATLINK_ERR_RANGE;
    }
    env->die_temp_centi_c = (int16_t)satlink_get_le16(&frame->data[0]);
    env->vccint_mv = satlink_get_le16(&frame->data[2]);
    env->vccaux_mv = satlink_get_le16(&frame->data[4]);
    env->vbram_mv = satlink_get_le16(&frame->data[6]);
    return SATLINK_OK;
}

satlink_status_t satlink_hk_encode_supply(const satlink_hk_supply_t *supply,
                                          satlink_can_frame_t *frame)
{
    if ((supply == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    frame_init(frame, (uint16_t)SATLINK_HK_ID_SUPPLY, 6U);
    satlink_put_le16(&frame->data[0], supply->vccpint_mv);
    satlink_put_le16(&frame->data[2], supply->vccpaux_mv);
    satlink_put_le16(&frame->data[4], supply->vcco_ddr_mv);
    return SATLINK_OK;
}

satlink_status_t satlink_hk_decode_supply(const satlink_can_frame_t *frame,
                                          satlink_hk_supply_t *supply)
{
    if ((supply == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (!frame_matches(frame, (uint16_t)SATLINK_HK_ID_SUPPLY, 6U))
    {
        return SATLINK_ERR_RANGE;
    }
    supply->vccpint_mv = satlink_get_le16(&frame->data[0]);
    supply->vccpaux_mv = satlink_get_le16(&frame->data[2]);
    supply->vcco_ddr_mv = satlink_get_le16(&frame->data[4]);
    return SATLINK_OK;
}

satlink_status_t satlink_hk_encode_status(const satlink_hk_status_t *status,
                                          satlink_can_frame_t *frame)
{
    if ((status == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    frame_init(frame, (uint16_t)SATLINK_HK_ID_STATUS, 8U);
    satlink_put_le32(&frame->data[0], status->uptime_s);
    frame->data[4] = status->reset_cause;
    frame->data[5] = status->switches;
    frame->data[6] = status->cmd_count;
    frame->data[7] = status->error_flags;
    return SATLINK_OK;
}

satlink_status_t satlink_hk_decode_status(const satlink_can_frame_t *frame,
                                          satlink_hk_status_t *status)
{
    if ((status == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (!frame_matches(frame, (uint16_t)SATLINK_HK_ID_STATUS, 8U))
    {
        return SATLINK_ERR_RANGE;
    }
    status->uptime_s = satlink_get_le32(&frame->data[0]);
    status->reset_cause = frame->data[4];
    status->switches = frame->data[5];
    status->cmd_count = frame->data[6];
    status->error_flags = frame->data[7];
    return SATLINK_OK;
}

satlink_status_t satlink_hk_encode_time(const satlink_hk_time_t *time, satlink_can_frame_t *frame)
{
    if ((time == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (time->millis > 999U)
    {
        return SATLINK_ERR_RANGE;
    }
    frame_init(frame, (uint16_t)SATLINK_HK_ID_TIME_SYNC, 6U);
    satlink_put_le32(&frame->data[0], time->unix_s);
    satlink_put_le16(&frame->data[4], time->millis);
    return SATLINK_OK;
}

satlink_status_t satlink_hk_decode_time(const satlink_can_frame_t *frame, satlink_hk_time_t *time)
{
    if ((time == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (!frame_matches(frame, (uint16_t)SATLINK_HK_ID_TIME_SYNC, 6U))
    {
        return SATLINK_ERR_RANGE;
    }
    const uint16_t millis = satlink_get_le16(&frame->data[4]);
    if (millis > 999U)
    {
        return SATLINK_ERR_RANGE;
    }
    time->unix_s = satlink_get_le32(&frame->data[0]);
    time->millis = millis;
    return SATLINK_OK;
}

satlink_status_t satlink_hk_encode_command(uint8_t opcode, const uint8_t *argv, uint8_t argc,
                                           satlink_can_frame_t *frame)
{
    if ((frame == NULL) || ((argv == NULL) && (argc > 0U)))
    {
        return SATLINK_ERR_NULL;
    }
    if (argc > (SATLINK_CAN_MAX_DLC - 1U))
    {
        return SATLINK_ERR_RANGE;
    }
    frame_init(frame, (uint16_t)SATLINK_HK_ID_COMMAND, (uint8_t)(argc + 1U));
    frame->data[0] = opcode;
    for (uint32_t i = 0U; i < (uint32_t)argc; ++i)
    {
        frame->data[i + 1U] = argv[i];
    }
    return SATLINK_OK;
}

satlink_status_t satlink_hk_decode_command(const satlink_can_frame_t *frame,
                                           satlink_hk_command_t *command)
{
    if ((frame == NULL) || (command == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (!frame_matches(frame, (uint16_t)SATLINK_HK_ID_COMMAND, 1U))
    {
        return SATLINK_ERR_RANGE;
    }
    command->opcode = frame->data[0];
    command->argc = (uint8_t)(frame->dlc - 1U);
    for (uint32_t i = 0U; i < (SATLINK_CAN_MAX_DLC - 1U); ++i)
    {
        command->argv[i] = (i < (uint32_t)command->argc) ? frame->data[i + 1U] : 0U;
    }
    return SATLINK_OK;
}

satlink_status_t satlink_hk_encode_ack(const satlink_hk_ack_t *ack, satlink_can_frame_t *frame)
{
    if ((ack == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (ack->datac > (SATLINK_CAN_MAX_DLC - 2U))
    {
        return SATLINK_ERR_RANGE;
    }
    frame_init(frame, (uint16_t)SATLINK_HK_ID_COMMAND_ACK, (uint8_t)(ack->datac + 2U));
    frame->data[0] = ack->opcode;
    frame->data[1] = ack->status;
    for (uint32_t i = 0U; i < (uint32_t)ack->datac; ++i)
    {
        frame->data[i + 2U] = ack->datav[i];
    }
    return SATLINK_OK;
}

satlink_status_t satlink_hk_decode_ack(const satlink_can_frame_t *frame, satlink_hk_ack_t *ack)
{
    if ((frame == NULL) || (ack == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (!frame_matches(frame, (uint16_t)SATLINK_HK_ID_COMMAND_ACK, 2U))
    {
        return SATLINK_ERR_RANGE;
    }
    ack->opcode = frame->data[0];
    ack->status = frame->data[1];
    ack->datac = (uint8_t)(frame->dlc - 2U);
    for (uint32_t i = 0U; i < (SATLINK_CAN_MAX_DLC - 2U); ++i)
    {
        ack->datav[i] = (i < (uint32_t)ack->datac) ? frame->data[i + 2U] : 0U;
    }
    return SATLINK_OK;
}

int16_t satlink_hk_xadc_temp_centi_c(uint16_t code12)
{
    /* UG480: T[degC] = code * 503.975 / 4096 - 273.15. In 0.01 degC with integer maths:
     * code * 50397.5 / 4096 - 27315 = (code * 100795 / 8192) - 27315. */
    const int32_t code = (int32_t)(code12 & 0x0FFFU);
    const int32_t centi = ((code * 100795) / 8192) - 27315;
    return (int16_t)centi;
}

uint16_t satlink_hk_xadc_supply_mv(uint16_t code12)
{
    /* UG480: V = code * 3 V / 4096. */
    const uint32_t code = (uint32_t)(code12 & 0x0FFFU);
    return (uint16_t)((code * 3000U) / 4096U);
}
