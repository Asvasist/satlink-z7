/**
 * @file conv.h
 * @brief CCSDS convolutional code (K = 7, rate 1/2, CCSDS 131.0-B section 3) with the CCSDS
 *        puncturing patterns and a soft-decision Viterbi decoder.
 *
 * Generators G1 = 171 (octal), G2 = 133 (octal); the G2 output is inverted as CCSDS requires.
 * Every block is terminated with six zero tail bits, so the decoder starts and ends in state 0.
 *
 * Soft values are signed 8-bit log-likelihood ratios: positive means "bit 0 more likely",
 * 0 means "no information" (erasure; used for punctured positions).
 *
 * @implements SRS-MDM-001
 */
#ifndef SATLINK_MODEM_CONV_H
#define SATLINK_MODEM_CONV_H

#include <stddef.h>
#include <stdint.h>

#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_CONV_K         (7U)
#define SATLINK_CONV_TAIL_BITS (6U)
#define SATLINK_CONV_STATES    (64U)

/** Largest information block the decoder accepts, in bits (without tail). */
#define SATLINK_CONV_MAX_INFO_BITS (2048U)

/** Code rates (mother rate 1/2 plus the CCSDS punctured rates). */
typedef enum
{
    SATLINK_CONV_RATE_1_2 = 0,
    SATLINK_CONV_RATE_2_3 = 1,
    SATLINK_CONV_RATE_3_4 = 2,
    SATLINK_CONV_RATE_5_6 = 3,
    SATLINK_CONV_RATE_7_8 = 4,
    SATLINK_CONV_RATE_COUNT
} satlink_conv_rate_t;

/**
 * @brief Number of coded bits for @p info_bits information bits (tail included) at @p rate.
 * Returns 0 for an invalid rate.
 */
size_t satlink_conv_coded_bits(size_t info_bits, satlink_conv_rate_t rate);

/**
 * @brief Encode @p info_bits bits from @p in (MSB first) and append the tail.
 *
 * @param out   One coded bit per byte (0 or 1), satlink_conv_coded_bits() entries.
 * @param out_len Capacity of @p out.
 * @return SATLINK_ERR_RANGE if @p out is too small or the rate is invalid.
 */
satlink_status_t satlink_conv_encode(const uint8_t *in, size_t info_bits, satlink_conv_rate_t rate,
                                     uint8_t *out, size_t out_len);

/**
 * @brief Decode a terminated block.
 *
 * @param soft     satlink_conv_coded_bits(info_bits, rate) soft values (see file comment).
 * @param out      Decoded bits packed MSB first ((info_bits + 7) / 8 bytes).
 * Not reentrant: the decision memory is static (one decoder per image; the RX task owns it).
 *
 * @param metric   Optional: final path metric difference to the best competitor (a rough
 *                 reliability indicator; larger is better). May be NULL.
 */
satlink_status_t satlink_conv_decode(const int8_t *soft, size_t soft_len, size_t info_bits,
                                     satlink_conv_rate_t rate, uint8_t *out, uint32_t *metric);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_CONV_H */
