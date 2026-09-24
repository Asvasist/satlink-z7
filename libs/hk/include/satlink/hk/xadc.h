/**
 * @file xadc.h
 * @brief Conversion of XADC data registers to physical units.
 *
 * The XADC reports a 12-bit result left-justified in a 16-bit register (UG480):
 * temperature = code * 503.975 / 4096 - 273.15 degC, supplies = code / 4096 * 3 V. Integer
 * arithmetic only, so it runs on the MicroBlaze V without a floating point unit.
 *
 * @implements SRS-HKC-001
 */
#ifndef SATLINK_HK_XADC_H
#define SATLINK_HK_XADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Die temperature in millidegrees Celsius from the XADC TEMP register. */
int32_t satlink_xadc_temp_mdegc(uint16_t raw);

/** VCCINT, VCCAUX or VCCBRAM in millivolts from the matching XADC register. */
uint16_t satlink_xadc_supply_mv(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_HK_XADC_H */
