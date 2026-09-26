/**
 * @file main.c
 * @brief MicroBlaze V housekeeping application: hardware glue around hkc_app.
 *
 * Interrupt-driven: the AXI Timer raises a 1 kHz tick and the MCP2515 INT pin (AXI INTC input
 * 2) signals received frames. The main loop sleeps in WFI between events, drains the CAN
 * receive buffers, runs the application logic and kicks the watchdog.
 *
 * @implements SRS-HKC-005
 * @implements SRS-HKC-001
 */
#include <stdbool.h>
#include <stdint.h>

#include "satlink/drivers/mcp2515.h"
#include "satlink/hkc/hk_proto.h"

#include "hkc/board.h"
#include "hkc/cpu.h"
#include "hkc/drivers.h"
#include "hkc/hkc_app.h"
#include "hkc/mmio.h"

#define IRQ_BIT(n) (1UL << (uint32_t)(n))
#define IRQ_TIMER  IRQ_BIT(SATLINK_HKC_HKC_TIMER_IRQ_TIMER)
#define IRQ_CAN    IRQ_BIT(SATLINK_HKC_HKC_SPI_IRQ_CAN2_INT)

int main(void);

static volatile uint32_t g_now_ms;
static volatile bool g_can_event;
static satlink_mcp2515_t g_can;

void hkc_trap_handler(uint32_t mcause, uint32_t mepc)
{
    if (mcause == (HKC_MCAUSE_INTERRUPT | HKC_MCAUSE_MEI))
    {
        const uint32_t pending = hkc_intc_pending();
        if ((pending & IRQ_TIMER) != 0U)
        {
            hkc_timer_ack();
            g_now_ms = g_now_ms + 1U;
        }
        if ((pending & IRQ_CAN) != 0U)
        {
            g_can_event = true;
        }
        hkc_intc_ack(pending);
        return;
    }
    /* Any exception is a firmware bug: report it and let the watchdog reset the subsystem. */
    hkc_uart_puts("\napp: exception mcause=");
    hkc_uart_put_hex32(mcause);
    hkc_uart_puts(" mepc=");
    hkc_uart_put_hex32(mepc);
    hkc_uart_puts("\n");
    for (;;)
    {
    }
}

static uint16_t read_sensor(void *ctx, hkc_sensor_t sensor)
{
    (void)ctx;
    static const hkc_xadc_channel_t k_channels[] = {
        HKC_XADC_TEMP,    HKC_XADC_VCCINT,  HKC_XADC_VCCAUX, HKC_XADC_VBRAM,
        HKC_XADC_VCCPINT, HKC_XADC_VCCPAUX, HKC_XADC_VCCODDR};
    return hkc_xadc_read(k_channels[(uint32_t)sensor]);
}

static bool sensor_alarm(void *ctx)
{
    (void)ctx;
    return hkc_xadc_alarm();
}

static uint8_t switches(void *ctx)
{
    (void)ctx;
    return hkc_gpio_switches();
}

static void set_rgb(void *ctx, uint8_t rgb)
{
    (void)ctx;
    hkc_gpio_set_rgb(rgb);
}

static satlink_status_t send(void *ctx, const satlink_can_frame_t *frame)
{
    (void)ctx;
    /* TXB0 frees up within one frame time (~250 us at 500 kbit/s); bound the wait. */
    satlink_status_t status = SATLINK_ERR_FULL;
    for (uint32_t i = 0U; (i < 2000U) && (status == SATLINK_ERR_FULL); ++i)
    {
        status = satlink_mcp2515_send(&g_can, frame);
    }
    return status;
}

static void enter_bootloader(void *ctx)
{
    (void)ctx;
    hkc_uart_puts("app: entering bootloader\n");
    hkc_write32(HKC_MAILBOX_ADDR, HKC_MAILBOX_STAY_MAGIC);
    hkc_cpu_jump(SATLINK_HKC_LMB_BOOTLOADER_BASE);
}

int main(void)
{
    hkc_gpio_init();
    hkc_spi_init();

    const uint8_t reset_cause = (uint8_t)hkc_read32(HKC_MAILBOX_ADDR + 4U);
    hkc_uart_puts("\nSatLink-Z7 HKC application ");
    hkc_uart_put_dec(HKC_APP_VERSION_MAJOR);
    hkc_uart_putc('.');
    hkc_uart_put_dec(HKC_APP_VERSION_MINOR);
    hkc_uart_putc('.');
    hkc_uart_put_dec(HKC_APP_VERSION_PATCH);
    hkc_uart_puts(", reset cause ");
    hkc_uart_put_dec(reset_cause);
    hkc_uart_puts("\n");

    satlink_mcp2515_timing_t timing;
    if ((satlink_mcp2515_bit_timing(HKC_MCP2515_OSC_HZ, HKC_CAN_BITRATE, &timing) != SATLINK_OK) ||
        (satlink_mcp2515_init(&g_can, &hkc_spi_xfer, NULL, &timing, SATLINK_MCP2515_NORMAL) !=
         SATLINK_OK))
    {
        hkc_uart_puts("app: MCP2515 init failed\n");
    }

    const hkc_app_hw_t hw = {NULL,     &read_sensor, &sensor_alarm,    &switches,
                             &set_rgb, &send,        &enter_bootloader};
    hkc_app_t app;
    (void)hkc_app_init(&app, &hw, reset_cause);

    hkc_intc_init(IRQ_TIMER | IRQ_CAN);
    hkc_timer_start_tick();
    hkc_cpu_enable_external_irq();
    hkc_wdt_enable();

    uint32_t last_ms = 0xFFFFFFFFUL;
    for (;;)
    {
        satlink_can_frame_t frame;
        satlink_status_t rx = SATLINK_OK;
        g_can_event = false;
        while (rx != SATLINK_ERR_EMPTY)
        {
            rx = satlink_mcp2515_receive(&g_can, &frame);
            if (rx == SATLINK_OK)
            {
                hkc_app_on_frame(&app, &frame, g_now_ms);
            }
            else if (rx != SATLINK_ERR_RANGE)
            {
                break; /* SPI error: try again on the next tick */
            }
        }
        if (g_can.rx_overruns != 0U)
        {
            app.error_flags |= (uint8_t)SATLINK_HK_ERR_CAN_RX_OVR;
            g_can.rx_overruns = 0U;
        }

        const uint32_t now = g_now_ms;
        if (now != last_ms)
        {
            last_ms = now;
            hkc_app_poll(&app, now);
            hkc_wdt_kick();
        }
        if (!g_can_event)
        {
            hkc_cpu_wait_for_interrupt();
        }
    }
}
