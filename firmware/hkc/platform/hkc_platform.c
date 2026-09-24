/**
 * @file hkc_platform.c
 * @implements SRS-HKC-004
 */
#include "hkc_platform.h"

#include <stddef.h>

#include "satlink/regs/address_map.h"

/* AXI UART Lite */
#define UART_TX_FIFO          (0x04U)
#define UART_STATUS           (0x08U)
#define UART_CONTROL          (0x0CU)
#define UART_STAT_TX_FULL     (0x08U)
#define UART_CTRL_RESET_FIFOS (0x03U)

/* AXI Timer, timer 0 */
#define TIMER_TCSR0           (0x00U)
#define TIMER_TLR0            (0x04U)
#define TIMER_TCR0            (0x08U)
#define TIMER_CSR_LOAD        (0x20U)
#define TIMER_CSR_AUTO_RELOAD (0x10U)
#define TIMER_CSR_ENABLE      (0x80U)

/* AXI Timebase Watchdog */
#define WDT_TWCSR0     (0x00U)
#define WDT_TWCSR1     (0x04U)
#define WDT_CSR0_EWDT1 (0x02U)
#define WDT_CSR0_WDS   (0x04U) /* write 1 to restart the timebase */
#define WDT_CSR0_WRS   (0x08U) /* the last reset was the watchdog; write 1 to clear */
#define WDT_CSR1_EWDT2 (0x01U)

/* XADC Wizard: the DRP registers start at 0x200 */
#define XADC_DATA_BASE (0x200U)

/* AXI Quad SPI, standard mode */
#define SPI_SRR             (0x40U)
#define SPI_CR              (0x60U)
#define SPI_SR              (0x64U)
#define SPI_DTR             (0x68U)
#define SPI_DRR             (0x6CU)
#define SPI_SSR             (0x70U)
#define SPI_SRR_RESET       (0x0AU)
#define SPI_CR_ENABLE       (0x02U)
#define SPI_CR_MASTER       (0x04U)
#define SPI_CR_TXFIFO_RESET (0x20U)
#define SPI_CR_RXFIFO_RESET (0x40U)
#define SPI_CR_MANUAL_SS    (0x80U)
#define SPI_SR_RX_EMPTY     (0x01U)
#define SPI_POLL_LIMIT      (200000U)

#define CAN_SEND_RETRIES  (50U)
#define CAN_SEND_RETRY_US (200U)

#define TICKS_PER_MS (HKC_CLOCK_HZ / 1000U)
#define TICKS_PER_US (HKC_CLOCK_HZ / 1000000U)

static inline uint32_t reg_read(uint32_t base, uint32_t offset)
{
    return *(volatile const uint32_t *)(uintptr_t)(base + offset);
}

static inline void reg_write(uint32_t base, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(base + offset) = value;
}

/* ---------------------------------------------------------------------------------------- */
/* Console                                                                                  */
/* ---------------------------------------------------------------------------------------- */

void hkc_uart_init(void)
{
    reg_write(SATLINK_HKC_HKC_UART_BASE, UART_CONTROL, UART_CTRL_RESET_FIFOS);
}

static void uart_putc(char c)
{
    while ((reg_read(SATLINK_HKC_HKC_UART_BASE, UART_STATUS) & UART_STAT_TX_FULL) != 0U)
    {
        /* wait for room in the transmit FIFO */
    }
    reg_write(SATLINK_HKC_HKC_UART_BASE, UART_TX_FIFO, (uint32_t)(uint8_t)c);
}

void hkc_uart_puts(const char *text)
{
    if (text != NULL)
    {
        for (size_t i = 0U; text[i] != '\0'; ++i)
        {
            if (text[i] == '\n')
            {
                uart_putc('\r');
            }
            uart_putc(text[i]);
        }
    }
}

void hkc_uart_put_hex32(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    uart_putc('0');
    uart_putc('x');
    for (uint32_t shift = 32U; shift > 0U; shift -= 4U)
    {
        uart_putc(digits[(value >> (shift - 4U)) & 0x0FU]);
    }
}

