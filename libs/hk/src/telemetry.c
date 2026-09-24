/**
 * @file telemetry.c
 * @implements SRS-HKC-001
 */
#include "satlink/hk/telemetry.h"

#include <stddef.h>

#define HK_FRAME_DLC  (8U)
#define HK_LEVEL_MASK (0x03U)

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)(v >> 8U);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8U));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static void frame_clear(satlink_can_frame_t *frame, uint32_t id)
{
    frame->id = id;
    frame->dlc = (uint8_t)HK_FRAME_DLC;
    frame->extended = false;
    frame->rtr = false;
    for (size_t i = 0U; i < SATLINK_CAN_MAX_DLC; ++i)
    {
        frame->data[i] = 0U;
    }
}

static uint8_t pack_levels(const satlink_hk_snapshot_t *s)
{
    return (uint8_t)(((uint32_t)s->temp_level & HK_LEVEL_MASK) |
                     (((uint32_t)s->vccint_level & HK_LEVEL_MASK) << 2U) |
                     (((uint32_t)s->vccaux_level & HK_LEVEL_MASK) << 4U) |
                     (((uint32_t)s->vccbram_level & HK_LEVEL_MASK) << 6U));
}

static satlink_hk_level_t level_from(uint8_t levels, uint32_t shift)
{
    const uint32_t raw = ((uint32_t)levels >> shift) & HK_LEVEL_MASK;

    /* The two-bit field can hold 3, which is not a level; treat it as the worst one. */
    return (raw > (uint32_t)SATLINK_HK_ALARM) ? SATLINK_HK_ALARM : (satlink_hk_level_t)raw;
}

satlink_status_t satlink_hk_pack(const satlink_hk_snapshot_t *snapshot, uint8_t seq,
                                 satlink_can_frame_t frames[SATLINK_HK_FRAME_COUNT])
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((snapshot != NULL) && (frames != NULL))
    {
        frame_clear(&frames[0], SATLINK_HK_ID_THERMAL);
        frames[0].data[0] = seq;
        frames[0].data[1] = pack_levels(snapshot);
        put_u32(&frames[0].data[2], (uint32_t)snapshot->temp_mdegc);

        frame_clear(&frames[1], SATLINK_HK_ID_SUPPLY);
        frames[1].data[0] = seq;
        put_u16(&frames[1].data[2], snapshot->vccint_mv);
        put_u16(&frames[1].data[4], snapshot->vccaux_mv);
        put_u16(&frames[1].data[6], snapshot->vccbram_mv);

        frame_clear(&frames[2], SATLINK_HK_ID_STATUS);
        frames[2].data[0] = seq;
        frames[2].data[1] = snapshot->watchdog_reset ? 0x01U : 0x00U;
        put_u32(&frames[2].data[2], snapshot->uptime_s);

        status = SATLINK_OK;
    }

    return status;
}

satlink_status_t satlink_hk_decode(const satlink_can_frame_t *frame,
                                   satlink_hk_snapshot_t *snapshot, uint8_t *seq)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((frame != NULL) && (snapshot != NULL) && (seq != NULL))
    {
        status = SATLINK_ERR_RANGE;

        if ((!frame->extended) && (!frame->rtr) && (frame->dlc == HK_FRAME_DLC))
        {
            if (frame->id == SATLINK_HK_ID_THERMAL)
            {
                snapshot->temp_level = level_from(frame->data[1], 0U);
                snapshot->vccint_level = level_from(frame->data[1], 2U);
                snapshot->vccaux_level = level_from(frame->data[1], 4U);
                snapshot->vccbram_level = level_from(frame->data[1], 6U);
                snapshot->temp_mdegc = (int32_t)get_u32(&frame->data[2]);
                *seq = frame->data[0];
                status = SATLINK_OK;
            }
            else if (frame->id == SATLINK_HK_ID_SUPPLY)
            {
                snapshot->vccint_mv = get_u16(&frame->data[2]);
                snapshot->vccaux_mv = get_u16(&frame->data[4]);
                snapshot->vccbram_mv = get_u16(&frame->data[6]);
                *seq = frame->data[0];
                status = SATLINK_OK;
            }
            else if (frame->id == SATLINK_HK_ID_STATUS)
            {
                snapshot->watchdog_reset = ((frame->data[1] & 0x01U) != 0U);
                snapshot->uptime_s = get_u32(&frame->data[2]);
                *seq = frame->data[0];
                status = SATLINK_OK;
            }
            else
            {
                /* not a housekeeping frame */
            }
        }
    }

    return status;
}
