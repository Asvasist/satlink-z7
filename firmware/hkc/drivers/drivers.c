/**
 * @file drivers.c
 * @brief Register-level drivers for the MicroBlaze V subsystem (see drivers.h for the IP list).
 *
 * @implements SRS-HKC-001
 */
#include "hkc/drivers.h"

#include "hkc/board.h"
#include "hkc/mmio.h"

/* ---- UART Lite (PG142) ---- */
#define UART_TX           (SATLINK_HKC_HKC_UART_BASE + 0x04U)
#define UART_STAT         (SATLINK_HKC_HKC_UART_BASE + 0x08U)
#define UART_STAT_TX_FULL (1UL << 3U)

void hkc_uart_putc(char c)
{
    while ((hkc_read32(UART_STAT) & UART_STAT_TX_FULL) != 0U)
    {
    }
    hkc_write32(UART_TX, (uint32_t)(uint8_t)c);
}

void hkc_uart_puts(const char *s)
{
    for (const char *p = s; *p != '\0'; ++p)
    {
        if (*p == '\n')
        {
            hkc_uart_putc('\r');
        }
        hkc_uart_putc(*p);
    }
}

void hkc_uart_put_hex32(uint32_t value)
{
    static const char k_digits[] = "0123456789ABCDEF";
    hkc_uart_puts("0x");
    for (int32_t shift = 28; shift >= 0; shift -= 4)
    {
        hkc_uart_putc(k_digits[(value >> (uint32_t)shift) & 0x0FU]);
    }
}

void hkc_uart_put_dec(uint32_t value)
{
    char buf[11];
    uint32_t n = 0U;
    uint32_t v = value;
    do
    {
        buf[n] = (char)('0' + (char)(v % 10U));
        ++n;
        v /= 10U;
    } while ((v != 0U) && (n < sizeof(buf)));
    while (n > 0U)
    {
        --n;
        hkc_uart_putc(buf[n]);
    }
}

/* ---- AXI GPIO (PG144) ---- */
#define GPIO_DATA  (SATLINK_HKC_HKC_GPIO_BASE + 0x00U)
#define GPIO_TRI   (SATLINK_HKC_HKC_GPIO_BASE + 0x04U)
#define GPIO_DATA2 (SATLINK_HKC_HKC_GPIO_BASE + 0x08U)
#define GPIO_TRI2  (SATLINK_HKC_HKC_GPIO_BASE + 0x0CU)

void hkc_gpio_init(void)
{
    hkc_write32(GPIO_TRI, 0x0FU); /* SW0..3 inputs */
    hkc_write32(GPIO_DATA2, 0U);
    hkc_write32(GPIO_TRI2, 0U); /* RGB outputs */
}

uint8_t hkc_gpio_switches(void)
{
    return (uint8_t)(hkc_read32(GPIO_DATA) & 0x0FU);
}

void hkc_gpio_set_rgb(uint8_t rgb)
{
    hkc_write32(GPIO_DATA2, (uint32_t)rgb & 0x07U);
}

/* ---- AXI Timer (PG079), timer 0 ---- */
#define TMR_TCSR0 (SATLINK_HKC_HKC_TIMER_BASE + 0x00U)
#define TMR_TLR0  (SATLINK_HKC_HKC_TIMER_BASE + 0x04U)
#define TMR_UDT   (1UL << 1U)
#define TMR_ARHT  (1UL << 4U)
#define TMR_LOAD  (1UL << 5U)
#define TMR_ENIT  (1UL << 6U)
#define TMR_ENT   (1UL << 7U)
#define TMR_TINT  (1UL << 8U)

void hkc_timer_start_tick(void)
{
    hkc_write32(TMR_TCSR0, 0U);
    hkc_write32(TMR_TLR0, (uint32_t)(HKC_AXI_CLK_HZ / 1000UL) - 2U); /* PG079: period = TLR + 2 */
    hkc_write32(TMR_TCSR0, TMR_LOAD);
    hkc_write32(TMR_TCSR0, TMR_ENT | TMR_ENIT | TMR_ARHT | TMR_UDT | TMR_TINT);
}

void hkc_timer_ack(void)
{
    hkc_write32(TMR_TCSR0, hkc_read32(TMR_TCSR0) | TMR_TINT);
}

/* ---- AXI INTC (PG099) ---- */
#define INTC_IPR        (SATLINK_HKC_HKC_INTC_BASE + 0x04U)
#define INTC_IER        (SATLINK_HKC_HKC_INTC_BASE + 0x08U)
#define INTC_IAR        (SATLINK_HKC_HKC_INTC_BASE + 0x0CU)
#define INTC_MER        (SATLINK_HKC_HKC_INTC_BASE + 0x1CU)
#define INTC_MER_ME_HIE (0x3U)

void hkc_intc_init(uint32_t enable_mask)
{
    hkc_write32(INTC_IER, 0U);
    hkc_write32(INTC_IAR, 0xFFFFFFFFUL);
    hkc_write32(INTC_IER, enable_mask);
    hkc_write32(INTC_MER, INTC_MER_ME_HIE);
}

uint32_t hkc_intc_pending(void)
{
    return hkc_read32(INTC_IPR);
}