void hkc_uart_put_dec(uint32_t value)
{
    char text[11];
    size_t pos = sizeof(text);
    uint32_t rest = value;

    do
    {
        pos--;
        text[pos] = (char)('0' + (rest % 10U));
        rest /= 10U;
    } while (rest != 0U);

    for (; pos < sizeof(text); ++pos)
    {
        uart_putc(text[pos]);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* Time                                                                                     */
/* ---------------------------------------------------------------------------------------- */

static uint32_t g_last_ticks;
static uint32_t g_millis;
static uint32_t g_fraction; /* ticks that do not yet add up to a millisecond */

void hkc_timer_init(void)
{
    reg_write(SATLINK_HKC_HKC_TIMER_BASE, TIMER_TCSR0, 0U);
    reg_write(SATLINK_HKC_HKC_TIMER_BASE, TIMER_TLR0, 0U);
    reg_write(SATLINK_HKC_HKC_TIMER_BASE, TIMER_TCSR0, TIMER_CSR_LOAD);
    /* Counts up and reloads zero on overflow, so it runs free. */
    reg_write(SATLINK_HKC_HKC_TIMER_BASE, TIMER_TCSR0, TIMER_CSR_ENABLE | TIMER_CSR_AUTO_RELOAD);

    g_last_ticks = reg_read(SATLINK_HKC_HKC_TIMER_BASE, TIMER_TCR0);
    g_millis = 0U;
    g_fraction = 0U;
}

uint32_t hkc_millis(void)
{
    const uint32_t now = reg_read(SATLINK_HKC_HKC_TIMER_BASE, TIMER_TCR0);

    g_fraction += now - g_last_ticks;
    g_last_ticks = now;
    g_millis += g_fraction / TICKS_PER_MS;
    g_fraction %= TICKS_PER_MS;

    return g_millis;
}

void hkc_delay_us(uint32_t microseconds)
{
    const uint32_t start = reg_read(SATLINK_HKC_HKC_TIMER_BASE, TIMER_TCR0);
    const uint32_t wait = microseconds * TICKS_PER_US;

    while ((reg_read(SATLINK_HKC_HKC_TIMER_BASE, TIMER_TCR0) - start) < wait)
    {
        /* busy wait */
    }
}

/* ---------------------------------------------------------------------------------------- */
/* Watchdog                                                                                 */
/* ---------------------------------------------------------------------------------------- */

bool hkc_wdt_reset_was_watchdog(void)
{
    return (reg_read(SATLINK_HKC_HKC_WDT_BASE, WDT_TWCSR0) & WDT_CSR0_WRS) != 0U;
}

void hkc_wdt_start(void)
{
    const uint32_t csr0 = reg_read(SATLINK_HKC_HKC_WDT_BASE, WDT_TWCSR0);

    /* Both enable bits have to be set; WRS and WDS are cleared by writing them as ones. */
    reg_write(SATLINK_HKC_HKC_WDT_BASE, WDT_TWCSR0,
              csr0 | WDT_CSR0_WRS | WDT_CSR0_WDS | WDT_CSR0_EWDT1);
    reg_write(SATLINK_HKC_HKC_WDT_BASE, WDT_TWCSR1, WDT_CSR1_EWDT2);
}

void hkc_wdt_kick(void)
{
    const uint32_t csr0 = reg_read(SATLINK_HKC_HKC_WDT_BASE, WDT_TWCSR0);

    reg_write(SATLINK_HKC_HKC_WDT_BASE, WDT_TWCSR0, csr0 | WDT_CSR0_WRS | WDT_CSR0_WDS);
}

/* ---------------------------------------------------------------------------------------- */
/* XADC                                                                                     */
/* ---------------------------------------------------------------------------------------- */

uint16_t hkc_xadc_read(hkc_xadc_channel_t channel)
{
    const uint32_t offset = XADC_DATA_BASE + (4U * (uint32_t)channel);

    return (uint16_t)(reg_read(SATLINK_HKC_HKC_XADC_BASE, offset) & 0xFFFFU);
}

/* ---------------------------------------------------------------------------------------- */
/* SPI master and CAN controller                                                            */
/* ---------------------------------------------------------------------------------------- */

static satlink_status_t spi_wait_rx(void)
{
    satlink_status_t status = SATLINK_ERR_TIMEOUT;

    for (uint32_t i = 0U; (i < SPI_POLL_LIMIT) && (status != SATLINK_OK); ++i)
    {
        if ((reg_read(SATLINK_HKC_HKC_SPI_BASE, SPI_SR) & SPI_SR_RX_EMPTY) == 0U)
        {
            status = SATLINK_OK;
        }
    }

    return status;
}

/** One chip-select-framed transaction, byte by byte, so it works with any FIFO depth. */
static satlink_status_t spi_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    satlink_status_t status = SATLINK_OK;

    (void)ctx;
    reg_write(SATLINK_HKC_HKC_SPI_BASE, SPI_SSR, 0xFFFFFFFEU); /* select slave 0, active low */

    for (size_t i = 0U; (i < len) && (status == SATLINK_OK); ++i)
    {
        reg_write(SATLINK_HKC_HKC_SPI_BASE, SPI_DTR, (uint32_t)tx[i]);
        status = spi_wait_rx();
        if (status == SATLINK_OK)
        {
            rx[i] = (uint8_t)(reg_read(SATLINK_HKC_HKC_SPI_BASE, SPI_DRR) & 0xFFU);
        }
    }

    reg_write(SATLINK_HKC_HKC_SPI_BASE, SPI_SSR, 0xFFFFFFFFU); /* deselect */

    return status;
}

static void can_delay(void *ctx, uint32_t microseconds)
{
    (void)ctx;
    hkc_delay_us(microseconds);
}

satlink_status_t hkc_can_init(satlink_mcp2515_t *dev)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if (dev != NULL)
    {
        reg_write(SATLINK_HKC_HKC_SPI_BASE, SPI_SRR, SPI_SRR_RESET);
        /* Master, mode 0, software-controlled slave select, FIFOs cleared. */
        reg_write(SATLINK_HKC_HKC_SPI_BASE, SPI_CR,
                  SPI_CR_ENABLE | SPI_CR_MASTER | SPI_CR_MANUAL_SS | SPI_CR_TXFIFO_RESET |
                      SPI_CR_RXFIFO_RESET);

        status = satlink_mcp2515_init(dev, spi_transfer, can_delay, NULL);
        if (status == SATLINK_OK)
        {
            status = satlink_mcp2515_configure(dev, HKC_CAN_OSC_HZ, HKC_CAN_BITRATE,
                                               SATLINK_MCP2515_MODE_NORMAL);
        }
    }

    return status;
}

satlink_status_t hkc_can_send(const satlink_mcp2515_t *dev, const satlink_can_frame_t *frame)
{
    satlink_status_t status = SATLINK_ERR_BUSY;

    for (uint32_t attempt = 0U; (attempt < CAN_SEND_RETRIES) && (status == SATLINK_ERR_BUSY);
         ++attempt)
    {
        status = satlink_mcp2515_send(dev, frame);
        if (status == SATLINK_ERR_BUSY)
        {
            hkc_delay_us(CAN_SEND_RETRY_US);
        }
    }

    return status;
}

/* ---------------------------------------------------------------------------------------- */
/* Control transfer                                                                         */
/* ---------------------------------------------------------------------------------------- */

void hkc_jump(uintptr_t address)
{
    void (*entry)(void) = (void (*)(void))address;

    __asm__ volatile("fence.i" ::: "memory"); /* the code was just written as data */
    entry();

    for (;;)
    {
        /* the entry point must not return */
    }
}
