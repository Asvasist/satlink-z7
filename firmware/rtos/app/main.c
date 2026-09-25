/**
 * @file main.c
 * @brief Core 1 firmware entry: console, IPC, interrupt controller, tasks, scheduler.
 *
 * @implements SRS-AMP-001
 */
#include "satlink/amp/shm.h"

#include "FreeRTOS.h"
#include "rtos/bsp.h"
#include "rtos/ipc.h"
#include "rtos/tasks.h"
#include "task.h"
#include "version.h"

int main(void);

int main(void)
{
    bsp_uart_init();
    const bool standalone = ipc_init();
    ipc_set_state(SATLINK_RTOS_STATE_BOOTING);
    bsp_printf("\nSatLink-Z7 modem firmware %u.%u.%u on Cortex-A9 core %u%s\n",
               SATLINK_RTOS_VERSION_MAJOR, SATLINK_RTOS_VERSION_MINOR, SATLINK_RTOS_VERSION_PATCH,
               bsp_cpu_id(), standalone ? " (standalone: no Linux, running the self-test)" : "");
    bsp_gic_init();

    const TaskHandle_t modem = modem_task_create(standalone);
    ipc_attach_irq(modem, MODEM_EVT_IPC);
    monitor_task_create(standalone);
    vTaskStartScheduler();
    satlink_fault(BSP_FAULT_ASSERT, 0U); /* the scheduler never returns */
}

void vApplicationIdleHook(void);
void vApplicationIdleHook(void)
{
    __asm__ volatile("wfi");
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                   configSTACK_DEPTH_TYPE *size);
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                   configSTACK_DEPTH_TYPE *size)
{
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *tcb = &idle_tcb;
    *stack = idle_stack;
    *size = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                    configSTACK_DEPTH_TYPE *size);
void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                    configSTACK_DEPTH_TYPE *size)
{
    static StaticTask_t timer_tcb;
    static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];
    *tcb = &timer_tcb;
    *stack = timer_stack;
    *size = configTIMER_TASK_STACK_DEPTH;
}
