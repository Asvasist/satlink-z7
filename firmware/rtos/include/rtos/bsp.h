/**
 * @file bsp.h
 * @brief Board support for the Core 1 firmware: MMU, GIC, timers, console UART, faults.
 *
 * @implements SRS-AMP-001
 */
#ifndef RTOS_BSP_H
#define RTOS_BSP_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/regs/address_map.h"

/* ---- MMIO ---- */
static inline uint32_t bsp_read32(uintptr_t addr)
{
    return *(volatile const uint32_t *)addr;
}

static inline void bsp_write32(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

/* ---- MMU (mmu.c) ---- */
/** Memory types of the translation table. */
typedef enum
{
    BSP_MEM_FAULT = 0, /**< Not ours: any access aborts. */
    BSP_MEM_NORMAL_WB, /**< Cacheable code and data. */
    BSP_MEM_NORMAL_NC, /**< Non-cacheable normal memory (shared with Linux or DMA). */
    BSP_MEM_DEVICE,    /**< Peripheral registers. */
} bsp_mem_type_t;

typedef struct
{
    uint32_t base;
    uint32_t size;
    bsp_mem_type_t type;
    bool exec;
} bsp_mem_region_t;

/** Builds the translation table from the region list in mmu.c and enables MMU and L1 caches.
 *  Called from startup.S before main(). */
void satlink_mmu_enable(void);

/** Memory type the live translation table gives @p addr (walks L1 and L2). */
bsp_mem_type_t bsp_mmu_type_of(uint32_t addr);

/* ---- CPU ---- */
/** Index of the core this code runs on (MPIDR.Aff0): 1 on the board, 0 under QEMU. */
uint32_t bsp_cpu_id(void);

/* ---- GIC (gic.c) ---- */
typedef void (*bsp_irq_handler_t)(void *ctx);
/** Route SPI/PPI @p irq to this core with @p priority (0..31, lower = more urgent), rising
 *  edge or level, and install @p handler. Does not reset the distributor (Linux owns it). */
void bsp_gic_attach(uint32_t irq, uint32_t priority, bool edge, bsp_irq_handler_t handler,
                    void *ctx);
void bsp_gic_enable(uint32_t irq);
void bsp_gic_disable(uint32_t irq);
/** Set an SPI pending by software (IPC notification to the other core). */
void bsp_gic_set_pending(uint32_t irq);
/** Prepare this core's CPU interface; enables the distributor only if nobody did (QEMU). */
void bsp_gic_init(void);

/* ---- Timers (timer.c) ---- */
/** Microseconds since the global timer started (64-bit counter at CPU / 2), wraps at 2^32. */
uint32_t satlink_rtos_time_us(void);
uint64_t bsp_time_ticks(void);
uint32_t bsp_ticks_to_us(uint64_t ticks);

/* ---- Console UART (uart.c): PS UART0, 115200 8N1 ---- */
void bsp_uart_init(void);
void bsp_uart_putc(char c);
void bsp_uart_puts(const char *s);
/** Minimal printf: %s %d %u %x %c %%, optional width for %u/%d/%x with '0' padding. Every
 *  numeric argument is a 32-bit value (int32_t for %d, uint32_t for %u and %x). */
void bsp_printf(const char *fmt, ...);

/* ---- Faults (fault.c) ---- */
/** Fault codes published in the shared control block. */
#define BSP_FAULT_UNDEF          (1U)
#define BSP_FAULT_PREFETCH_ABORT (2U)
#define BSP_FAULT_DATA_ABORT     (3U)
#define BSP_FAULT_FIQ            (4U)
#define BSP_FAULT_ASSERT         (5U)
#define BSP_FAULT_STACK_OVERFLOW (6U)
#define BSP_FAULT_MALLOC         (7U)

/** Record a fault for Linux, print it and stop this core (Linux restarts it). */
void satlink_fault(uint32_t code, uint32_t addr) __attribute__((noreturn));

/** Hook the fault path calls to publish the fault (set by the IPC layer). */
void bsp_set_fault_hook(void (*hook)(uint32_t code, uint32_t addr));

#endif /* RTOS_BSP_H */
