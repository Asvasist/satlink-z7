/**
 * @file modcod.h
 * @brief Modulation and coding combinations (MODCODs) of the SatLink physical layer.
 *
 * | ID | Modulation | Code rate | Info bits / symbol | Es/N0 threshold (dB) |
 * |----|------------|-----------|--------------------|----------------------|
 * | 0  | BPSK       | 1/2       | 0.50               | 2.0                  |
 * | 1  | QPSK       | 1/2       | 1.00               | 4.5                  |
 * | 2  | QPSK       | 3/4       | 1.50               | 7.0                  |
 * | 3  | 8PSK       | 2/3       | 2.00               | 10.0                 |
 * | 4  | 8PSK       | 5/6       | 2.50               | 12.5                 |
 *
 * Thresholds are the Es/N0 at which the frame error rate of a 128-byte frame falls below 1 %
 * in the link simulation (tools/perf, with carrier and timing offsets), plus at least 0.4 dB,
 * rounded to 0.5 dB (docs/performance); the ACM controller adds its own margin.
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
