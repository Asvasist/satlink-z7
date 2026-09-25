/**
 * @file modcod.c
 * @implements SRS-MDM-002
 */
#include "satlink/modem/modcod.h"

#include <stddef.h>

static const satlink_modcod_t k_modcods[SATLINK_MODCOD_COUNT] = {
    {0U, SATLINK_MOD_BPSK, SATLINK_CONV_RATE_1_2, "BPSK-1/2", 2.0F, 0.5F},
    {1U, SATLINK_MOD_QPSK, SATLINK_CONV_RATE_1_2, "QPSK-1/2", 5.0F, 1.0F},
    {2U, SATLINK_MOD_QPSK, SATLINK_CONV_RATE_3_4, "QPSK-3/4", 8.0F, 1.5F},
    {3U, SATLINK_MOD_8PSK, SATLINK_CONV_RATE_2_3, "8PSK-2/3", 11.5F, 2.0F},
    {4U, SATLINK_MOD_8PSK, SATLINK_CONV_RATE_5_6, "8PSK-5/6", 15.0F, 2.5F},
};

const satlink_modcod_t *satlink_modcod_get(uint8_t id)
{
    return (id < SATLINK_MODCOD_COUNT) ? &k_modcods[id] : NULL;
}
