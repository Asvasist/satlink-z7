/**
 * @file image.c
 * @implements SRS-HKC-004
 */
#include "satlink/hkc/image.h"

#include "satlink/common/byte_order.h"
#include "satlink/common/crc32.h"

#define HEADER_CRC_SPAN (28U)

satlink_status_t satlink_hkc_image_write_header(const satlink_hkc_image_header_t *header,
                                                uint8_t out[SATLINK_HKC_IMAGE_HEADER_SIZE])
{
    if ((header == NULL) || (out == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    satlink_put_le32(&out[0], header->magic);
    satlink_put_le16(&out[4], header->header_version);
    satlink_put_le16(&out[6], header->header_size);
    satlink_put_le32(&out[8], header->load_addr);
    satlink_put_le32(&out[12], header->entry);
    satlink_put_le32(&out[16], header->payload_size);
    satlink_put_le32(&out[20], header->payload_crc32);
    out[24] = header->version_major;
    out[25] = header->version_minor;
    out[26] = header->version_patch;
    out[27] = header->flags;
    satlink_put_le32(&out[28], satlink_crc32(out, HEADER_CRC_SPAN));
    return SATLINK_OK;
}

satlink_status_t satlink_hkc_image_read_header(const uint8_t *image, size_t len,
                                               satlink_hkc_image_header_t *header)
{
    if ((image == NULL) || (header == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (len < SATLINK_HKC_IMAGE_HEADER_SIZE)
    {
        return SATLINK_ERR_RANGE;
    }
    header->magic = satlink_get_le32(&image[0]);
    header->header_version = satlink_get_le16(&image[4]);
    header->header_size = satlink_get_le16(&image[6]);
    header->load_addr = satlink_get_le32(&image[8]);
    header->entry = satlink_get_le32(&image[12]);
    header->payload_size = satlink_get_le32(&image[16]);
    header->payload_crc32 = satlink_get_le32(&image[20]);
    header->version_major = image[24];
    header->version_minor = image[25];
    header->version_patch = image[26];
    header->flags = image[27];
    header->header_crc32 = satlink_get_le32(&image[28]);
    return SATLINK_OK;
}

satlink_hkc_image_result_t satlink_hkc_image_verify(const uint8_t *image, size_t len,
                                                    uint32_t region_base, uint32_t region_size,
                                                    satlink_hkc_image_header_t *header)
{
    satlink_hkc_image_header_t local;
    satlink_hkc_image_header_t *const h = (header != NULL) ? header : &local;

    if ((image == NULL) || (satlink_hkc_image_read_header(image, len, h) != SATLINK_OK))
    {
        return SATLINK_HKC_IMAGE_TOO_SHORT;
    }
    if (h->magic != SATLINK_HKC_IMAGE_MAGIC)
    {
        return SATLINK_HKC_IMAGE_BAD_MAGIC;
    }
    if ((h->header_version != SATLINK_HKC_IMAGE_HEADER_VERSION) ||
        (h->header_size != SATLINK_HKC_IMAGE_HEADER_SIZE))
    {
        return SATLINK_HKC_IMAGE_BAD_HEADER;
    }
    if (satlink_crc32(image, HEADER_CRC_SPAN) != h->header_crc32)
    {
        return SATLINK_HKC_IMAGE_HEADER_CRC;
    }

    /* 64-bit arithmetic: a hostile payload_size must not wrap the comparisons. */
    const uint64_t total = (uint64_t)SATLINK_HKC_IMAGE_HEADER_SIZE + (uint64_t)h->payload_size;
    const uint64_t payload_start = (uint64_t)region_base + SATLINK_HKC_IMAGE_HEADER_SIZE;
    const uint64_t payload_end = payload_start + (uint64_t)h->payload_size;
    if ((h->load_addr != region_base) || (total > (uint64_t)region_size) ||
        (h->payload_size == 0U) || ((uint64_t)h->entry < payload_start) ||
        ((uint64_t)h->entry >= payload_end))
    {
        return SATLINK_HKC_IMAGE_BAD_LOCATION;
    }
    if (total > (uint64_t)len)
    {
        return SATLINK_HKC_IMAGE_TOO_SHORT;
    }
    if (satlink_crc32(&image[SATLINK_HKC_IMAGE_HEADER_SIZE], (size_t)h->payload_size) !=
        h->payload_crc32)
    {
        return SATLINK_HKC_IMAGE_PAYLOAD_CRC;
    }
    return SATLINK_HKC_IMAGE_OK;
}
