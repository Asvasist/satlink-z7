/**
 * @file types.h
 * @brief Complex sample type of the software modem.
 *
 * @implements SRS-MDM-002
 */
#ifndef SATLINK_MODEM_TYPES_H
#define SATLINK_MODEM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/** Complex baseband sample or symbol, single precision (the Cortex-A9 has VFPv3). */
typedef struct
{
    float re;
    float im;
} satlink_cf_t;

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_MODEM_TYPES_H */
