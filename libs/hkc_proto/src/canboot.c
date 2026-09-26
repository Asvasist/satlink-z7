/**
 * @file canboot.c
 * @implements SRS-HKC-002
 */
#include "satlink/hkc/canboot.h"

#include <stddef.h>

#include "satlink/common/byte_order.h"
#include "satlink/common/crc32.h"

static void respond(satlink_can_frame_t *response, uint8_t opcode, uint8_t status,
                    const uint8_t *extra, uint8_t extra_len)
{
    response->id = (uint16_t)SATLINK_CANBOOT_ID_RESPONSE;
    response->dlc = (uint8_t)(2U + extra_len);
    for (uint32_t i = 0U; i < SATLINK_CAN_MAX_DLC; ++i)
    {
        response->data[i] = 0U;
    }
    response->data[0] = (uint8_t)(opcode | SATLINK_CANBOOT_RESPONSE_FLAG);
    response->data[1] = status;
    for (uint32_t i = 0U; i < (uint32_t)extra_len; ++i)
    {
        response->data[i + 2U] = extra[i];
    }
}

static void check_region(satlink_canboot_target_t *target, uint32_t len)
{
    satlink_hkc_image_header_t header;
    const satlink_hkc_image_result_t result = satlink_hkc_image_verify(
        target->region, (size_t)len, target->region_base, target->region_size, &header);
    target->image_valid = (result == SATLINK_HKC_IMAGE_OK);
    target->entry = target->image_valid ? header.entry : 0U;
}

satlink_status_t satlink_canboot_target_init(satlink_canboot_target_t *target, uint8_t *region,
                                             uint32_t region_base, uint32_t region_size,
                                             uint8_t version_major, uint8_t version_minor)
{
    if ((target == NULL) || (region == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (region_size <= SATLINK_HKC_IMAGE_HEADER_SIZE)
    {
        return SATLINK_ERR_RANGE;
    }
    target->region = region;
    target->region_base = region_base;
    target->region_size = region_size;
    target->version_major = version_major;
    target->version_minor = version_minor;
    target->state = SATLINK_CANBOOT_IDLE;
    target->image_size = 0U;
    target->received = 0U;
    target->next_seq = 0U;
    target->have_last_seq = false;
    target->boot_requested = false;
    check_region(target, region_size);
    return SATLINK_OK;
}

static uint8_t handle_start(satlink_canboot_target_t *target, const satlink_can_frame_t *request)
{
    if (target->state == SATLINK_CANBOOT_RECEIVING)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_STATE;
    }
    if (request->dlc != 5U)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_LENGTH;
    }
    const uint32_t size = satlink_get_le32(&request->data[1]);
    if ((size <= SATLINK_HKC_IMAGE_HEADER_SIZE) || (size > target->region_size))
    {
        return (uint8_t)SATLINK_CANBOOT_ST_TOO_LARGE;
    }
    /* The old image is about to be overwritten: it is no longer bootable. */
    target->image_valid = false;
    target->state = SATLINK_CANBOOT_RECEIVING;
    target->image_size = size;
    target->received = 0U;
    target->next_seq = 0U;
    target->have_last_seq = false;
    return (uint8_t)SATLINK_CANBOOT_ST_OK;
}

static uint8_t handle_data(satlink_canboot_target_t *target, const satlink_can_frame_t *request)
{
    if (target->state != SATLINK_CANBOOT_RECEIVING)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_STATE;
    }
    if (request->dlc < 3U)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_LENGTH;
    }
    const uint8_t seq = request->data[1];
    const uint8_t last_seq = (uint8_t)(target->next_seq - 1U);
    if (target->have_last_seq && (seq == last_seq))
    {
        return (uint8_t)SATLINK_CANBOOT_ST_OK; /* retransmission: already written */
    }
    if (seq != target->next_seq)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_SEQ;
    }
    const uint32_t len = (uint32_t)request->dlc - 2U;
    if ((target->received + len) > target->image_size)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_LENGTH;
    }
    for (uint32_t i = 0U; i < len; ++i)
    {
        target->region[target->received + i] = request->data[i + 2U];
    }
    target->received += len;
    target->next_seq = (uint8_t)(seq + 1U);
    target->have_last_seq = true;
    return (uint8_t)SATLINK_CANBOOT_ST_OK;
}

