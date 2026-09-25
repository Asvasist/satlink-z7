/**
 * @file drivers.h
 * @brief Minimal register-level drivers for the AMD/Xilinx IP around the MicroBlaze V.
 *
 * | IP                  | Product guide | Used for                                  |
 * |---------------------|---------------|-------------------------------------------|
 * | AXI UART Lite       | PG142         | Console (115200 8N1, set in the IP)       |
 * | AXI GPIO            | PG144         | Switches (ch1 in), RGB LED6 (ch2 out)     |
 * | AXI Timer           | PG079         | 1 kHz tick interrupt, microsecond counter |
 * | AXI INTC            | PG099         | Interrupt fan-in to the MEI line          |
 * | AXI Timebase WDT    | PG128         | Subsystem watchdog                        |
 * | AXI Quad SPI        | PG153         | SPI master to PmodCAN #2 (standard mode)  |
 * | XADC Wizard (AXI)   | PG091, UG480  | Die temperature and supply voltages       |
 *
 * Only the features this firmware uses are implemented.
 *
 * @implements SRS-HKC-001
 */
#ifndef HKC_DRIVERS_H
#define HKC_DRIVERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- UART Lite ---- */
void hkc_uart_putc(char c);
void hkc_uart_puts(const char *s);
void hkc_uart_put_hex32(uint32_t value);
void hkc_uart_put_dec(uint32_t value);

/* ---- GPIO ---- */
void hkc_gpio_init(void);
uint8_t hkc_gpio_switches(void);
void hkc_gpio_set_rgb(uint8_t rgb);

/* ---- Timer: 1 kHz tick on timer 0 ---- */
void hkc_timer_start_tick(void);
/** Acknowledge the tick interrupt (called from the interrupt dispatcher). */
void hkc_timer_ack(void);

/* ---- Interrupt controller ---- */
void hkc_intc_init(uint32_t enable_mask);
/** Pending and enabled inputs; acknowledge with hkc_intc_ack(). */
uint32_t hkc_intc_pending(void);
void hkc_intc_ack(uint32_t mask);

/* ---- Watchdog ---- */
void hkc_wdt_enable(void);
void hkc_wdt_kick(void);
/** True if the last reset was caused by the watchdog; clears the flag. */
bool hkc_wdt_caused_reset(void);

/* ---- SPI (chip select 0 = PmodCAN #2) ---- */
void hkc_spi_init(void);
/** Full-duplex transfer with CS held low for all @p len bytes. Matches satlink_mcp2515_xfer_fn. */
int hkc_spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);

/* ---- XADC ---- */
typedef enum
{
    HKC_XADC_TEMP = 0x200U,
    HKC_XADC_VCCINT = 0x204U,
    HKC_XADC_VCCAUX = 0x208U,
    HKC_XADC_VBRAM = 0x218U,
    HKC_XADC_VCCPINT = 0x234U,
    HKC_XADC_VCCPAUX = 0x238U,
    HKC_XADC_VCCODDR = 0x23CU,
} hkc_xadc_channel_t;

/** 12-bit conversion result of @p channel. */
uint16_t hkc_xadc_read(hkc_xadc_channel_t channel);
/** True if any XADC alarm output is active. */
bool hkc_xadc_alarm(void);

#endif /* HKC_DRIVERS_H */
