/**
 * @file cpu.h
 * @brief RISC-V machine-mode helpers for the MicroBlaze V.
 *
 * @implements SRS-HKC-001
 */
#ifndef HKC_CPU_H
#define HKC_CPU_H

#include <stdint.h>

#define HKC_MCAUSE_INTERRUPT (0x80000000UL)
#define HKC_MCAUSE_MEI       (11UL) /**< Machine external interrupt (AXI INTC output). */

static inline void hkc_cpu_enable_external_irq(void)
{
    __asm__ volatile("csrs mie, %0" ::"r"(1UL << 11U));
    __asm__ volatile("csrsi mstatus, 0x8");
}

static inline void hkc_cpu_disable_irq(void)
{
    __asm__ volatile("csrci mstatus, 0x8");
}

static inline void hkc_cpu_wait_for_interrupt(void)
{
    __asm__ volatile("wfi");
}

/** Jump to @p entry with interrupts off, after making fresh code visible to the fetch unit. */
static inline __attribute__((noreturn)) void hkc_cpu_jump(uint32_t entry)
{
    hkc_cpu_disable_irq();
    __asm__ volatile("csrw mie, zero");
    __asm__ volatile("fence.i" ::: "memory");
    void (*const fn)(void) = (void (*)(void))(uintptr_t)entry;
    fn();
    __builtin_unreachable();
}

/** Handler called from crt0.S for every trap. Defined by each image. */
void hkc_trap_handler(uint32_t mcause, uint32_t mepc);

#endif /* HKC_CPU_H */
