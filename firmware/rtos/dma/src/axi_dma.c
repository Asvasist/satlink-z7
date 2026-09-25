/**
 * @file axi_dma.c
 * @implements SRS-AMP-006
 */
#include "rtos/axi_dma.h"

#define RESET_POLLS (100000U)

static uint32_t reg_cr(const axidma_ring_t *r)
{
    return r->s2mm ? AXIDMA_S2MM_DMACR : AXIDMA_MM2S_DMACR;
}
static uint32_t reg_sr(const axidma_ring_t *r)
{
    return r->s2mm ? AXIDMA_S2MM_DMASR : AXIDMA_MM2S_DMASR;
}
static uint32_t reg_cur(const axidma_ring_t *r)
{
    return r->s2mm ? AXIDMA_S2MM_CURDESC : AXIDMA_MM2S_CURDESC;
}
static uint32_t reg_tail(const axidma_ring_t *r)
{
    return r->s2mm ? AXIDMA_S2MM_TAILDESC : AXIDMA_MM2S_TAILDESC;
}

static uint32_t bd_phys(const axidma_ring_t *r, uint32_t i)
{
    return r->bds_phys + (i * (uint32_t)sizeof(axidma_bd_t));
}

static void memory_barrier(void)
{
#if defined(__arm__)
    __asm__ volatile("dsb" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

bool axidma_reset(const axidma_regs_t *regs)
{
    regs->write(regs->ctx, AXIDMA_MM2S_DMACR, AXIDMA_CR_RESET);
    for (uint32_t i = 0U; i < RESET_POLLS; ++i)
    {
        if ((regs->read(regs->ctx, AXIDMA_MM2S_DMACR) & AXIDMA_CR_RESET) == 0U)
        {
            return true;
        }
    }
    return false;
}

void axidma_ring_init(axidma_ring_t *ring, const axidma_regs_t *regs, bool s2mm,
                      volatile axidma_bd_t *bds, uint32_t bds_phys, uint32_t count,
                      uint8_t *buffers, uint32_t buffers_phys, uint32_t buf_bytes)
{
    ring->regs = *regs;
    ring->s2mm = s2mm;
    ring->bds = bds;
    ring->bds_phys = bds_phys;
    ring->count = count;
    ring->buffers = buffers;
    ring->buffers_phys = buffers_phys;
    ring->buf_bytes = buf_bytes;
    ring->next = 0U;
    ring->errors = 0U;
    ring->restarts = 0U;
    for (uint32_t i = 0U; i < count; ++i)
    {
        volatile axidma_bd_t *bd = &bds[i];
        bd->nxtdesc = bd_phys(ring, (i + 1U) % count);
        bd->nxtdesc_msb = 0U;
        bd->buffer = buffers_phys + (i * buf_bytes);
        bd->buffer_msb = 0U;
        bd->control = (buf_bytes & AXIDMA_BD_CTRL_LEN_MASK) |
                      (s2mm ? 0U : (AXIDMA_BD_CTRL_SOF | AXIDMA_BD_CTRL_EOF));
        bd->status = 0U;
    }
    memory_barrier();
}

uint8_t *axidma_ring_buffer(const axidma_ring_t *ring, uint32_t index)
{
    return &ring->buffers[(size_t)(index % ring->count) * ring->buf_bytes];
}

void axidma_ring_start(axidma_ring_t *ring)
{
    ring->regs.write(ring->regs.ctx, reg_cur(ring), bd_phys(ring, 0U));
    ring->regs.write(ring->regs.ctx, reg_cr(ring),
                     AXIDMA_CR_RS | AXIDMA_CR_IOC_IRQ | AXIDMA_CR_ERR_IRQ | AXIDMA_CR_THRESH(1U));
    memory_barrier();
    /* All but none: the tail is the last descriptor, so the whole ring is armed. */
    ring->regs.write(ring->regs.ctx, reg_tail(ring), bd_phys(ring, ring->count - 1U));
}

uint32_t axidma_ring_ack(axidma_ring_t *ring)
{
    const uint32_t sr = ring->regs.read(ring->regs.ctx, reg_sr(ring));
    ring->regs.write(ring->regs.ctx, reg_sr(ring), sr & (AXIDMA_SR_IOC_IRQ | AXIDMA_SR_ERR_IRQ));
    if ((sr & (AXIDMA_SR_ERR_MASK | AXIDMA_SR_ERR_IRQ)) != 0U)
    {
        ++ring->errors;
    }
    return sr;
}

uint8_t *axidma_ring_peek_done(const axidma_ring_t *ring, uint32_t *len)
{
    const volatile axidma_bd_t *bd = &ring->bds[ring->next];
    const uint32_t status = bd->status;
    if ((status & AXIDMA_BD_STS_CMPLT) == 0U)
    {
        return NULL;
    }
    if (len != NULL)
    {
        *len = ring->s2mm ? (status & AXIDMA_BD_STS_LEN_MASK) : ring->buf_bytes;
    }
    return axidma_ring_buffer(ring, ring->next);
}

void axidma_ring_requeue(axidma_ring_t *ring)
{
    volatile axidma_bd_t *bd = &ring->bds[ring->next];
    if ((bd->status & AXIDMA_BD_STS_ERR_MASK) != 0U)
    {
        ++ring->errors;
    }
    bd->status = 0U;
    bd->control = (ring->buf_bytes & AXIDMA_BD_CTRL_LEN_MASK) |
                  (ring->s2mm ? 0U : (AXIDMA_BD_CTRL_SOF | AXIDMA_BD_CTRL_EOF));
    memory_barrier();
    ring->regs.write(ring->regs.ctx, reg_tail(ring), bd_phys(ring, ring->next));
    ring->next = (ring->next + 1U) % ring->count;
}

bool axidma_ring_check_idle(axidma_ring_t *ring)
{
    const uint32_t sr = ring->regs.read(ring->regs.ctx, reg_sr(ring));
    if (((sr & AXIDMA_SR_IDLE) != 0U) && ((sr & AXIDMA_SR_HALTED) == 0U))
    {
        ++ring->restarts;
        return true;
    }
    return false;
}
