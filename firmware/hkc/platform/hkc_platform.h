/**
 * @file hkc_platform.h
 * @brief Board support for the MicroBlaze V housekeeping controller: console, time, watchdog,
 *        XADC, the SPI master that talks to the MCP2515, and the jump between bootloader and
 *        application.
 *
 * Register offsets are those of the Xilinx IP (AXI UART Lite, AXI Timer, AXI Timebase Watchdog,
 * XADC Wizard, AXI Quad SPI in standard mode); base addresses come from the generated ICD header.
 * Everything is polled: no interrupt controller is needed.
 *
 * @implements SRS-HKC-004
 */
#ifndef HKC_PLATFORM_H
#define HKC_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/can/mcp2515.h"
#include "satlink/common/status.h"

/** Clock of the MicroBlaze V and of the AXI peripherals; set with -DHKC_CLOCK_HZ. */
#ifndef HKC_CLOCK_HZ
#define HKC_CLOCK_HZ (100000000U)
#endif

/** Oscillator on the PmodCAN #2 board; set with -DHKC_CAN_OSC_HZ. */
#ifndef HKC_CAN_OSC_HZ
#define HKC_CAN_OSC_HZ (16000000U)
#endif

#define HKC_CAN_BITRATE (500000U)

/** XADC data registers, as channel numbers (register offset = 0x200 + 4 * channel). */
typedef enum
{
    HKC_XADC_TEMP = 0,
    HKC_XADC_VCCINT = 1,
    HKC_XADC_VCCAUX = 2,
    HKC_XADC_VCCBRAM = 6
} hkc_xadc_channel_t;

/* Console (AXI UART Lite; the baud rate is fixed in the IP configuration). */
void hkc_uart_init(void);
void hkc_uart_puts(const char *text);
void hkc_uart_put_hex32(uint32_t value);
void hkc_uart_put_dec(uint32_t value);

/* Time (AXI Timer 0 as a free-running 32-bit counter). Call hkc_millis() at least every 40 s. */
void hkc_timer_init(void);
uint32_t hkc_millis(void);
void hkc_delay_us(uint32_t microseconds);

/* Watchdog (AXI Timebase Watchdog; its timeout is set in the IP configuration). */
bool hkc_wdt_reset_was_watchdog(void); /**< read before hkc_wdt_start() */
void hkc_wdt_start(void);
void hkc_wdt_kick(void);

/** Raw 16-bit XADC data register (12-bit result, left-justified). */
uint16_t hkc_xadc_read(hkc_xadc_channel_t channel);

/** Bring up the AXI Quad SPI and configure the MCP2515 on it at HKC_CAN_BITRATE. */
satlink_status_t hkc_can_init(satlink_mcp2515_t *dev);

/** Send a frame, waiting briefly while the previous one is still leaving the controller. */
satlink_status_t hkc_can_send(const satlink_mcp2515_t *dev, const satlink_can_frame_t *frame);

/** Flush the instruction stream and jump to @p address; does not return. */
void hkc_jump(uintptr_t address) __attribute__((noreturn));

#endif /* HKC_PLATFORM_H */
