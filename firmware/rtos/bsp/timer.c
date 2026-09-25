/**
 * @file timer.c
 * @brief FreeRTOS tick from the Core 1 private timer, time base from the global timer.
 *
 * @implements SRS-AMP-001
 */
#include "FreeRTOS.h"
#include "rtos/bsp.h"
#include "task.h"

#define PTIMER_LOAD (SATLINK_PS_MPCORE_BASE + 0x600U)
#define PTIMER_CTRL (SATLINK_PS_MPCORE_BASE + 0x608U)
#define PTIMER_ISR  (SATLINK_PS_MPCORE_BASE + 0x60CU)
#define GTIMER_LO   (SATLINK_PS_MPCORE_BASE + 0x200U)
#define GTIMER_HI   (SATLINK_PS_MPCORE_BASE + 0x204U)
#define GTIMER_CTRL (SATLINK_PS_MPCORE_BASE + 0x208U)
#define PTIMER_IRQ  (SATLINK_PS_MPCORE_IRQ_PTIMER)

#define TICKS_PER_US (configPERIPH_CLOCK_HZ / 1000000UL)

extern void FreeRTOS_Tick_Handler(void);

static void tick_isr(void *ctx)
{
    (void)ctx;
    FreeRTOS_Tick_Handler();
}

void satlink_rtos_setup_tick(void)
{
    /* The global timer is Linux' clocksource on the board; start it only if it is stopped. */
    if ((bsp_read32(GTIMER_CTRL) & 1U) == 0U)
    {
        bsp_write32(GTIMER_CTRL, 1U);
    }
    bsp_write32(PTIMER_CTRL, 0U);
    bsp_write32(PTIMER_ISR, 1U);
    bsp_write32(PTIMER_LOAD, (uint32_t)(configPERIPH_CLOCK_HZ / configTICK_RATE_HZ) - 1U);
    bsp_gic_attach(PTIMER_IRQ, 30U, true, &tick_isr, NULL);
    bsp_gic_enable(PTIMER_IRQ);
    bsp_write32(PTIMER_CTRL, 0x7U); /* enable, auto-reload, IRQ */
}

void satlink_rtos_clear_tick(void)
{
    bsp_write32(PTIMER_ISR, 1U);
}

uint64_t bsp_time_ticks(void)
{
    uint32_t hi = 0U;
    uint32_t lo = 0U;
    uint32_t hi2 = 0U;
    do
    {
        hi = bsp_read32(GTIMER_HI);
        lo = bsp_read32(GTIMER_LO);
        hi2 = bsp_read32(GTIMER_HI);
    } while (hi != hi2);
    return ((uint64_t)hi << 32U) | (uint64_t)lo;
}

uint32_t bsp_ticks_to_us(uint64_t ticks)
{
    return (uint32_t)(ticks / TICKS_PER_US);
}

uint32_t satlink_rtos_time_us(void)
{
    return bsp_ticks_to_us(bsp_time_ticks());
}
