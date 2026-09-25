/**
 * @file payload.c
 * @brief Driver for the Core 1-owned PL blocks: payload_ctrl and axi_dma_modem.
 *
 * @implements SRS-AMP-006
 */
#include "rtos/payload.h"

#include <math.h>

#include "satlink/regs/payload_ctrl.h"

#include "rtos/axi_dma.h"
#include "rtos/bsp.h"

#define PC(off)        ((uintptr_t)SATLINK_PS_PAYLOAD_CTRL_BASE + (uintptr_t)(off))
#define EXPECTED_MAJOR (2U)

typedef struct
{
    axidma_bd_t bd[PAYLOAD_TX_BUFS + PAYLOAD_RX_BUFS];
    uint32_t tx[PAYLOAD_TX_BUFS][PAYLOAD_TX_SYMBOLS_PER_BUF];
    uint32_t rx[PAYLOAD_RX_BUFS][PAYLOAD_RX_SAMPLES_PER_BUF];
} dma_memory_t;

static dma_memory_t g_dma __attribute__((section(".dma_buffers"), aligned(64)));
static axidma_ring_t g_tx;
static axidma_ring_t g_rx;
static TaskHandle_t g_task;
static uint32_t g_tx_bit;
static uint32_t g_rx_bit;
static volatile uint32_t g_underruns;
static volatile uint32_t g_overruns;

static uint32_t dma_read(void *ctx, uint32_t offset)
{
    (void)ctx;
    return bsp_read32((uintptr_t)SATLINK_PS_AXI_DMA_MODEM_BASE + offset);
}

static void dma_write(void *ctx, uint32_t offset, uint32_t value)
{
    (void)ctx;
    bsp_write32((uintptr_t)SATLINK_PS_AXI_DMA_MODEM_BASE + offset, value);
}

static const axidma_regs_t k_regs = {NULL, &dma_read, &dma_write};

static int16_t to_q15(float v)
{
    float s = v * PAYLOAD_SAMPLE_SCALE;
    if (s > 32767.0F)
    {
        s = 32767.0F;
    }
    else if (s < -32768.0F)
    {
        s = -32768.0F;
    }
    return (int16_t)lrintf(s);
}

uint32_t payload_pack(satlink_cf_t v)
{
    return (uint32_t)(uint16_t)to_q15(v.re) | ((uint32_t)(uint16_t)to_q15(v.im) << 16U);
}

satlink_cf_t payload_unpack(uint32_t word)
{
    satlink_cf_t v;
    v.re = (float)(int16_t)(uint16_t)(word & 0xFFFFU) / PAYLOAD_SAMPLE_SCALE;
    v.im = (float)(int16_t)(uint16_t)(word >> 16U) / PAYLOAD_SAMPLE_SCALE;
    return v;
}

bool payload_probe(void)
{
    const uint32_t version = bsp_read32(PC(PAYLOAD_CTRL_VERSION_OFFSET));
    const uint32_t major =
        (version & PAYLOAD_CTRL_VERSION_MAJOR_MASK) >> PAYLOAD_CTRL_VERSION_MAJOR_SHIFT;
    if (major != EXPECTED_MAJOR)
    {
        bsp_printf("rtos: payload_ctrl version 0x%08x, expected major %u\n", version,
                   EXPECTED_MAJOR);
        return false;
    }
    return axidma_reset(&k_regs);
}

