/**
 * @file main.c
 * @brief Housekeeping application: monitors the die temperature and supplies, supervises its own
 *        tasks with the watchdog, broadcasts telemetry on CAN and restarts into the bootloader on
 *        request.
 *
 * Cooperative loop, polled, no interrupts:
 *
 *   every 10 ms   read the CAN controller (task CAN); an ENTER command restarts into the
 *                 bootloader so a new application can be uploaded
 *   every 200 ms  sample the XADC and evaluate the limits (task MEASURE)
 *   every 100 ms  advance the watchdog supervisor; the hardware watchdog is kicked only in a
 *                 window in which both tasks reported in
 *   every 1 s     send the latest snapshot as three CAN frames and print it on the console
 *
 * The limits below are provisional; check them against the Zynq-7000 datasheet (recommended
 * operating conditions) during bring-up.
 *
 * @implements SRS-HKC-001
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/boot/can_boot.h"
#include "satlink/can/mcp2515.h"
#include "satlink/hk/limits.h"
#include "satlink/hk/supervisor.h"
#include "satlink/hk/telemetry.h"
#include "satlink/hk/xadc.h"
#include "satlink/regs/address_map.h"

#include "hkc_platform.h"

#define CAN_POLL_MS      (10U)
#define SAMPLE_MS        (200U)
#define WDG_TICK_MS      (100U)
#define WDG_WINDOW_TICKS (5U) /* 500 ms: both tasks must report in within this */
#define TELEMETRY_MS     (1000U)
#define CAN_RETRY_MS     (1000U)

#define TASK_MEASURE (0U)
#define TASK_CAN     (1U)

/* Die temperature, millidegrees Celsius. */
#define TEMP_WARN_MDEGC  (75000)
#define TEMP_ALARM_MDEGC (85000)
#define TEMP_HYST_MDEGC  (3000)

/* Supplies, millivolts: a warning at 4 % and an alarm at 6 % below nominal. */
#define VCCINT_WARN_MV   (960)
#define VCCINT_ALARM_MV  (940)
#define VCCAUX_WARN_MV   (1728)
#define VCCAUX_ALARM_MV  (1692)
#define VCCBRAM_WARN_MV  (960)
#define VCCBRAM_ALARM_MV (940)
#define SUPPLY_HYST_MV   (10)

typedef struct
{
    satlink_hk_limit_t temp;
    satlink_hk_limit_t vccint;
    satlink_hk_limit_t vccaux;
    satlink_hk_limit_t vccbram;
} limits_t;

static void init_limits(limits_t *limits)
{
    (void)satlink_hk_limit_init(&limits->temp, SATLINK_HK_LIMIT_HIGH, TEMP_WARN_MDEGC,
                                TEMP_ALARM_MDEGC, TEMP_HYST_MDEGC);
    (void)satlink_hk_limit_init(&limits->vccint, SATLINK_HK_LIMIT_LOW, VCCINT_WARN_MV,
                                VCCINT_ALARM_MV, SUPPLY_HYST_MV);
    (void)satlink_hk_limit_init(&limits->vccaux, SATLINK_HK_LIMIT_LOW, VCCAUX_WARN_MV,
                                VCCAUX_ALARM_MV, SUPPLY_HYST_MV);
    (void)satlink_hk_limit_init(&limits->vccbram, SATLINK_HK_LIMIT_LOW, VCCBRAM_WARN_MV,
                                VCCBRAM_ALARM_MV, SUPPLY_HYST_MV);
}

static void sample(satlink_hk_snapshot_t *snap, limits_t *limits)
{
    snap->temp_mdegc = satlink_xadc_temp_mdegc(hkc_xadc_read(HKC_XADC_TEMP));
    snap->vccint_mv = satlink_xadc_supply_mv(hkc_xadc_read(HKC_XADC_VCCINT));
    snap->vccaux_mv = satlink_xadc_supply_mv(hkc_xadc_read(HKC_XADC_VCCAUX));
    snap->vccbram_mv = satlink_xadc_supply_mv(hkc_xadc_read(HKC_XADC_VCCBRAM));

    snap->temp_level = satlink_hk_limit_update(&limits->temp, snap->temp_mdegc);
    snap->vccint_level = satlink_hk_limit_update(&limits->vccint, (int32_t)snap->vccint_mv);
    snap->vccaux_level = satlink_hk_limit_update(&limits->vccaux, (int32_t)snap->vccaux_mv);
    snap->vccbram_level = satlink_hk_limit_update(&limits->vccbram, (int32_t)snap->vccbram_mv);
}

static const char *level_name(satlink_hk_level_t level)
{
    const char *name = "OK";

    if (level == SATLINK_HK_ALARM)
    {
        name = "ALARM";
    }
    else if (level == SATLINK_HK_WARN)
    {
        name = "WARN";
    }
    else
    {
        name = "OK";
    }

    return name;
}

