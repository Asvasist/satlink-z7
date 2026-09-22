/**
 * @file ccsds_randomizer.h
 * @brief CCSDS pseudo-randomizer (CCSDS 131.0-B, TM Synchronization and Channel Coding).
 *
 * Generator polynomial h(x) = x^8 + x^7 + x^5 + x^3 + 1, register seeded with all ones at
 * the start of every frame. The sequence has a period of 255 bits and starts with
 * 0xFF 0x48 0x0E 0xC0 0x9A 0x0D 0x70 0xBC. Randomizing is an XOR with this sequence,
 * so applying it twice restores the original data.
 *
 * This implementation is the bit-exact reference for the randomizer in the
 * ccsds_frame_accel PL block (see docs/icd).
 *
 * @implements SRS-LIB-002
 */
#ifndef SATLINK_COMMON_CCSDS_RANDOMIZER_H
#define SATLINK_COMMON_CCSDS_RANDOMIZER_H

#include <stddef.h>
#include <stdint.h>

#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** LFSR state at the start of every frame. */
#define SATLINK_CCSDS_RANDOMIZER_SEED (0xFFU)

/** Period of the randomizer sequence in bytes (255 bits * 8 / 8). */
#define SATLINK_CCSDS_RANDOMIZER_PERIOD_BYTES (255U)

/** Randomizer state. Treat as opaque; use the functions below. */
typedef struct
{
    uint8_t lfsr; /**< 8-bit LFSR, bit 7 is the next output bit. */
} satlink_ccsds_randomizer_t;

/**
 * @brief Reset the randomizer to the start of a frame.
 * @param ctx Randomizer state. Ignored if NULL.
 */
void satlink_ccsds_randomizer_reset(satlink_ccsds_randomizer_t *ctx);

/**
 * @brief Return the next byte of the randomizer sequence and advance the state.
 * @param ctx Randomizer state. Must not be NULL (returns 0 if it is).
 * @return Next sequence byte, most significant bit first.
 */
uint8_t satlink_ccsds_randomizer_next(satlink_ccsds_randomizer_t *ctx);

/**
 * @brief XOR @p len bytes with the sequence, continuing from the current state.
 *
 * @param ctx  Randomizer state.
 * @param data Buffer to (de)randomize in place. May be NULL only if @p len is 0.
 * @param len  Number of bytes.
 * @return ::SATLINK_OK, or ::SATLINK_ERR_NULL for a NULL @p ctx or NULL @p data with len > 0.
 */
satlink_status_t satlink_ccsds_randomizer_apply(satlink_ccsds_randomizer_t *ctx, uint8_t *data,
                                                size_t len);

/**
 * @brief (De)randomize one complete frame in place (reset + apply).
 *
 * @param frame Frame buffer. May be NULL only if @p len is 0.
 * @param len   Frame length in bytes.
 * @return ::SATLINK_OK or ::SATLINK_ERR_NULL.
 */
satlink_status_t satlink_ccsds_randomize_frame(uint8_t *frame, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_COMMON_CCSDS_RANDOMIZER_H */
