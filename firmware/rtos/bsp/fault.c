/**
 * @file fault.c
 * @brief Fatal error path of the Core 1 firmware: record, report, stop.
 *
 * The fault is published in the shared control block (state FAULT, code, address), so the
 * satlink_amp driver on Linux sees it at once and can restart Core 1 (FDIR). Core 1 then waits
 * with interrupts off; it never tries to continue after a fault.
 *
 * @implements SRS-AMP-005
 */
#include "FreeRTOS.h"
#include "rtos/bsp.h"
#include "task.h"

static void (*g_fault_hook)(uint32_t code, uint32_t addr);

void bsp_set_fault_hook(void (*hook)(uint32_t code, uint32_t addr))
{
    g_fault_hook = hook;
}

void satlink_fault(uint32_t code, uint32_t addr)
{
    __asm__ volatile("cpsid if" ::: "memory");
    uint32_t detail = addr;
    if (code == BSP_FAULT_DATA_ABORT)
    {
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(detail)); /* DFAR */
    }
    if (g_fault_hook != NULL)
    {
        g_fault_hook(code, detail);
    }
    bsp_printf("\nrtos: FAULT code %u at 0x%08x (pc 0x%08x)\n", code, detail, addr);
    for (;;)
    {
        __asm__ volatile("wfi");
    }
}

void satlink_rtos_assert(const char *file, int line)
{
    bsp_printf("\nrtos: assertion failed %s:%d\n", file, line);
    satlink_fault(BSP_FAULT_ASSERT, (uint32_t)line);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name);
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    bsp_printf("\nrtos: stack overflow in %s\n", name);
    satlink_fault(BSP_FAULT_STACK_OVERFLOW, 0U);
}

void vApplicationMallocFailedHook(void);
void vApplicationMallocFailedHook(void)
{
    satlink_fault(BSP_FAULT_MALLOC, 0U);
}