void hkc_intc_ack(uint32_t mask)
{
    hkc_write32(INTC_IAR, mask);
}

/* ---- AXI Timebase Watchdog (PG128) ---- */
#define WDT_TWCSR0 (SATLINK_HKC_HKC_WDT_BASE + 0x00U)
#define WDT_TWCSR1 (SATLINK_HKC_HKC_WDT_BASE + 0x04U)
#define WDT_EWDT1  (1UL << 0U)
#define WDT_WDS    (1UL << 1U)
#define WDT_WRS    (1UL << 2U)
#define WDT_EWDT2  (1UL << 0U)

void hkc_wdt_enable(void)
{
    hkc_write32(WDT_TWCSR0, WDT_EWDT1 | WDT_WDS);
    hkc_write32(WDT_TWCSR1, WDT_EWDT2);
}

void hkc_wdt_kick(void)
{
    /* Writing 1 to WDS restarts the first timeout interval. */
    hkc_write32(WDT_TWCSR0, WDT_EWDT1 | WDT_WDS);
}

bool hkc_wdt_caused_reset(void)
{
    const bool caused = (hkc_read32(WDT_TWCSR0) & WDT_WRS) != 0U;
    if (caused)
    {
        hkc_write32(WDT_TWCSR0, WDT_WRS); /* write 1 to clear; leaves the watchdog disabled */
    }
    return caused;
}

/* ---- AXI Quad SPI (PG153), standard mode, 16-entry FIFOs ---- */
#define SPI_SRR          (SATLINK_HKC_HKC_SPI_BASE + 0x40U)
#define SPI_CR           (SATLINK_HKC_HKC_SPI_BASE + 0x60U)
#define SPI_SR           (SATLINK_HKC_HKC_SPI_BASE + 0x64U)
#define SPI_DTR          (SATLINK_HKC_HKC_SPI_BASE + 0x68U)
#define SPI_DRR          (SATLINK_HKC_HKC_SPI_BASE + 0x6CU)
#define SPI_SSR          (SATLINK_HKC_HKC_SPI_BASE + 0x70U)
#define SPI_SRR_RESET    (0x0AU)
#define SPI_CR_SPE       (1UL << 1U)
#define SPI_CR_MASTER    (1UL << 2U)
#define SPI_CR_TXRST     (1UL << 5U)
#define SPI_CR_RXRST     (1UL << 6U)
#define SPI_CR_MANUAL_SS (1UL << 7U)
#define SPI_CR_INHIBIT   (1UL << 8U)
#define SPI_SR_RX_EMPTY  (1UL << 0U)
#define SPI_SR_TX_EMPTY  (1UL << 2U)
#define SPI_FIFO_DEPTH   (16U)
#define SPI_CR_BASE      (SPI_CR_SPE | SPI_CR_MASTER | SPI_CR_MANUAL_SS) /* mode 0 */

void hkc_spi_init(void)
{
    hkc_write32(SPI_SRR, SPI_SRR_RESET);
    hkc_write32(SPI_SSR, 0xFFFFFFFFUL);
    hkc_write32(SPI_CR, SPI_CR_BASE | SPI_CR_INHIBIT | SPI_CR_TXRST | SPI_CR_RXRST);
}

int hkc_spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx;
    if ((tx == NULL) && (len > 0U))
    {
        return -1;
    }
    hkc_write32(SPI_SSR, 0xFFFFFFFEUL); /* assert CS0 for the whole transfer */
    size_t done = 0U;
    while (done < len)
    {
        size_t chunk = len - done;
        if (chunk > SPI_FIFO_DEPTH)
        {
            chunk = SPI_FIFO_DEPTH;
        }
        for (size_t i = 0U; i < chunk; ++i)
        {
            hkc_write32(SPI_DTR, (uint32_t)tx[done + i]);
        }
        hkc_write32(SPI_CR, SPI_CR_BASE); /* release the inhibit: shift the FIFO out */
        while ((hkc_read32(SPI_SR) & SPI_SR_TX_EMPTY) == 0U)
        {
        }
        hkc_write32(SPI_CR, SPI_CR_BASE | SPI_CR_INHIBIT);
        for (size_t i = 0U; i < chunk; ++i)
        {
            while ((hkc_read32(SPI_SR) & SPI_SR_RX_EMPTY) != 0U)
            {
            }
            const uint8_t byte = (uint8_t)(hkc_read32(SPI_DRR) & 0xFFU);
            if (rx != NULL)
            {
                rx[done + i] = byte;
            }
        }
        done += chunk;
    }
    hkc_write32(SPI_SSR, 0xFFFFFFFFUL);
    return 0;
}

/* ---- XADC Wizard (PG091) ---- */
#define XADC_ALARM_STATUS (SATLINK_HKC_HKC_XADC_BASE + 0x08U)

uint16_t hkc_xadc_read(hkc_xadc_channel_t channel)
{
    /* Results are 16-bit with the 12-bit conversion left-justified. */
    return (uint16_t)((hkc_read32(SATLINK_HKC_HKC_XADC_BASE + (uint32_t)channel) >> 4U) & 0x0FFFU);
}

bool hkc_xadc_alarm(void)
{
    return (hkc_read32(XADC_ALARM_STATUS) & 0xFFU) != 0U;
}
