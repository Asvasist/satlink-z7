/**
 * @file app_header.h
 * @brief Header at the start of a housekeeping application image, and its validation.
 *
 * The application lives in the volatile LMB memory of the MicroBlaze V. It survives a reset of
 * the housekeeping subsystem (only the PL configuration clears it), so after a watchdog reset the
 * bootloader can check the image that is still there and start it without a new upload. The
 * header makes that check possible:
 *
 *   offset  size  field
 *   0       4     magic "SLAP"                 (0x50414C53 little endian)
 *   4       2     header version               (1)
 *   6       2     header length in bytes       (16; the code starts here)
 *   8       4     image size in bytes          (header included)
 *   12      2     CRC-16-CCITT of bytes [header length, image size)
 *   14      2     reserved, zero
 *
 * tools/hkc/mkapp.py writes the header into the linked image.
 *
 * @implements SRS-HKC-003
 */
#ifndef SATLINK_BOOT_APP_HEADER_H
#define SATLINK_BOOT_APP_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_APP_MAGIC          (0x50414C53U)
#define SATLINK_APP_HEADER_VERSION (1U)
#define SATLINK_APP_HEADER_SIZE    (16U)

typedef struct
{
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_len;
    uint32_t image_size;
    uint16_t crc16;
    uint16_t reserved;
} satlink_app_header_t;

/** Decode the 16 header bytes. Only reads; does not judge the values. */
satlink_status_t satlink_app_header_parse(const uint8_t *raw, satlink_app_header_t *header);

/**
 * @brief Check the image at @p image, which has @p available readable bytes.
 * @param entry_offset  Receives the offset of the first instruction (the header length).
 * @return ::SATLINK_OK; ::SATLINK_ERR_STATE if there is no header (wrong magic, so no
 *         application); ::SATLINK_ERR_RANGE if the header fields are inconsistent or the image
 *         does not fit in @p available; ::SATLINK_ERR_CRC if the contents do not match the CRC.
 */
satlink_status_t satlink_app_validate(const uint8_t *image, size_t available, size_t *entry_offset);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_BOOT_APP_HEADER_H */
