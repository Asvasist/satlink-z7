/**
 * @file byte_order.h
 * @brief Endian-independent load/store helpers for protocol fields.
 *
 * CAN housekeeping and RPMsg messages are little-endian, CCSDS packets are big-endian. These
 * helpers work byte by byte, so they are correct on any host and never do unaligned accesses.
 *
 * @implements SRS-LIB-003
 */
#ifndef SATLINK_COMMON_BYTE_ORDER_H
#define SATLINK_COMMON_BYTE_ORDER_H

#include <stdint.h>

/* The header is included from C and C++; each language gets its own cast syntax. */
#ifdef __cplusplus
#define SATLINK_BO_CAST(type, value) static_cast<type>(value)
extern "C" {
#else
#define SATLINK_BO_CAST(type, value) ((type)(value))
#endif

static inline void satlink_put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = SATLINK_BO_CAST(uint8_t, value & 0xFFU);
    dst[1] = SATLINK_BO_CAST(uint8_t, SATLINK_BO_CAST(uint32_t, value) >> 8U);
}

static inline void satlink_put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = SATLINK_BO_CAST(uint8_t, value & 0xFFU);
    dst[1] = SATLINK_BO_CAST(uint8_t, (value >> 8U) & 0xFFU);
    dst[2] = SATLINK_BO_CAST(uint8_t, (value >> 16U) & 0xFFU);
    dst[3] = SATLINK_BO_CAST(uint8_t, value >> 24U);
}

static inline uint16_t satlink_get_le16(const uint8_t *src)
{
    return SATLINK_BO_CAST(uint16_t, SATLINK_BO_CAST(uint32_t, src[0]) |
                                         (SATLINK_BO_CAST(uint32_t, src[1]) << 8U));
}

static inline uint32_t satlink_get_le32(const uint8_t *src)
{
    return SATLINK_BO_CAST(uint32_t, src[0]) | (SATLINK_BO_CAST(uint32_t, src[1]) << 8U) |
           (SATLINK_BO_CAST(uint32_t, src[2]) << 16U) | (SATLINK_BO_CAST(uint32_t, src[3]) << 24U);
}

static inline void satlink_put_be16(uint8_t *dst, uint16_t value)
{
    dst[0] = SATLINK_BO_CAST(uint8_t, SATLINK_BO_CAST(uint32_t, value) >> 8U);
    dst[1] = SATLINK_BO_CAST(uint8_t, value & 0xFFU);
}

static inline void satlink_put_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = SATLINK_BO_CAST(uint8_t, value >> 24U);
    dst[1] = SATLINK_BO_CAST(uint8_t, (value >> 16U) & 0xFFU);
    dst[2] = SATLINK_BO_CAST(uint8_t, (value >> 8U) & 0xFFU);
    dst[3] = SATLINK_BO_CAST(uint8_t, value & 0xFFU);
}

static inline uint16_t satlink_get_be16(const uint8_t *src)
{
    return SATLINK_BO_CAST(uint16_t, (SATLINK_BO_CAST(uint32_t, src[0]) << 8U) |
                                         SATLINK_BO_CAST(uint32_t, src[1]));
}

static inline uint32_t satlink_get_be32(const uint8_t *src)
{
    return (SATLINK_BO_CAST(uint32_t, src[0]) << 24U) | (SATLINK_BO_CAST(uint32_t, src[1]) << 16U) |
           (SATLINK_BO_CAST(uint32_t, src[2]) << 8U) | SATLINK_BO_CAST(uint32_t, src[3]);
}

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_COMMON_BYTE_ORDER_H */
