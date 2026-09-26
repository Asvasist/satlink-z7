/**
 * @file frame.h
 * @brief SatLink physical-layer frame (a small DVB-S2-style PLFRAME with CCSDS channel coding).
 *
 * ```
 * | ASM 32 sym | header 64 sym | payload (MODCOD), 16 pilot symbols after every 512 |
 *    BPSK         BPSK, rep-4
 * ```
 *
 * - **ASM**: 0x1ACFFC1D (CCSDS attached sync marker) in BPSK. Found by correlation, it gives
 *   frame timing, the carrier phase (including the 180 degree ambiguity) and the amplitude.
 * - **Header**: 16 bits = MODCOD (3) | type (2) | sequence (7) | CRC-4 (4, x^4 + x + 1), each
 *   bit sent four times, interleaved (bit i at positions i, i+16, i+32, i+48), BPSK. The
 *   receiver learns the payload's modulation and length before it arrives, so the MODCOD can
 *   change on every frame (ACM).
 * - **Payload**: 128 information bytes + CRC-16-CCITT, CCSDS-randomized, convolutionally
 *   encoded (K = 7, punctured to the MODCOD's rate), zero-padded to whole symbols, mapped.
 * - **Pilots**: 16 BPSK +1 symbols after every 512 payload symbols, for phase tracking and
 *   SNR estimation at high-order modulations.
 *
 * @implements SRS-MDM-004
 */
#ifndef SATLINK_MODEM_FRAME_H
#define SATLINK_MODEM_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/common/status.h"
#include "satlink/modem/modcod.h"
#include "satlink/modem/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_FRAME_INFO_BYTES   (128U)
#define SATLINK_FRAME_ASM          (0x1ACFFC1DUL)
#define SATLINK_FRAME_ASM_SYMBOLS  (32U)
#define SATLINK_FRAME_HDR_BITS     (16U)
#define SATLINK_FRAME_HDR_REPEAT   (4U)
#define SATLINK_FRAME_HDR_SYMBOLS  (SATLINK_FRAME_HDR_BITS * SATLINK_FRAME_HDR_REPEAT)
#define SATLINK_FRAME_PILOT_PERIOD (512U)
#define SATLINK_FRAME_PILOT_LEN    (16U)
/** Worst case (BPSK 1/2): 96 + 2092 payload + 4 pilot blocks. */
#define SATLINK_FRAME_MAX_SYMBOLS         (2304U)
#define SATLINK_FRAME_MAX_PAYLOAD_SYMBOLS (2092U)

/** Frame types (header field). */
typedef enum
{
    SATLINK_FRAME_DATA = 0, /**< Payload from the upper layer. */
    SATLINK_FRAME_IDLE = 1, /**< PN9 payload: sent when there is no data; measures BER. */
} satlink_frame_type_t;

typedef struct
{
    uint8_t modcod;
    uint8_t type;
    uint8_t seq; /**< 0..127 */
} satlink_frame_header_t;

/** Encode the 16 header bits (CRC-4 included). */
uint16_t satlink_frame_header_encode(const satlink_frame_header_t *header);

/** Decode 16 header bits; false if the CRC-4 fails or the MODCOD is unknown. */
bool satlink_frame_header_decode(uint16_t bits, satlink_frame_header_t *header);

/** Payload symbols (data only, without pilots) for @p modcod; 0 for an unknown MODCOD. */
size_t satlink_frame_payload_symbols(uint8_t modcod);

/** Total frame length in symbols (ASM + header + payload + pilots); 0 if unknown. */
size_t satlink_frame_symbols(uint8_t modcod);

/**
 * @brief Build one frame.
 *
 * @param info     SATLINK_FRAME_INFO_BYTES bytes (ignored for IDLE frames, which carry PN9).
 * @param symbols  Output, at least satlink_frame_symbols(header->modcod) entries.
 * @param count    Number of symbols written.
 */
satlink_status_t satlink_frame_build(const satlink_frame_header_t *header, const uint8_t *info,
                                     satlink_cf_t *symbols, size_t max_symbols, size_t *count);

/** The ASM as BPSK symbols (+1 / -1), SATLINK_FRAME_ASM_SYMBOLS entries. */
const float *satlink_frame_asm_symbols(void);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_FRAME_H */
