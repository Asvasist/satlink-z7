/**
 * @file shm.h
 * @brief Layout of the AMP shared-memory partition (ipc_shm in the ICD, 1 MiB at
 *        0x39000000).
 *
 * | Offset    | Size     | Content                                             |
 * |-----------|----------|-----------------------------------------------------|
 * | 0x00000   | 4 KiB    | Control block (below)                               |
 * | 0x01000   | 256 KiB  | Ring Linux -> RTOS (commands, TX frames)            |
 * | 0x41000   | 256 KiB  | Ring RTOS -> Linux (telemetry, RX frames, logs)     |
 * | 0x81000   | 508 KiB  | Reserved                                            |
 *
 * Linux initialises the control block and both rings before it releases Core 1; the firmware
 * only attaches. Ownership of each control-block word is noted below.
 *
 * @implements SRS-AMP-002
 */
#ifndef SATLINK_AMP_SHM_H
#define SATLINK_AMP_SHM_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define SATLINK_SHM_SIZE            (0x00100000UL)
#define SATLINK_SHM_CTRL_OFFSET     (0x00000000UL)
#define SATLINK_SHM_TO_RTOS_OFFSET  (0x00001000UL)
#define SATLINK_SHM_TO_LINUX_OFFSET (0x00041000UL)
#define SATLINK_SHM_RING_BYTES      (0x00040000UL)

#define SATLINK_SHM_MAGIC   (0x534C4D53UL) /* "SMLS" */
#define SATLINK_SHM_VERSION (1U)

/** Firmware state as published by the firmware. */
#define SATLINK_RTOS_STATE_OFFLINE (0U) /**< Written by Linux before release. */
#define SATLINK_RTOS_STATE_BOOTING (1U) /**< Firmware reached main(). */
#define SATLINK_RTOS_STATE_RUNNING (2U) /**< Scheduler and IPC up. */
#define SATLINK_RTOS_STATE_FAULT   (3U) /**< Firmware caught an abort, assert or stack overflow. */

/** Control block at offset 0. */
typedef struct
{
    uint32_t magic;        /**< Linux: SATLINK_SHM_MAGIC. */
    uint32_t version;      /**< Linux: SATLINK_SHM_VERSION. */
    uint32_t rtos_state;   /**< RTOS: SATLINK_RTOS_STATE_*. */
    uint32_t heartbeat;    /**< RTOS: incremented every 100 ms by the idle monitor. */
    uint32_t fault_code;   /**< RTOS: why it entered FAULT. */
    uint32_t fault_addr;   /**< RTOS: faulting PC or address. */
    uint32_t boot_count;   /**< Linux: incremented on every release of Core 1. */
    uint32_t irq_to_rtos;  /**< Linux: GIC SPI Linux pends to notify the firmware. */
    uint32_t irq_to_linux; /**< Linux: GIC SPI the firmware pends to notify Linux. */
    uint32_t fw_version;   /**< RTOS: major << 16 | minor << 8 | patch. */
    uint32_t reserved[54];
} satlink_shm_ctrl_t;

#endif /* SATLINK_AMP_SHM_H */
