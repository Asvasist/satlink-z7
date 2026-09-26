/**
 * @file axi_dma.h
 * @brief AMD AXI DMA (PG021) in scatter-gather mode: a ring of buffer descriptors per channel.
 *
 * Used for the modem streams (axi_dma_modem): MM2S carries TX symbols to the PL, S2MM brings
 * RX baseband samples. Every descriptor stays linked in a circle; the driver hands a completed
 * buffer to the application, which refills (MM2S) or consumes (S2MM) it and re-queues it by
 * moving TAILDESC. Descriptors and buffers live in the non-cacheable modem_dma partition, so
 * no cache maintenance is needed.
 *
 * Register access is injected, so the ring logic runs in host tests against an engine model.
 *
 * @implements SRS-AMP-006
 */
#ifndef RTOS_AXI_DMA_H
#define RTOS_AXI_DMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register offsets (PG021, SG mode). */
#define AXIDMA_MM2S_DMACR    (0x00U)
#define AXIDMA_MM2S_DMASR    (0x04U)
#define AXIDMA_MM2S_CURDESC  (0x08U)
#define AXIDMA_MM2S_TAILDESC (0x10U)
#define AXIDMA_S2MM_DMACR    (0x30U)
#define AXIDMA_S2MM_DMASR    (0x34U)
#define AXIDMA_S2MM_CURDESC  (0x38U)
#define AXIDMA_S2MM_TAILDESC (0x40U)

#define AXIDMA_CR_RS        (1UL << 0U)
#define AXIDMA_CR_RESET     (1UL << 2U)
#define AXIDMA_CR_IOC_IRQ   (1UL << 12U)
#define AXIDMA_CR_ERR_IRQ   (1UL << 14U)
#define AXIDMA_CR_THRESH(n) ((uint32_t)(n) << 16U)
#define AXIDMA_SR_HALTED    (1UL << 0U)
#define AXIDMA_SR_IDLE      (1UL << 1U)
#define AXIDMA_SR_ERR_MASK  (0x770UL) /* internal, slave, decode errors (SG and data) */
#define AXIDMA_SR_IOC_IRQ   (1UL << 12U)
#define AXIDMA_SR_ERR_IRQ   (1UL << 14U)

#define AXIDMA_BD_CTRL_LEN_MASK (0x03FFFFFFUL)
#define AXIDMA_BD_CTRL_EOF      (1UL << 26U)
#define AXIDMA_BD_CTRL_SOF      (1UL << 27U)
#define AXIDMA_BD_STS_CMPLT     (1UL << 31U)
#define AXIDMA_BD_STS_ERR_MASK  (0x70000000UL)
#define AXIDMA_BD_STS_LEN_MASK  (0x03FFFFFFUL)

/** Buffer descriptor (64-byte aligned in memory, as the engine requires). */
typedef struct
{
    uint32_t nxtdesc;
    uint32_t nxtdesc_msb;
    uint32_t buffer;
    uint32_t buffer_msb;
    uint32_t reserved[2];
    uint32_t control;
    uint32_t status;
    uint32_t app[5];
    uint32_t pad[3];
} axidma_bd_t;

typedef struct
{
    void *ctx;
    uint32_t (*read)(void *ctx, uint32_t offset);
    void (*write)(void *ctx, uint32_t offset, uint32_t value);
} axidma_regs_t;

typedef struct
{
    axidma_regs_t regs;
    bool s2mm;
    volatile axidma_bd_t *bds;
    uint32_t bds_phys;
    uint32_t count;
    uint8_t *buffers;
    uint32_t buffers_phys;
    uint32_t buf_bytes;
    uint32_t next; /**< Oldest descriptor not yet handed to the application. */
    uint32_t errors;
    uint32_t restarts; /**< Times the channel had gone idle (underrun / overrun) */
} axidma_ring_t;

/** Reset the whole engine (both channels). Returns false if the reset does not complete. */
bool axidma_reset(const axidma_regs_t *regs);

/**
 * @brief Link @p count descriptors in a circle over @p count buffers of @p buf_bytes each.
 * @p bds and @p buffers are the CPU addresses, @p bds_phys / @p buffers_phys what the engine
 * sees (identical on Core 1, different in host tests).
 */
void axidma_ring_init(axidma_ring_t *ring, const axidma_regs_t *regs, bool s2mm,
                      volatile axidma_bd_t *bds, uint32_t bds_phys, uint32_t count,
                      uint8_t *buffers, uint32_t buffers_phys, uint32_t buf_bytes);

/** CPU address of buffer @p index (to prefill MM2S buffers before start). */
uint8_t *axidma_ring_buffer(const axidma_ring_t *ring, uint32_t index);

/** Queue every descriptor and start the channel with an interrupt per completed buffer. */
void axidma_ring_start(axidma_ring_t *ring);

/** Read and clear the channel's interrupt flags; counts errors. Returns the status bits. */
uint32_t axidma_ring_ack(axidma_ring_t *ring);

/** Oldest completed buffer, or NULL. @p len gets the bytes transferred (S2MM) or queued. */
uint8_t *axidma_ring_peek_done(const axidma_ring_t *ring, uint32_t *len);

/** Give the buffer returned by axidma_ring_peek_done() back to the engine. */
void axidma_ring_requeue(axidma_ring_t *ring);

/** If the channel went idle because it caught up with TAILDESC, count it (underrun/overrun). */
bool axidma_ring_check_idle(axidma_ring_t *ring);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_AXI_DMA_H */
