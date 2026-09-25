/**
 * @file prbs.h
 * @brief PN9 (x^9 + x^5 + 1) pseudo-random bit sequence, the payload of idle frames: the
 *        receiver knows it, so every idle frame is a bit error rate measurement.
 *
 * @implements SRS-MDM-004
 */
#ifndef SATLINK_MODEM_PRBS_H
#define SATLINK_MODEM_PRBS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fill @p out with @p len bytes of PN9 (MSB first) starting from the all-ones state. */
void satlink_pn9_fill(uint8_t *out, size_t len);

/** Number of differing bits between @p a and @p b. */
uint32_t satlink_bit_errors(const uint8_t *a, const uint8_t *b, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_PRBS_H */
