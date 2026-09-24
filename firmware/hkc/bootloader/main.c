/**
 * @file main.c
 * @brief Housekeeping bootloader: takes the application over CAN and starts it.
 *
 * Runs at the reset vector, in the 16 KiB at the bottom of the LMB memory. After a reset of the
 * housekeeping subsystem the application region still holds whatever was there, so:
 *
 *   - within AUTOBOOT_WAIT_MS the bootloader listens for an upload (see satlink/boot/can_boot.h);
 *   - if none starts and the image in memory passes its CRC check, that image is started;
 *   - otherwise it stays here and waits for an upload.
 *
 * A finished upload is only started on the host's BOOT command, and only after its header has
 * been validated. The watchdog is kicked from the main loop.
 *
 * @implements SRS-HKC-003
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/boot/app_header.h"
#include "satlink/boot/can_boot.h"
#include "satlink/can/mcp2515.h"
#include "satlink/regs/address_map.h"

#include "hkc_platform.h"

#define APP_BASE (SATLINK_HKC_LMB_APP_BASE)
#define APP_SIZE (SATLINK_HKC_LMB_APP_SIZE)

#define AUTOBOOT_WAIT_MS (2000U)
#define TRANSFER_IDLE_MS (3000U)
#define CAN_RETRY_MS     (1000U)

static satlink_status_t store_image(void *ctx, uint32_t offset, const uint8_t *data, size_t len)
{
    satlink_status_t status = SATLINK_ERR_RANGE;

    (void)ctx;
    if (((size_t)offset + len) <= (size_t)APP_SIZE)
    {
        volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)(APP_BASE + offset);

        for (size_t i = 0U; i < len; ++i)
        {
            dst[i] = data[i];
        }
        status = SATLINK_OK;
    }

    return status;
}

/** Start the application if the image in memory is valid. Returns only if it is not. */
static void try_start_application(void)
{
    size_t entry = 0U;
    const satlink_status_t status =
        satlink_app_validate((const uint8_t *)(uintptr_t)APP_BASE, (size_t)APP_SIZE, &entry);

    if (status == SATLINK_OK)
    {
        hkc_uart_puts("starting application at ");
        hkc_uart_put_hex32((uint32_t)(APP_BASE + entry));
        hkc_uart_puts("\n");
        hkc_jump((uintptr_t)(APP_BASE + entry));
    }

    if (status == SATLINK_ERR_STATE)
    {
        hkc_uart_puts("no application in memory\n");
    }
    else if (status == SATLINK_ERR_CRC)
    {
        hkc_uart_puts("application in memory fails its CRC check\n");
    }
    else
    {
        hkc_uart_puts("application header is not valid\n");
    }
}

static void reset_receiver(satlink_canboot_rx_t *rx)
{
    const satlink_canboot_rx_config_t cfg = {
        .write = store_image, .ctx = NULL, .max_size = (uint32_t)APP_SIZE, .window = 0U};

    (void)satlink_canboot_rx_init(rx, &cfg);
}

/** Answer every command frame that is waiting; returns true if there was any traffic. */
static bool service_can(const satlink_mcp2515_t *can, satlink_canboot_rx_t *rx)
{
    bool traffic = false;
    satlink_can_frame_t frame;

    while (satlink_mcp2515_receive(can, &frame) == SATLINK_OK)
    {
        satlink_can_frame_t rsp;
        bool have_rsp = false;

        traffic = true;
        if ((satlink_canboot_rx_handle(rx, &frame, &rsp, &have_rsp) == SATLINK_OK) && have_rsp)
        {
            (void)hkc_can_send(can, &rsp);
        }
    }

    return traffic;
}

int main(void)
{
    satlink_mcp2515_t can;
    satlink_canboot_rx_t rx;
    bool can_ready = false;
    bool autoboot_pending = true;
    uint32_t last_activity = 0U;
    uint32_t last_can_try = 0U;

    hkc_uart_init();
    hkc_timer_init();
    hkc_uart_puts("\nSatLink hkc bootloader, protocol ");
    hkc_uart_put_dec(SATLINK_CANBOOT_VERSION);
    hkc_uart_puts("\n");
    hkc_wdt_start();

    reset_receiver(&rx);
    can_ready = (hkc_can_init(&can) == SATLINK_OK);
    hkc_uart_puts(can_ready ? "CAN ready\n" : "CAN controller not answering, retrying\n");

    for (;;)
    {
        const uint32_t now = hkc_millis();

        hkc_wdt_kick();

        if ((!can_ready) && ((now - last_can_try) >= CAN_RETRY_MS))
        {
            last_can_try = now;
            can_ready = (hkc_can_init(&can) == SATLINK_OK);
            if (can_ready)
            {
                hkc_uart_puts("CAN ready\n");
            }
        }

        if (can_ready && service_can(&can, &rx))
        {
            last_activity = now;
        }

        if (satlink_canboot_rx_boot_requested(&rx))
        {
            try_start_application(); /* returns only if the header is not valid */
            reset_receiver(&rx);
        }

        if ((satlink_canboot_rx_state(&rx) == SATLINK_CANBOOT_RX_RECEIVING) &&
            ((now - last_activity) > TRANSFER_IDLE_MS))
        {
            hkc_uart_puts("upload timed out\n");
            reset_receiver(&rx);
        }

        if (autoboot_pending && (satlink_canboot_rx_state(&rx) != SATLINK_CANBOOT_RX_RECEIVING) &&
            (now >= AUTOBOOT_WAIT_MS))
        {
            autoboot_pending = false;
            try_start_application();
            hkc_uart_puts("waiting for an upload\n");
        }
    }
}
