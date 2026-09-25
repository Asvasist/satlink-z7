/**
 * @file modem_task.c
 * @brief The modem task: owns satlink_modem_app and moves data between the IPC rings, the
 *        modem library and the PL (or the software loopback).
 *
 * @implements SRS-AMP-001
 * @implements SRS-MDM-007
 */
#include "satlink/amp/ipc_ring.h"
#include "satlink/amp/msg.h"
#include "satlink/modem_app/modem_app.h"

#include "rtos/bsp.h"
#include "rtos/ipc.h"
#include "rtos/payload.h"
#include "rtos/tasks.h"

#define LOOP_PERIOD_MS    (10U)
#define SYMBOLS_PER_MS    (6U) /* 6 kSym/s */
#define MODEM_STACK_WORDS (4096U)
#define MODEM_PRIORITY    (5U)

static satlink_modem_app_t g_app;
static StaticTask_t g_tcb;
static StackType_t g_stack[MODEM_STACK_WORDS];
static bool g_standalone;
static bool g_pl_present;
static bool g_hw_running;

static satlink_status_t send_msg(void *ctx, uint16_t type, const uint8_t *payload, uint16_t len)
{
    (void)ctx;
    return ipc_send(type, payload, len);
}

static void set_channel(void *ctx, uint16_t noise_level, uint16_t gain_q15)
{
    (void)ctx;
    if (g_pl_present)
    {
        payload_set_channel(noise_level, gain_q15);
    }
}

static void fill_tx(void *ctx, satlink_cf_t *symbols, uint32_t count)
{
    satlink_modem_app_tx_symbols((satlink_modem_app_t *)ctx, symbols, count);
}

static void sink_rx(void *ctx, const satlink_cf_t *samples, uint32_t count)
{
    const uint64_t t0 = bsp_time_ticks();
    satlink_modem_app_rx_samples((satlink_modem_app_t *)ctx, samples, count);
    satlink_modem_app_note_latency((satlink_modem_app_t *)ctx,
                                   bsp_ticks_to_us(bsp_time_ticks() - t0));
}

static void apply_mode(void)
{
    const bool want_hw = g_pl_present && (g_app.config.loopback != (uint8_t)SATLINK_LOOP_SOFTWARE);
    if (want_hw && !g_hw_running)
    {
        payload_start(xTaskGetCurrentTaskHandle(), MODEM_EVT_TX, MODEM_EVT_RX);
        g_hw_running = true;
    }
    else if (!want_hw && g_hw_running)
    {
        payload_stop();
        g_hw_running = false;
    }
    if (g_hw_running)
    {
        payload_set_digital_loopback(g_app.config.loopback == (uint8_t)SATLINK_LOOP_DIGITAL);
    }
}

static void drain_ipc(void)
{
    uint8_t buf[SATLINK_RING_MAX_PAYLOAD];
    for (;;)
    {
        uint16_t type = 0U;
        uint16_t len = (uint16_t)sizeof(buf);
        const satlink_status_t rc = ipc_receive(&type, buf, &len);
        if (rc != SATLINK_OK)
        {
            if (rc == SATLINK_ERR_IO)
            {
                bsp_printf("rtos: IPC ring corrupt\n");
                satlink_fault(BSP_FAULT_ASSERT, 0U);
            }
            return;
        }
        satlink_modem_app_on_msg(&g_app, type, buf, len);
    }
}

static void modem_task(void *arg)
{
    (void)arg;
    const satlink_modem_app_hw_t hw = {NULL, &send_msg, &set_channel, NULL, NULL};
    (void)satlink_modem_app_init(&g_app, &hw);
    if (g_standalone)
    {
        g_app.config.loopback = (uint8_t)SATLINK_LOOP_SOFTWARE;
    }
    g_pl_present = !g_standalone && payload_probe();
    if (!g_standalone && !g_pl_present)
    {
        /* Without the PL datapath only the software loopback can run. */
        g_app.config.loopback = (uint8_t)SATLINK_LOOP_SOFTWARE;
    }
    ipc_set_state(SATLINK_RTOS_STATE_RUNNING);
    bsp_printf("rtos: modem running (%s)\n", g_pl_present ? "PL datapath" : "software loopback");

    TickType_t last = xTaskGetTickCount();
    for (;;)
    {
        uint32_t events = 0U;
        (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &events, pdMS_TO_TICKS(LOOP_PERIOD_MS));
        drain_ipc();
        apply_mode();

        const TickType_t now = xTaskGetTickCount();
        if (g_hw_running)
        {
            payload_service_tx(&fill_tx, &g_app);
            payload_service_rx(&sink_rx, &g_app);
        }
        else if (g_app.config.loopback == (uint8_t)SATLINK_LOOP_SOFTWARE)
        {
            /* Real-time pacing: produce what 6 kSym/s would have produced since last time. */
            const uint32_t elapsed_ms = (uint32_t)(now - last) * portTICK_PERIOD_MS;
            if (elapsed_ms > 0U)
            {
                const uint64_t t0 = bsp_time_ticks();
                satlink_modem_app_run_loopback(&g_app, (size_t)elapsed_ms * SYMBOLS_PER_MS);
                satlink_modem_app_note_latency(&g_app, bsp_ticks_to_us(bsp_time_ticks() - t0));
            }
        }
        last = now;
        satlink_modem_app_note_platform(&g_app, monitor_cpu_load(),
                                        g_pl_present ? payload_underruns() : 0U,
                                        g_pl_present ? payload_overruns() : 0U);
        satlink_modem_app_tick(&g_app, (uint32_t)now * portTICK_PERIOD_MS);
    }
}

TaskHandle_t modem_task_create(bool standalone)
{
    g_standalone = standalone;
    return xTaskCreateStatic(&modem_task, "modem", MODEM_STACK_WORDS, NULL, MODEM_PRIORITY, g_stack,
                             &g_tcb);
}
