/**
 * @file ipc.h
 * @brief Core 1 side of the AMP link: shared control block and the two message rings.
 *
 * @implements SRS-AMP-002
 */
#ifndef RTOS_IPC_H
#define RTOS_IPC_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/amp/shm.h"
#include "satlink/common/status.h"

#include "FreeRTOS.h"
#include "task.h"

/** Attach to the rings Linux prepared. Without Linux (magic missing: QEMU, bench tests) the
 *  firmware sets them up itself and runs standalone. Returns true when standalone. */
bool ipc_init(void);

/** Route the Linux -> RTOS notification interrupt to @p task (notification bit @p bit). */
void ipc_attach_irq(TaskHandle_t task, uint32_t bit);

/** Publish the firmware state (SATLINK_RTOS_STATE_*). */
void ipc_set_state(uint32_t state);

/** Increment the heartbeat Linux watches. */
void ipc_heartbeat(void);

/** Send one message to Linux and raise its interrupt. Thread-safe. */
satlink_status_t ipc_send(uint16_t type, const uint8_t *payload, uint16_t len);

/** Take one message from Linux: SATLINK_OK, SATLINK_ERR_EMPTY, or SATLINK_ERR_IO. */
satlink_status_t ipc_receive(uint16_t *type, uint8_t *payload, uint16_t *len);

/* Standalone mode only: the firmware plays Linux for its self-test. */
satlink_status_t ipc_standalone_send_to_rtos(uint16_t type, const uint8_t *payload, uint16_t len);
satlink_status_t ipc_standalone_receive_from_rtos(uint16_t *type, uint8_t *payload, uint16_t *len);

#endif /* RTOS_IPC_H */
