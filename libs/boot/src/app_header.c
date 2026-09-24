/**
 * @file app_header.c
 * @implements SRS-HKC-003
 */
#include "satlink/boot/app_header.h"

#include <stddef.h>

#include "satlink/common/crc16_ccitt.h"

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8U));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

satlink_status_t satlink_app_header_parse(const uint8_t *raw, satlink_app_header_t *header)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((raw != NULL) && (header != NULL))
    {
        header->magic = get_u32(&raw[0]);
        header->header_version = get_u16(&raw[4]);
        header->header_len = get_u16(&raw[6]);
        header->image_size = get_u32(&raw[8]);
        header->crc16 = get_u16(&raw[12]);
        header->reserved = get_u16(&raw[14]);
        status = SATLINK_OK;
    }

    return status;
}

satlink_status_t satlink_app_validate(const uint8_t *image, size_t available, size_t *entry_offset)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((image != NULL) && (entry_offset != NULL))
    {
        satlink_app_header_t header = {0U, 0U, 0U, 0U, 0U, 0U};

        if (available < SATLINK_APP_HEADER_SIZE)
        {
            status = SATLINK_ERR_RANGE;
        }
        else
        {
            status = satlink_app_header_parse(image, &header);
        }

        if (status == SATLINK_OK)
        {
            if (header.magic != SATLINK_APP_MAGIC)
            {
                status = SATLINK_ERR_STATE; /* nothing that looks like an application */
            }
            else if ((header.header_version != SATLINK_APP_HEADER_VERSION) ||
                     (header.header_len != SATLINK_APP_HEADER_SIZE) ||
                     (header.image_size <= (uint32_t)header.header_len) ||
                     ((size_t)header.image_size > available))
            {
                status = SATLINK_ERR_RANGE;
            }
            else
            {
                const size_t payload_len = (size_t)header.image_size - (size_t)header.header_len;
                const uint16_t crc = satlink_crc16_ccitt(&image[header.header_len], payload_len);

                if (crc == header.crc16)
                {
                    *entry_offset = (size_t)header.header_len;
                }
                else
                {
                    status = SATLINK_ERR_CRC;
                }
            }
        }
    }

    return status;
}
