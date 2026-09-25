/**
 * @file modcod.h
 * @brief Modulation and coding combinations (MODCODs) of the SatLink physical layer.
 *
 * | ID | Modulation | Code rate | Info bits / symbol | Es/N0 threshold (dB) |
 * |----|------------|-----------|--------------------|----------------------|
 * | 0  | BPSK       | 1/2       | 0.50               | 2.0                  |
 * | 1  | QPSK       | 1/2       | 1.00               | 5.0                  |
 * | 2  | QPSK       | 3/4       | 1.50               | 8.0                  |
 * | 3  | 8PSK       | 2/3       | 2.00               | 11.5                 |
 * | 4  | 8PSK       | 5/6       | 2.50               | 15.0                 |
 *
 * Thresholds are the Es/N0 at which the frame error rate of a 128-byte frame falls below 1 %
 * in the AWGN simulation (tests/perf), rounded up; the ACM controller adds its own margin.
 *
 * @implements SRS-MDM-002
 */
#ifndef SATLINK_MODEM_MODCOD_H
#define SATLINK_MODEM_MODCOD_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/modem/conv.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SATLINK_MOD_BPSK = 1,
    SATLINK_MOD_QPSK = 2,
    SATLINK_MOD_8PSK = 3,
} satlink_modulation_t; /* value = bits per symbol */

#define SATLINK_MODCOD_COUNT (5U)

typedef struct
{
    uint8_t id;
    satlink_modulation_t modulation;
    satlink_conv_rate_t rate;
    const char *name;
    float esn0_threshold_db;
    float info_bits_per_symbol;
} satlink_modcod_t;

/** Entry for @p id, or NULL if @p id >= SATLINK_MODCOD_COUNT. */
const satlink_modcod_t *satlink_modcod_get(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_MODCOD_H */
