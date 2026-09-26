/**
 * @file image.h
 * @brief Firmware image format for the housekeeping controller (HKC) application.
 *
 * An image is a 32-byte header followed by the raw binary (objcopy -O binary) of the
 * application. It is loaded to the start of the LMB application region (`lmb_app` in the
 * address map); the payload therefore starts at `load_addr + 32`.
 *
 * | Offset | Size | Field           | Meaning                                              |
 * |--------|------|-----------------|------------------------------------------------------|
 * | 0      | 4    | magic           | 0x4B484C53 ("SLHK" little-endian)                    |
 * | 4      | 2    | header_version  | 1                                                    |
 * | 6      | 2    | header_size     | 32                                                   |
 * | 8      | 4    | load_addr       | Address of the header in HKC memory                  |
 * | 12     | 4    | entry           | Entry point; must lie inside the payload             |
 * | 16     | 4    | payload_size    | Bytes after the header                               |
 * | 20     | 4    | payload_crc32   | CRC-32 of the payload                                |
 * | 24     | 1+1+1| version         | Application version major, minor, patch              |
 * | 27     | 1    | flags           | Reserved, 0                                          |
 * | 28     | 4    | header_crc32    | CRC-32 of bytes 0..27                                |
 *
 * All fields are little-endian. tools/hkc/mkimage.py writes this format.
 *
 * @implements SRS-HKC-004
 */
#ifndef SATLINK_HKC_IMAGE_H
#define SATLINK_HKC_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_HKC_IMAGE_MAGIC          (0x4B484C53UL)
#define SATLINK_HKC_IMAGE_HEADER_VERSION (1U)
#define SATLINK_HKC_IMAGE_HEADER_SIZE    (32U)

/** Decoded image header. */
typedef struct
{
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t load_addr;
    uint32_t entry;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_patch;
    uint8_t flags;
    uint32_t header_crc32;
} satlink_hkc_image_header_t;

/** Why an image was rejected. */
typedef enum
{
    SATLINK_HKC_IMAGE_OK = 0,
    SATLINK_HKC_IMAGE_TOO_SHORT, /**< Fewer bytes than a header, or than header + payload. */
    SATLINK_HKC_IMAGE_BAD_MAGIC,
    SATLINK_HKC_IMAGE_BAD_HEADER,   /**< Unknown header version or size. */
    SATLINK_HKC_IMAGE_HEADER_CRC,   /**< Header CRC does not match. */
    SATLINK_HKC_IMAGE_BAD_LOCATION, /**< Wrong load address, entry outside payload, too large. */
    SATLINK_HKC_IMAGE_PAYLOAD_CRC,  /**< Payload CRC does not match. */
} satlink_hkc_image_result_t;

/**
 * @brief Serialize @p header into @p out (32 bytes), computing header_crc32.
 *
 * The header_crc32 field of @p header is ignored; the computed value is written.
 */
satlink_status_t satlink_hkc_image_write_header(const satlink_hkc_image_header_t *header,
                                                uint8_t out[SATLINK_HKC_IMAGE_HEADER_SIZE]);

/** Decode the first 32 bytes of @p image without any checks beyond the length. */
satlink_status_t satlink_hkc_image_read_header(const uint8_t *image, size_t len,
                                               satlink_hkc_image_header_t *header);

/**
 * @brief Full check of an image placed at @p region_base in a region of @p region_size bytes.
 *
 * Checks magic, header version and size, header CRC, that the header sits at @p region_base,
 * that header + payload fit in the region and in @p len, that the entry point lies inside the
 * payload, and the payload CRC. @p header may be NULL.
 */
satlink_hkc_image_result_t satlink_hkc_image_verify(const uint8_t *image, size_t len,
                                                    uint32_t region_base, uint32_t region_size,
                                                    satlink_hkc_image_header_t *header);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_HKC_IMAGE_H */
