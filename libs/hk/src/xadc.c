/**
 * @file xadc.c
 * @implements SRS-HKC-001
 */
#include "satlink/hk/xadc.h"

#define XADC_CODE_SHIFT           (4U)
#define XADC_FULL_SCALE           (4096U)
#define XADC_TEMP_SLOPE_UDEG      (503975U) /* 503.975 degC per full scale, in millidegrees */
#define XADC_TEMP_OFFSET_MDEGC    (273150)
#define XADC_SUPPLY_FULL_SCALE_MV (3000U)

int32_t satlink_xadc_temp_mdegc(uint16_t raw)
{
    const uint32_t code = (uint32_t)raw >> XADC_CODE_SHIFT;
    /* At most 4095 * 503975 = 2 063 777 625, which fits in 32 bits. */
    const uint32_t scaled =
        ((code * XADC_TEMP_SLOPE_UDEG) + (XADC_FULL_SCALE / 2U)) / XADC_FULL_SCALE;

    return (int32_t)scaled - XADC_TEMP_OFFSET_MDEGC;
}

uint16_t satlink_xadc_supply_mv(uint16_t raw)
{
    const uint32_t code = (uint32_t)raw >> XADC_CODE_SHIFT;

    return (uint16_t)(((code * XADC_SUPPLY_FULL_SCALE_MV) + (XADC_FULL_SCALE / 2U)) /
                      XADC_FULL_SCALE);
}
