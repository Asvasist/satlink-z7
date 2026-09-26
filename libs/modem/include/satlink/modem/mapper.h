/**
 * @file mapper.h
 * @brief Gray-coded BPSK / QPSK / 8PSK mapping and max-log soft demapping (unit energy).
 *
 * - BPSK: bit 0 -> +1, bit 1 -> -1.
 * - QPSK: first bit sets the sign of I, second the sign of Q (0 -> +), amplitude 1/sqrt(2).
 * - 8PSK: point m sits at angle m * 45 degrees and carries the Gray label m ^ (m >> 1), first
 *   bit most significant.
 *
 * Soft outputs follow conv.h: signed 8-bit, positive = bit 0 more likely.
 *
 * @implements SRS-MDM-002
 */
#ifndef SATLINK_MODEM_MAPPER_H
#define SATLINK_MODEM_MAPPER_H

#include <stddef.h>
#include <stdint.h>

#include "satlink/common/status.h"
#include "satlink/modem/modcod.h"
#include "satlink/modem/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Map @p bits (one per byte, 0/1; count a multiple of the bits per symbol) to symbols. */
satlink_status_t satlink_map(satlink_modulation_t mod, const uint8_t *bits, size_t nbits,
                             satlink_cf_t *symbols, size_t max_symbols);

/**
 * @brief Soft-demap @p count symbols.
 *
 * @param scale  LLR scale: soft = clamp(scale * max-log LLR). Choose about 4 / N0 so that
 *               typical values use the int8 range; the Viterbi decoder is insensitive to the
 *               overall scale as long as nothing saturates constantly.
 * @param soft   count * bits-per-symbol outputs.
 */
satlink_status_t satlink_demap(satlink_modulation_t mod, const satlink_cf_t *symbols, size_t count,
                               float scale, int8_t *soft, size_t max_soft);

/** Hard decision: nearest constellation point to @p y. */
satlink_cf_t satlink_slice(satlink_modulation_t mod, satlink_cf_t y);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_MAPPER_H */
