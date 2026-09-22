/**
 * @file regdef.h
 * @brief Helpers for the generated C register headers in satlink/regs/.
 *
 * Static inline functions instead of function-like macros (MISRA C:2012 Dir 4.9).
 *
 * @implements SRS-ICD-001
 */
#ifndef SATLINK_REGDEF_H
#define SATLINK_REGDEF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Extract a field from a register value. */
static inline uint32_t satlink_field_get(uint32_t reg, uint32_t mask, uint32_t shift)
{
    return (reg & mask) >> shift;
}

/** Return @p reg with the field replaced by @p value (excess value bits are dropped). */
static inline uint32_t satlink_field_set(uint32_t reg, uint32_t mask, uint32_t shift,
                                         uint32_t value)
{
    return (reg & ~mask) | ((value << shift) & mask);
}

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_REGDEF_H */
