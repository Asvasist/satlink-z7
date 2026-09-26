/**
 * @file tasks.h
 * @brief Tasks of the Core 1 firmware.
 *
 * | Task     | Priority | Stack  | Role                                                   |
 * |----------|----------|--------|--------------------------------------------------------|
 * | modem    | 5        | 16 KiB | Event loop owning the modem application: IPC, DMA, TX/RX |
 * | monitor  | 2        | 4 KiB  | Heartbeat, CPU load, stack checks, standalone self-test |
 * | Tmr Svc  | 7        | 4 KiB  | FreeRTOS timer service                                 |
 * | IDLE     | 0        | 2 KiB  | WFI; its run time is the CPU load measurement          |
 *
 * The modem task is the only one that touches the modem state, so the modem library needs no
 * locking: interrupts only set notification bits.
 *
 * @implements SRS-AMP-001
 */
#ifndef RTOS_TASKS_H
#define RTOS_TASKS_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#define MODEM_EVT_IPC (1UL << 0U)
#define MODEM_EVT_TX  (1UL << 1U)
#define MODEM_EVT_RX  (1UL << 2U)

/** Create the modem task; returns its handle (for interrupt notifications). */
TaskHandle_t modem_task_create(bool standalone);

/** Create the monitor task. */
void monitor_task_create(bool standalone);

/** CPU load over the last second, in 0.1 % (written by the monitor). */
uint16_t monitor_cpu_load(void);

#endif /* RTOS_TASKS_H */
