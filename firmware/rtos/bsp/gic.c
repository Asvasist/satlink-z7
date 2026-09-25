/**
 * @file gic.c
 * @brief GIC (PL390) access for Core 1 and the interrupt dispatcher FreeRTOS calls.
 *
 * Linux configures the distributor; Core 1 only programs the SPIs it owns (priority, target,
 * trigger, enable) and its own banked CPU interface (ADR-0003).
 *
 * @implements SRS-AMP-001
 */
#include <stddef.h>

#include "rtos/bsp.h"

#define GICD_BASE         (0xF8F01000UL)
#define GICC_BASE         (0xF8F00100UL)
#define GICD_CTLR         (GICD_BASE + 0x000U)
#define GICD_ISENABLER(n) (GICD_BASE + 0x100U + (4U * (n)))
#define GICD_ICENABLER(n) (GICD_BASE + 0x180U + (4U * (n)))
#define GICD_ISPENDR(n)   (GICD_BASE + 0x200U + (4U * (n)))
#define GICD_IPRIORITYR   (GICD_BASE + 0x400U)
#define GICD_ITARGETSR    (GICD_BASE + 0x800U)
#define GICD_ICFGR(n)     (GICD_BASE + 0xC00U + (4U * (n)))
#define GICC_CTLR         (GICC_BASE + 0x00U)
#define GICC_PMR          (GICC_BASE + 0x04U)
#define GICC_BPR          (GICC_BASE + 0x08U)

#define MAX_IRQS (96U)
#define SPURIOUS (1023U)

typedef struct
{
    bsp_irq_handler_t handler;
    void *ctx;
} irq_slot_t;

static irq_slot_t g_handlers[MAX_IRQS];
static uint32_t g_spurious;

void bsp_gic_init(void)
{
    if ((bsp_read32(GICD_CTLR) & 1U) == 0U)
    {
        bsp_write32(GICD_CTLR, 1U); /* standalone (QEMU): nobody enabled the distributor */
    }
    bsp_write32(GICC_PMR, 0xF8U);
    bsp_write32(GICC_BPR, 0U);
    bsp_write32(GICC_CTLR, 1U);
}

static void write_byte_field(uintptr_t base, uint32_t irq, uint8_t value)
{
    volatile uint8_t *reg = (volatile uint8_t *)(base + irq);
    *reg = value;
}

void bsp_gic_attach(uint32_t irq, uint32_t priority, bool edge, bsp_irq_handler_t handler,
                    void *ctx)
{
    if (irq >= MAX_IRQS)
    {
        return;
    }
    g_handlers[irq].handler = handler;
    g_handlers[irq].ctx = ctx;
    write_byte_field(GICD_IPRIORITYR, irq, (uint8_t)((priority & 0x1FU) << 3U));
    if (irq >= 32U)
    {
        write_byte_field(GICD_ITARGETSR, irq, (uint8_t)(1U << bsp_cpu_id()));
        const uint32_t reg = GICD_ICFGR(irq / 16U);
        const uint32_t shift = ((irq % 16U) * 2U) + 1U;
        uint32_t cfg = bsp_read32(reg);
        cfg = edge ? (cfg | (1U << shift)) : (cfg & ~(1U << shift));
        bsp_write32(reg, cfg);
    }
}

void bsp_gic_enable(uint32_t irq)
{
    bsp_write32(GICD_ISENABLER(irq / 32U), 1U << (irq % 32U));
}

void bsp_gic_disable(uint32_t irq)
{
    bsp_write32(GICD_ICENABLER(irq / 32U), 1U << (irq % 32U));
}

void bsp_gic_set_pending(uint32_t irq)
{
    bsp_write32(GICD_ISPENDR(irq / 32U), 1U << (irq % 32U));
}

/* Called by the FreeRTOS ARM_CA9 port with the acknowledged interrupt ID; the port writes the
 * end-of-interrupt afterwards. */
void vApplicationFPUSafeIRQHandler(uint32_t ulICCIAR);
void vApplicationFPUSafeIRQHandler(uint32_t ulICCIAR)
{
    const uint32_t irq = ulICCIAR & 0x3FFU;
    if ((irq < MAX_IRQS) && (g_handlers[irq].handler != NULL))
    {
        g_handlers[irq].handler(g_handlers[irq].ctx);
    }
    else if (irq != SPURIOUS)
    {
        ++g_spurious;
    }
}
