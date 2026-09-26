/**
 * @file mmio.h
 * @brief 32-bit memory-mapped register access for the MicroBlaze V peripherals.
 *
 * @implements SRS-HKC-001
 */
#ifndef HKC_MMIO_H
#define HKC_MMIO_H

#include <stdint.h>

static inline uint32_t hkc_read32(uint32_t addr)
{
    return *(volatile const uint32_t *)(uintptr_t)addr;
}

static inline void hkc_write32(uint32_t addr, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

#endif /* HKC_MMIO_H */