static uint8_t handle_end(satlink_canboot_target_t *target, const satlink_can_frame_t *request,
                          uint32_t *crc_out)
{
    *crc_out = 0U;
    if (target->state != SATLINK_CANBOOT_RECEIVING)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_STATE;
    }
    if (request->dlc != 5U)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_LENGTH;
    }
    if (target->received != target->image_size)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_LENGTH;
    }
    *crc_out = satlink_crc32(target->region, (size_t)target->received);
    if (*crc_out != satlink_get_le32(&request->data[1]))
    {
        target->state = SATLINK_CANBOOT_IDLE;
        return (uint8_t)SATLINK_CANBOOT_ST_CRC;
    }
    check_region(target, target->received);
    target->state = SATLINK_CANBOOT_IDLE;
    if (!target->image_valid)
    {
        return (uint8_t)SATLINK_CANBOOT_ST_BAD_IMAGE;
    }
    target->state = SATLINK_CANBOOT_COMPLETE;
    return (uint8_t)SATLINK_CANBOOT_ST_OK;
}

bool satlink_canboot_target_handle(satlink_canboot_target_t *target,
                                   const satlink_can_frame_t *request,
                                   satlink_can_frame_t *response)
{
    if ((target == NULL) || (request == NULL) || (response == NULL) ||
        (request->id != SATLINK_CANBOOT_ID_REQUEST))
    {
        return false;
    }
    if ((request->dlc == 0U) || (request->dlc > SATLINK_CAN_MAX_DLC))
    {
        respond(response, 0U, (uint8_t)SATLINK_CANBOOT_ST_BAD_LENGTH, NULL, 0U);
        return true;
    }

    const uint8_t opcode = request->data[0];
    switch (opcode)
    {
    case (uint8_t)SATLINK_CANBOOT_OP_PING:
    {
        const uint8_t extra[4] = {target->version_major, target->version_minor,
                                  target->image_valid ? 1U : 0U, (uint8_t)target->state};
        respond(response, opcode, (uint8_t)SATLINK_CANBOOT_ST_OK, extra, 4U);
        break;
    }
    case (uint8_t)SATLINK_CANBOOT_OP_START:
        respond(response, opcode, handle_start(target, request), NULL, 0U);
        break;
    case (uint8_t)SATLINK_CANBOOT_OP_DATA:
    {
        const uint8_t seq = request->data[1];
        respond(response, opcode, handle_data(target, request), &seq, 1U);
        break;
    }
    case (uint8_t)SATLINK_CANBOOT_OP_END:
    {
        uint32_t crc = 0U;
        const uint8_t status = handle_end(target, request, &crc);
        uint8_t extra[4];
        satlink_put_le32(extra, crc);
        respond(response, opcode, status, extra, 4U);
        break;
    }
    case (uint8_t)SATLINK_CANBOOT_OP_BOOT:
        if (target->image_valid && (target->state != SATLINK_CANBOOT_RECEIVING))
        {
            target->boot_requested = true;
            respond(response, opcode, (uint8_t)SATLINK_CANBOOT_ST_OK, NULL, 0U);
        }
        else
        {
            respond(response, opcode, (uint8_t)SATLINK_CANBOOT_ST_BAD_IMAGE, NULL, 0U);
        }
        break;
    case (uint8_t)SATLINK_CANBOOT_OP_ABORT:
        if (target->state == SATLINK_CANBOOT_RECEIVING)
        {
            target->state = SATLINK_CANBOOT_IDLE;
        }
        respond(response, opcode, (uint8_t)SATLINK_CANBOOT_ST_OK, NULL, 0U);
        break;
    default:
        respond(response, opcode, (uint8_t)SATLINK_CANBOOT_ST_UNKNOWN, NULL, 0U);
        break;
    }
    return true;
}

satlink_status_t satlink_canboot_encode_request(uint8_t opcode, const uint8_t *payload, uint8_t len,
                                                satlink_can_frame_t *frame)
{
    if ((frame == NULL) || ((payload == NULL) && (len > 0U)))
    {
        return SATLINK_ERR_NULL;
    }
    if (len > (SATLINK_CAN_MAX_DLC - 1U))
    {
        return SATLINK_ERR_RANGE;
    }
    frame->id = (uint16_t)SATLINK_CANBOOT_ID_REQUEST;
    frame->dlc = (uint8_t)(len + 1U);
    for (uint32_t i = 0U; i < SATLINK_CAN_MAX_DLC; ++i)
    {
        frame->data[i] = 0U;
    }
    frame->data[0] = opcode;
    for (uint32_t i = 0U; i < (uint32_t)len; ++i)
    {
        frame->data[i + 1U] = payload[i];
    }
    return SATLINK_OK;
}