static void print_status(const satlink_hk_snapshot_t *snap)
{
    const int32_t t = snap->temp_mdegc;
    const uint32_t magnitude = (t < 0) ? (uint32_t)(-t) : (uint32_t)t;

    hkc_uart_puts("T=");
    if (t < 0)
    {
        hkc_uart_puts("-");
    }
    hkc_uart_put_dec(magnitude / 1000U);
    hkc_uart_puts(".");
    hkc_uart_put_dec((magnitude % 1000U) / 100U);
    hkc_uart_puts(" C ");
    hkc_uart_puts(level_name(snap->temp_level));
    hkc_uart_puts("  VCCINT=");
    hkc_uart_put_dec(snap->vccint_mv);
    hkc_uart_puts(" mV ");
    hkc_uart_puts(level_name(snap->vccint_level));
    hkc_uart_puts("  VCCAUX=");
    hkc_uart_put_dec(snap->vccaux_mv);
    hkc_uart_puts(" mV ");
    hkc_uart_puts(level_name(snap->vccaux_level));
    hkc_uart_puts("  VCCBRAM=");
    hkc_uart_put_dec(snap->vccbram_mv);
    hkc_uart_puts(" mV ");
    hkc_uart_puts(level_name(snap->vccbram_level));
    hkc_uart_puts("\n");
}

static void send_snapshot(const satlink_mcp2515_t *can, const satlink_hk_snapshot_t *snap,
                          uint8_t seq)
{
    satlink_can_frame_t frames[SATLINK_HK_FRAME_COUNT];

    if (satlink_hk_pack(snap, seq, frames) == SATLINK_OK)
    {
        for (size_t i = 0U; i < SATLINK_HK_FRAME_COUNT; ++i)
        {
            (void)hkc_can_send(can, &frames[i]);
        }
    }
}

/** Restart into the bootloader: acknowledge, let the frame leave, then jump to the reset vector. */
static void enter_bootloader(const satlink_mcp2515_t *can)
{
    satlink_can_frame_t rsp = {.id = SATLINK_CANBOOT_ID_RSP,
                               .dlc = 8U,
                               .extended = false,
                               .rtr = false,
                               .data = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}};

    rsp.data[0] = (uint8_t)SATLINK_CANBOOT_RSP_ACK;
    rsp.data[1] = (uint8_t)SATLINK_CANBOOT_OP_ENTER;
    (void)hkc_can_send(can, &rsp);
    hkc_delay_us(2000U);
    hkc_uart_puts("restarting into the bootloader\n");
    hkc_jump((uintptr_t)SATLINK_HKC_LMB_BOOTLOADER_BASE);
}

static void service_can(const satlink_mcp2515_t *can)
{
    satlink_can_frame_t frame;

    while (satlink_mcp2515_receive(can, &frame) == SATLINK_OK)
    {
        if ((frame.id == SATLINK_CANBOOT_ID_CMD) && (!frame.extended) && (!frame.rtr) &&
            (frame.dlc >= 1U) && (frame.data[0] == (uint8_t)SATLINK_CANBOOT_OP_ENTER))
        {
            enter_bootloader(can);
        }
    }
}

int main(void)
{
    satlink_mcp2515_t can;
    satlink_wdg_sup_t supervisor;
    limits_t limits;
    satlink_hk_snapshot_t snap = {
        0, 0U, 0U, 0U, SATLINK_HK_OK, SATLINK_HK_OK, SATLINK_HK_OK, SATLINK_HK_OK, 0U, false};
    bool can_ready = false;
    uint8_t seq = 0U;
    uint32_t last_can = 0U;
    uint32_t last_sample = 0U;
    uint32_t last_wdg = 0U;
    uint32_t last_telemetry = 0U;
    uint32_t last_can_try = 0U;

    hkc_uart_init();
    hkc_timer_init();
    snap.watchdog_reset = hkc_wdt_reset_was_watchdog(); /* read before the watchdog is restarted */
    hkc_wdt_start();

    hkc_uart_puts("\nSatLink hkc application\n");
    if (snap.watchdog_reset)
    {
        hkc_uart_puts("the last reset was the watchdog\n");
    }

    init_limits(&limits);
    (void)satlink_wdg_sup_init(&supervisor, (1U << TASK_MEASURE) | (1U << TASK_CAN),
                               WDG_WINDOW_TICKS);
    can_ready = (hkc_can_init(&can) == SATLINK_OK);
    hkc_uart_puts(can_ready ? "CAN ready\n" : "CAN controller not answering, retrying\n");
    sample(&snap, &limits);

    for (;;)
    {
        const uint32_t now = hkc_millis();

        if ((now - last_can) >= CAN_POLL_MS)
        {
            last_can = now;
            if (can_ready)
            {
                service_can(&can);
            }
            else if ((now - last_can_try) >= CAN_RETRY_MS)
            {
                last_can_try = now;
                can_ready = (hkc_can_init(&can) == SATLINK_OK);
            }
            else
            {
                /* wait for the next retry */
            }
            satlink_wdg_sup_checkin(&supervisor, TASK_CAN);
        }

        if ((now - last_sample) >= SAMPLE_MS)
        {
            last_sample = now;
            sample(&snap, &limits);
            satlink_wdg_sup_checkin(&supervisor, TASK_MEASURE);
        }

        if ((now - last_wdg) >= WDG_TICK_MS)
        {
            last_wdg = now;
            if (satlink_wdg_sup_tick(&supervisor))
            {
                hkc_wdt_kick();
            }
        }

        if ((now - last_telemetry) >= TELEMETRY_MS)
        {
            last_telemetry = now;
            snap.uptime_s = now / 1000U;
            print_status(&snap);
            if (can_ready)
            {
                send_snapshot(&can, &snap, seq);
                seq = (uint8_t)(seq + 1U);
            }
        }
    }
}
