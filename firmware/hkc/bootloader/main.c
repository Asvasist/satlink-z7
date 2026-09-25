/**
 * @file main.c
 * @brief MicroBlaze V bootloader: loads the housekeeping application into LMB BRAM over CAN.
 *
 * Sequence after reset:
 *  1. Bring up UART, SPI and the MCP2515 (PmodCAN #2, 500 kbit/s).
 *  2. Announce itself with an unsolicited PING response, so Linux knows it is waiting.
 *  3. Serve the canboot protocol. If the application window already holds a valid image
 *     (warm reset) and nobody talks to the bootloader for 2 s, start it. The ENTER_BOOT
 *     command of the application sets a mailbox word that disables this timeout.
 *
 * Polled only: the bootloader leaves interrupts off.
 *
 * @implements SRS-HKC-002
 */
#include <stdbool.h>
#include <stdint.h>

#include "satlink/drivers/mcp2515.h"
#include "satlink/hkc/canboot.h"
#include "satlink/hkc/hk_proto.h"

#include "hkc/board.h"
#include "hkc/cpu.h"
#include "hkc/drivers.h"
#include "hkc/mmio.h"

/* Iterations of the polling loop that make up the autoboot window (one iteration is a few SPI
 * transfers, roughly 20 us at a 10 MHz SPI clock). */
#define AUTOBOOT_POLLS (100000UL)

/* Mailbox word 1 tells the application why it was started. */
#define MAILBOX_STAY        (HKC_MAILBOX_ADDR + 0U)
#define MAILBOX_RESET_CAUSE (HKC_MAILBOX_ADDR + 4U)

int main(void);

void hkc_trap_handler(uint32_t mcause, uint32_t mepc)
{
    hkc_uart_puts("\nbootloader: trap mcause=");
    hkc_uart_put_hex32(mcause);
    hkc_uart_puts(" mepc=");
    hkc_uart_put_hex32(mepc);
    hkc_uart_puts("\n");
    for (;;)
    {
    }
}

static void fatal(const char *what)
{
    hkc_uart_puts("bootloader: ");
    hkc_uart_puts(what);
    hkc_uart_puts(", halting\n");
    for (;;)
    {
        hkc_gpio_set_rgb(0x1U); /* solid red */
    }
}

int main(void)
{
    hkc_gpio_init();
    hkc_gpio_set_rgb(0x4U); /* blue: in the bootloader */
    hkc_spi_init();

    const bool stay = hkc_read32(MAILBOX_STAY) == HKC_MAILBOX_STAY_MAGIC;
    hkc_write32(MAILBOX_STAY, 0U);
    uint32_t reset_cause = (uint32_t)SATLINK_HK_RESET_POWER_ON;
    if (hkc_wdt_caused_reset())
    {
        reset_cause = (uint32_t)SATLINK_HK_RESET_WATCHDOG;
    }
    else if (stay)
    {
        reset_cause = (uint32_t)SATLINK_HK_RESET_COMMAND;
    }
    hkc_write32(MAILBOX_RESET_CAUSE, reset_cause);

    hkc_uart_puts("\nSatLink-Z7 HKC bootloader ");
    hkc_uart_put_dec(HKC_BOOTLOADER_VERSION_MAJOR);
    hkc_uart_putc('.');
    hkc_uart_put_dec(HKC_BOOTLOADER_VERSION_MINOR);
    hkc_uart_puts("\n");

    satlink_mcp2515_timing_t timing;
    satlink_mcp2515_t can;
    if ((satlink_mcp2515_bit_timing(HKC_MCP2515_OSC_HZ, HKC_CAN_BITRATE, &timing) != SATLINK_OK) ||
        (satlink_mcp2515_init(&can, &hkc_spi_xfer, NULL, &timing, SATLINK_MCP2515_NORMAL) !=
         SATLINK_OK))
    {
        fatal("MCP2515 init failed");
    }

    satlink_canboot_target_t session;
    (void)satlink_canboot_target_init(&session, (uint8_t *)(uintptr_t)SATLINK_HKC_LMB_APP_BASE,
                                      SATLINK_HKC_LMB_APP_BASE, SATLINK_HKC_LMB_APP_SIZE,
                                      HKC_BOOTLOADER_VERSION_MAJOR, HKC_BOOTLOADER_VERSION_MINOR);
    hkc_uart_puts(session.image_valid ? "valid application in BRAM\n" : "no application\n");

    satlink_can_frame_t frame;
    satlink_can_frame_t response;
    const uint8_t ping = (uint8_t)SATLINK_CANBOOT_OP_PING;
    (void)satlink_canboot_encode_request(ping, NULL, 0U, &frame);
    (void)satlink_canboot_target_handle(&session, &frame, &response);
    (void)satlink_mcp2515_send(&can, &response);

    bool talked = false;
    uint32_t idle_polls = 0U;
    for (;;)
    {
        if (satlink_mcp2515_receive(&can, &frame) == SATLINK_OK)
        {
            if (satlink_canboot_target_handle(&session, &frame, &response))
            {
                talked = true;
                while (satlink_mcp2515_send(&can, &response) == SATLINK_ERR_FULL)
                {
                }
            }
        }
        else
        {
            ++idle_polls;
        }

        if (session.boot_requested ||
            (!stay && !talked && session.image_valid && (idle_polls >= AUTOBOOT_POLLS)))
        {
            hkc_uart_puts("starting application at ");
            hkc_uart_put_hex32(session.entry);
            hkc_uart_puts("\n");
            /* Let the last response leave the MCP2515 before the application re-initialises it. */
            for (uint32_t i = 0U; i < 1000U; ++i)
            {
                uint8_t ctrl = 0U;
                (void)satlink_mcp2515_read_reg(&can, MCP2515_REG_TXB0CTRL, &ctrl);
                if ((ctrl & MCP2515_TXBCTRL_TXREQ) == 0U)
                {
                    break;
                }
            }
            hkc_cpu_jump(session.entry);
        }
    }
}
