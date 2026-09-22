/**
 * @file crc16_ccitt.h
 * @brief CRC-16-CCITT as used for the CCSDS Frame Error Control Field (FECF).
 *
 * Parameters (a.k.a. CRC-16/CCITT-FALSE): polynomial 0x1021, initial value 0xFFFF,
 * no input/output reflection, no final XOR. Check value for "123456789" is 0x29B1.
 * Appending the CRC big-endian to the protected data yields a residue of 0x0000.
 *
 * This implementation is the bit-exact reference for the CRC engine in the
 * ccsds_frame_accel PL block (see docs/icd).
 *
 * @implements SRS-LIB-001
 */
#ifndef SATLINK_COMMON_CRC16_CCITT_H
#define SATLINK_COMMON_CRC16_CCITT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initial value of the CCSDS CRC-16. */
#define SATLINK_CRC16_CCITT_INIT (0xFFFFU)

/**
 * @brief Continue a CRC-16-CCITT computation over @p len bytes.
 *
 * @param crc  Running CRC value (start with ::SATLINK_CRC16_CCITT_INIT).
 * @param data Bytes to process. May be NULL only if @p len is 0.
 * @param len  Number of bytes.
 * @return Updated CRC. If @p data is NULL, @p crc is returned unchanged.
 */
uint16_t satlink_crc16_ccitt_update(uint16_t crc, const uint8_t *data, size_t len);

/**
 * @brief Compute the CRC-16-CCITT of a complete buffer.
 *
 * @param data Bytes to process. May be NULL only if @p len is 0.
 * @param len  Number of bytes.
 * @return CRC of the buffer (0xFFFF for an empty buffer).
 */
uint16_t satlink_crc16_ccitt(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_COMMON_CRC16_CCITT_H */
