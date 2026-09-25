/**
 * @file uart.c
 * @brief Console on PS UART0 (Cadence UART, routed over EMIO to Pmod JE), polled transmit.
 *
 * @implements SRS-AMP-001
 */
#include <stdarg.h>

#include "rtos/bsp.h"

#define UART_CR      (SATLINK_PS_PS_UART0_BASE + 0x00U)
#define UART_MR      (SATLINK_PS_PS_UART0_BASE + 0x04U)
#define UART_BAUDGEN (SATLINK_PS_PS_UART0_BASE + 0x18U)
#define UART_SR      (SATLINK_PS_PS_UART0_BASE + 0x2CU)
#define UART_FIFO    (SATLINK_PS_PS_UART0_BASE + 0x30U)
#define UART_BAUDDIV (SATLINK_PS_PS_UART0_BASE + 0x34U)
#define CR_RXRES     (1U << 0U)
#define CR_TXRES     (1U << 1U)
#define CR_RXEN      (1U << 2U)
#define CR_TXEN      (1U << 4U)
#define CR_RXDIS     (1U << 3U)
#define CR_TXDIS     (1U << 5U)
#define MR_8N1       (0x20U) /* 8 data bits, no parity, 1 stop */
#define SR_TXFULL    (1U << 4U)

void bsp_uart_init(void)
{
    /* 100 MHz UART reference clock: 100 MHz / (124 * (6 + 1)) = 115207 baud. */
    bsp_write32(UART_CR, CR_TXDIS | CR_RXDIS);
    bsp_write32(UART_MR, MR_8N1);
    bsp_write32(UART_BAUDGEN, 124U);
    bsp_write32(UART_BAUDDIV, 6U);
    bsp_write32(UART_CR, CR_TXRES | CR_RXRES);
    bsp_write32(UART_CR, CR_TXEN | CR_RXEN);
}

void bsp_uart_putc(char c)
{
    while ((bsp_read32(UART_SR) & SR_TXFULL) != 0U)
    {
    }
    bsp_write32(UART_FIFO, (uint32_t)(uint8_t)c);
}

void bsp_uart_puts(const char *s)
{
    for (const char *p = s; *p != '\0'; ++p)
    {
        if (*p == '\n')
        {
            bsp_uart_putc('\r');
        }
        bsp_uart_putc(*p);
    }
}

static void put_number(uint32_t value, uint32_t base, uint32_t width, char pad, bool negative)
{
    char buf[12];
    uint32_t n = 0U;
    uint32_t v = value;
    do
    {
        const uint32_t digit = v % base;
        buf[n] = (char)((digit < 10U) ? ('0' + (char)digit) : ('a' + (char)(digit - 10U)));
        ++n;
        v /= base;
    } while ((v != 0U) && (n < sizeof(buf)));
    uint32_t len = n + (negative ? 1U : 0U);
    if (negative && (pad == '0'))
    {
        bsp_uart_putc('-');
    }
    while (len < width)
    {
        bsp_uart_putc(pad);
        ++len;
    }
    if (negative && (pad != '0'))
    {
        bsp_uart_putc('-');
    }
    while (n > 0U)
    {
        --n;
        bsp_uart_putc(buf[n]);
    }
}

void bsp_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p != '\0'; ++p)
    {
        if (*p != '%')
        {
            if (*p == '\n')
            {
                bsp_uart_putc('\r');
            }
            bsp_uart_putc(*p);
            continue;
        }
        ++p;
        char pad = ' ';
        uint32_t width = 0U;
        if (*p == '0')
        {
            pad = '0';
            ++p;
        }
        while ((*p >= '0') && (*p <= '9'))
        {
            width = (width * 10U) + (uint32_t)(*p - '0');
            ++p;
        }
        switch (*p)
        {
        case 's':
            bsp_uart_puts(va_arg(ap, const char *));
            break;
        case 'c':
            bsp_uart_putc((char)va_arg(ap, int));
            break;
        case 'd':
        {
            const int32_t v = va_arg(ap, int32_t);
            const uint32_t mag = (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
            put_number(mag, 10U, width, pad, v < 0);
            break;
        }
        case 'u':
            put_number(va_arg(ap, uint32_t), 10U, width, pad, false);
            break;
        case 'x':
            put_number(va_arg(ap, uint32_t), 16U, width, pad, false);
            break;
        case '%':
            bsp_uart_putc('%');
            break;
        default:
            bsp_uart_putc('?');
            break;
        }
        if (*p == '\0')
        {
            break;
        }
    }
    va_end(ap);
}