static void dma_isr(void *ctx)
{
    axidma_ring_t *ring = (axidma_ring_t *)ctx;
    (void)axidma_ring_ack(ring);
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(g_task, (ring == &g_tx) ? g_tx_bit : g_rx_bit, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

static void status_isr(void *ctx)
{
    (void)ctx;
    const uint32_t st = bsp_read32(PC(PAYLOAD_CTRL_STATUS_OFFSET));
    bsp_write32(PC(PAYLOAD_CTRL_STATUS_OFFSET), st); /* W1C */
    if ((st & PAYLOAD_CTRL_STATUS_TX_UNDERRUN_MASK) != 0U)
    {
        g_underruns = g_underruns + 1U;
    }
    if ((st & PAYLOAD_CTRL_STATUS_RX_OVERRUN_MASK) != 0U)
    {
        g_overruns = g_overruns + 1U;
    }
}

void payload_start(TaskHandle_t task, uint32_t tx_bit, uint32_t rx_bit)
{
    g_task = task;
    g_tx_bit = tx_bit;
    g_rx_bit = rx_bit;
    axidma_ring_init(&g_tx, &k_regs, false, &g_dma.bd[0], (uint32_t)(uintptr_t)&g_dma.bd[0],
                     PAYLOAD_TX_BUFS, (uint8_t *)g_dma.tx, (uint32_t)(uintptr_t)g_dma.tx,
                     PAYLOAD_TX_SYMBOLS_PER_BUF * 4U);
    axidma_ring_init(&g_rx, &k_regs, true, &g_dma.bd[PAYLOAD_TX_BUFS],
                     (uint32_t)(uintptr_t)&g_dma.bd[PAYLOAD_TX_BUFS], PAYLOAD_RX_BUFS,
                     (uint8_t *)g_dma.rx, (uint32_t)(uintptr_t)g_dma.rx,
                     PAYLOAD_RX_SAMPLES_PER_BUF * 4U);
    for (uint32_t b = 0U; b < PAYLOAD_TX_BUFS; ++b)
    {
        for (uint32_t i = 0U; i < PAYLOAD_TX_SYMBOLS_PER_BUF; ++i)
        {
            g_dma.tx[b][i] = 0U; /* start with silence; the first IRQs bring real symbols */
        }
    }
    bsp_gic_attach(SATLINK_PS_AXI_DMA_MODEM_IRQ_MM2S, 22U, false, &dma_isr, &g_tx);
    bsp_gic_attach(SATLINK_PS_AXI_DMA_MODEM_IRQ_S2MM, 21U, false, &dma_isr, &g_rx);
    bsp_gic_attach(SATLINK_PS_PAYLOAD_CTRL_IRQ_STATUS, 24U, false, &status_isr, NULL);
    bsp_gic_enable(SATLINK_PS_AXI_DMA_MODEM_IRQ_MM2S);
    bsp_gic_enable(SATLINK_PS_AXI_DMA_MODEM_IRQ_S2MM);
    bsp_gic_enable(SATLINK_PS_PAYLOAD_CTRL_IRQ_STATUS);

    bsp_write32(PC(PAYLOAD_CTRL_RX_FRAME_LEN_OFFSET), PAYLOAD_RX_SAMPLES_PER_BUF);
    axidma_ring_start(&g_rx);
    axidma_ring_start(&g_tx);
    bsp_write32(PC(PAYLOAD_CTRL_CTRL_OFFSET), PAYLOAD_CTRL_CTRL_RX_EN_MASK |
                                                  PAYLOAD_CTRL_CTRL_CODEC_UNMUTE_MASK |
                                                  PAYLOAD_CTRL_CTRL_IRQ_EN_MASK);
}

void payload_stop(void)
{
    bsp_write32(PC(PAYLOAD_CTRL_CTRL_OFFSET), 0U);
    bsp_gic_disable(SATLINK_PS_AXI_DMA_MODEM_IRQ_MM2S);
    bsp_gic_disable(SATLINK_PS_AXI_DMA_MODEM_IRQ_S2MM);
    bsp_gic_disable(SATLINK_PS_PAYLOAD_CTRL_IRQ_STATUS);
    (void)axidma_reset(&k_regs);
}

void payload_service_tx(void (*fill)(void *ctx, satlink_cf_t *symbols, uint32_t count), void *ctx)
{
    satlink_cf_t symbols[PAYLOAD_TX_SYMBOLS_PER_BUF];
    uint8_t *buf = axidma_ring_peek_done(&g_tx, NULL);
    while (buf != NULL)
    {
        fill(ctx, symbols, PAYLOAD_TX_SYMBOLS_PER_BUF);
        uint32_t *words = (uint32_t *)(void *)buf;
        for (uint32_t i = 0U; i < PAYLOAD_TX_SYMBOLS_PER_BUF; ++i)
        {
            words[i] = payload_pack(symbols[i]);
        }
        axidma_ring_requeue(&g_tx);
        buf = axidma_ring_peek_done(&g_tx, NULL);
    }
    if (axidma_ring_check_idle(&g_tx))
    {
        g_underruns = g_underruns + 1U;
    }
}

void payload_service_rx(void (*sink)(void *ctx, const satlink_cf_t *samples, uint32_t count),
                        void *ctx)
{
    satlink_cf_t samples[PAYLOAD_RX_SAMPLES_PER_BUF];
    uint32_t len = 0U;
    uint8_t *buf = axidma_ring_peek_done(&g_rx, &len);
    while (buf != NULL)
    {
        const uint32_t n = len / 4U;
        const uint32_t *words = (const uint32_t *)(const void *)buf;
        for (uint32_t i = 0U; (i < n) && (i < PAYLOAD_RX_SAMPLES_PER_BUF); ++i)
        {
            samples[i] = payload_unpack(words[i]);
        }
        axidma_ring_requeue(&g_rx);
        sink(ctx, samples, n);
        buf = axidma_ring_peek_done(&g_rx, &len);
    }
    if (axidma_ring_check_idle(&g_rx))
    {
        g_overruns = g_overruns + 1U;
    }
}

void payload_set_channel(uint16_t noise_level, uint16_t gain_q15)
{
    bsp_write32(PC(PAYLOAD_CTRL_NOISE_LEVEL_OFFSET), noise_level);
    bsp_write32(PC(PAYLOAD_CTRL_ATTEN_OFFSET), gain_q15);
}

void payload_set_digital_loopback(bool on)
{
    uint32_t ctrl = bsp_read32(PC(PAYLOAD_CTRL_CTRL_OFFSET));
    ctrl = on ? (ctrl | PAYLOAD_CTRL_CTRL_DIG_LOOPBACK_MASK)
              : (ctrl & ~PAYLOAD_CTRL_CTRL_DIG_LOOPBACK_MASK);
    bsp_write32(PC(PAYLOAD_CTRL_CTRL_OFFSET), ctrl);
}

uint32_t payload_underruns(void)
{
    return g_underruns;
}

uint32_t payload_overruns(void)
{
    return g_overruns;
}
