/**
 * @file crc32.h
 * @brief CRC-32 (IEEE 802.3, reflected, init and final XOR 0xFFFFFFFF), used to check firmware
 *        images before they are started.
 *
 * Check value for "123456789" is 0xCBF43926. Same parameters as zlib's crc32() and Python's
 * zlib.crc32(), so host tools can compute image checksums without any SatLink code.
 *
 * @implements SRS-LIB-004
 */
#ifndef SATLINK_COMMON_CRC32_H
#define SATLINK_COMMON_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Value to start an incremental computation with (see satlink_crc32_update()). */
#define SATLINK_CRC32_INIT (0x00000000UL)

/**
 * @brief Continue a CRC-32 computation.
 *
 * The pre- and post-inversion are handled inside, so chaining calls with the previous return
 * value gives the same result as one call over the concatenated data.
 *
 * @param crc  CRC of the data so far (::SATLINK_CRC32_INIT for none).
 * @param data Bytes to process. May be NULL only if @p len is 0.
 * @param len  Number of bytes.
 * @return CRC of the data so far plus @p data. If @p data is NULL, @p crc is returned unchanged.
 */
uint32_t satlink_crc32_update(uint32_t crc, const uint8_t *data, size_t len);

/** @brief CRC-32 of a complete buffer (0 for an empty buffer). */
uint32_t satlink_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_COMMON_CRC32_H */
