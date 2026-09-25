/**
 * @file FreeRTOSConfig.h
 * @brief FreeRTOS configuration for the modem firmware on Cortex-A9 Core 1 (GCC/ARM_CA9 port).
 *
 * @implements SRS-AMP-001
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* Core 1 of the Zynq-7020 at 667 MHz; private and global timers run at CPU / 2. */
#define configCPU_CLOCK_HZ                    (666666687UL)
#define configPERIPH_CLOCK_HZ                 (333333343UL)
#define configTICK_RATE_HZ                    (1000U)
#define configUSE_PREEMPTION                  1
#define configUSE_TIME_SLICING                1
#define configMAX_PRIORITIES                  (8U)
#define configMINIMAL_STACK_SIZE              (512U) /* words */
#define configMAX_TASK_NAME_LEN               (12U)
#define configTICK_TYPE_WIDTH_IN_BITS         TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD               1
#define configUSE_TASK_NOTIFICATIONS          1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configUSE_MUTEXES                     1
#define configUSE_RECURSIVE_MUTEXES           0
#define configUSE_COUNTING_SEMAPHORES         1
#define configQUEUE_REGISTRY_SIZE             0
#define configUSE_QUEUE_SETS                  0
#define configUSE_NEWLIB_REENTRANT            0
#define configENABLE_BACKWARD_COMPATIBILITY   0
#define configSTACK_DEPTH_TYPE                uint32_t

/* Memory: everything large is allocated statically; the heap serves FreeRTOS objects only. */
#define configSUPPORT_STATIC_ALLOCATION  1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configTOTAL_HEAP_SIZE            (256U * 1024U)
#define configAPPLICATION_ALLOCATED_HEAP 0

/* Hooks and checks (fault detection feeds the FDIR state in shared memory). */
#define configUSE_IDLE_HOOK                1
#define configUSE_TICK_HOOK                0
#define configCHECK_FOR_STACK_OVERFLOW     2
#define configUSE_MALLOC_FAILED_HOOK       1
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

/* Run-time statistics from the 64-bit global timer (CPU / 2), divided to 1 MHz. */
#define configGENERATE_RUN_TIME_STATS        1
#define configUSE_TRACE_FACILITY             1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configRUN_TIME_COUNTER_TYPE          uint32_t
uint32_t satlink_rtos_time_us(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE() satlink_rtos_time_us()

/* Software timers. */
#define configUSE_TIMERS             1
#define configTIMER_TASK_PRIORITY    (configMAX_PRIORITIES - 1U)
#define configTIMER_QUEUE_LENGTH     (8U)
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2U)

/* GIC (PL390) of the Cortex-A9 MPCore: distributor at 0xF8F01000, CPU interface at 0xF8F00100,
 * 32 priority levels. API calls allowed from interrupts at priority 18 and below. */
#define configINTERRUPT_CONTROLLER_BASE_ADDRESS         (0xF8F01000UL)
#define configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET (-0xF00L)
#define configUNIQUE_INTERRUPT_PRIORITIES               (32U)
#define configMAX_API_CALL_INTERRUPT_PRIORITY           (18U)

/* Tick from the Core 1 private timer (PPI 29). */
void satlink_rtos_setup_tick(void);
void satlink_rtos_clear_tick(void);
#define configSETUP_TICK_INTERRUPT() satlink_rtos_setup_tick()
#define configCLEAR_TICK_INTERRUPT() satlink_rtos_clear_tick()

/* Every task gets an FPU context: the modem uses single-precision floating point throughout. */
#define configUSE_TASK_FPU_SUPPORT 2

void satlink_rtos_assert(const char *file, int line);
#define configASSERT(x)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if ((x) == 0)                                                                              \
        {                                                                                          \
            satlink_rtos_assert(__FILE__, __LINE__);                                               \
        }                                                                                          \
    } while (0)

#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskDelete                 0
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_xTaskDelayUntil             1
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_xTaskGetIdleTaskHandle      1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xTaskGetCurrentTaskHandle   1

#endif /* FREERTOS_CONFIG_H */
