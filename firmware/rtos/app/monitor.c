/**
 * @file monitor.c
 * @brief Monitor task: heartbeat for Linux, CPU load, stack margins; and, when no Linux is
 *        present (QEMU, bench), a self-test that plays the Linux side through the IPC rings.
 *
 * @implements SRS-AMP-005
 * @implements SRS-AMP-007
 * @verifies SRS-AMP-004
 */
#include <string.h>

#include "satlink/amp/ipc_ring.h"
#include "satlink/amp/msg.h"

#include "rtos/bsp.h"
#include "rtos/ipc.h"
#include "rtos/tasks.h"

#define MONITOR_STACK_WORDS (1024U)
#define MONITOR_PRIORITY    (2U)
#define PERIOD_MS           (100U)
#define STACK_WARN_WORDS    (128U)
#define SELFTEST_FRAMES     (8U)
#define SELFTEST_END_MS     (15000U)

static StaticTask_t g_tcb;
static StackType_t g_stack[MONITOR_STACK_WORDS];
static bool g_standalone;
static volatile uint16_t g_load_permille;

uint16_t monitor_cpu_load(void)
{
    return g_load_permille;
}

static void update_load(void)
{
    static uint32_t last_idle;
    static uint32_t last_total;
    const uint32_t idle = ulTaskGetIdleRunTimeCounter();
    const uint32_t total = satlink_rtos_time_us();
    const uint32_t d_total = total - last_total;
    const uint32_t d_idle = idle - last_idle;
    if ((d_total > 0U) && (d_idle <= d_total))
    {
        g_load_permille = (uint16_t)(1000U - ((uint64_t)d_idle * 1000U / d_total));
    }
    last_idle = idle;
    last_total = total;
}

static void check_stacks(void)
{
    static bool warned;
    TaskStatus_t tasks[6];
    const UBaseType_t n = uxTaskGetSystemState(tasks, 6U, NULL);
    for (UBaseType_t i = 0U; (i < n) && !warned; ++i)
    {
        if (tasks[i].usStackHighWaterMark < STACK_WARN_WORDS)
        {
            bsp_printf("rtos: task %s has only %u words of stack left\n", tasks[i].pcTaskName,
                       (uint32_t)tasks[i].usStackHighWaterMark);
            warned = true;
        }
    }
}

/* ---- standalone self-test ---- */

typedef struct
{
    uint32_t rx_good;
    uint32_t rx_bad;
    uint32_t status_seen;
    satlink_msg_status_t last_status;
    bool done;
} selftest_t;

static void selftest_send_config(void)
{
    uint8_t buf[16];
    const satlink_msg_modem_config_t cfg = {true, true, 2U, (uint8_t)SATLINK_LOOP_SOFTWARE};
    const size_t n = satlink_msg_encode_modem_config(&cfg, buf, sizeof(buf));
    (void)ipc_standalone_send_to_rtos(SATLINK_MSG_MODEM_CONFIG, buf, (uint16_t)n);
    const satlink_msg_channel_t ch = {256U, 0x7FFFU}; /* sigma 1/16: Es/N0 about 24 dB */
    (void)ipc_standalone_send_to_rtos(SATLINK_MSG_CHANNEL, buf,
                                      (uint16_t)satlink_msg_encode_channel(&ch, buf, sizeof(buf)));
}

static void selftest_send_frames(void)
{
    uint8_t data[SATLINK_MSG_FRAME_BYTES];
    for (uint32_t f = 0U; f < SELFTEST_FRAMES; ++f)
    {
        for (uint32_t i = 0U; i < sizeof(data); ++i)
        {
            data[i] = (uint8_t)((f * 31U) + i);
        }
        (void)ipc_standalone_send_to_rtos(SATLINK_MSG_TX_FRAME, data, (uint16_t)sizeof(data));
    }
}

static void selftest_drain(selftest_t *st)
{
    uint8_t buf[SATLINK_RING_MAX_PAYLOAD];
    for (;;)
    {
        uint16_t type = 0U;
        uint16_t len = (uint16_t)sizeof(buf);
        if (ipc_standalone_receive_from_rtos(&type, buf, &len) != SATLINK_OK)
        {
            return;
        }
        if (type == SATLINK_MSG_RX_FRAME)
        {
            satlink_msg_rx_frame_t f;
            if ((satlink_msg_decode_rx_frame(buf, len, &f) == SATLINK_OK) && f.crc_ok)
            {
                const uint32_t idx = (uint32_t)(f.data[1] - 1U) / 31U; /* data[i] = f*31 + i */
                bool match = true;
                for (uint32_t i = 0U; i < sizeof(f.data); ++i)
                {
                    match = match && (f.data[i] == (uint8_t)((idx * 31U) + i));
                }
                st->rx_good += match ? 1U : 0U;
                st->rx_bad += match ? 0U : 1U;
            }
            else
            {
                ++st->rx_bad;
            }
        }
        else if (type == SATLINK_MSG_STATUS)
        {
            if (satlink_msg_decode_status(buf, len, &st->last_status) == SATLINK_OK)
            {
                const satlink_msg_status_t *s = &st->last_status;
                ++st->status_seen;
                bsp_printf("status t=%us lock=%u modcod=%u EsN0=%d.%02u dB frames=%u crc_err=%u "
                           "ber=%u/%u load=%u.%u%% rx_lat_max=%uus\n",
                           s->uptime_ms / 1000U, (uint32_t)s->locked, (uint32_t)s->tx_modcod,
                           s->esn0_cdb / 100,
                           (uint32_t)((s->esn0_cdb < 0 ? -s->esn0_cdb : s->esn0_cdb) % 100),
                           s->frames_ok, s->frames_crc_error, s->bit_errors, s->bits_checked,
                           (uint32_t)s->cpu_load_permille / 10U,
                           (uint32_t)s->cpu_load_permille % 10U, s->rx_latency_max_us);
            }
        }
        else if (type == SATLINK_MSG_LOG)
        {
            satlink_msg_log_t log;
            if (satlink_msg_decode_log(buf, len, &log) == SATLINK_OK)
            {
                bsp_printf("log: %s\n", log.text);
            }
        }
        else
        {
            /* PONG and others: not used by the self-test */
        }
    }
}

/* The translation table in use must follow the address map: owned regions mapped with the
 * right type, everything Linux owns unmapped. */
static bool selftest_mmu_ownership(void)
{
    typedef struct
    {
        uint32_t addr;
        bsp_mem_type_t type;
    } probe_t;
    static const probe_t k_probes[] = {
        {SATLINK_DDR_LINUX_BASE + 0x00100000U, BSP_MEM_FAULT}, /* Linux kernel memory */
        {SATLINK_DDR_RTOS_FW_BASE, BSP_MEM_NORMAL_WB},
        {SATLINK_DDR_IPC_SHM_BASE, BSP_MEM_NORMAL_NC},
        {SATLINK_DDR_MODEM_DMA_BASE, BSP_MEM_NORMAL_NC},
        {SATLINK_DDR_RESERVED_BASE, BSP_MEM_FAULT},
        {SATLINK_PS_PAYLOAD_CTRL_BASE, BSP_MEM_DEVICE},
        {SATLINK_PS_CCSDS_FRAME_ACCEL_BASE, BSP_MEM_FAULT}, /* Linux-owned, same MiB */
        {SATLINK_PS_AXI_DMA_MODEM_BASE, BSP_MEM_DEVICE},
        {SATLINK_PS_AXI_DMA_FRAME_BASE, BSP_MEM_FAULT}, /* Linux-owned, same MiB */
        {SATLINK_PS_PS_UART0_BASE, BSP_MEM_DEVICE},
        {SATLINK_PS_PS_UART1_BASE, BSP_MEM_FAULT}, /* Linux console */
        {SATLINK_PS_PS_GEM0_BASE, BSP_MEM_FAULT},
        {SATLINK_PS_MPCORE_BASE, BSP_MEM_DEVICE},
    };
    bool ok = true;
    for (uint32_t i = 0U; i < (uint32_t)(sizeof(k_probes) / sizeof(k_probes[0])); ++i)
    {
        if (bsp_mmu_type_of(k_probes[i].addr) != k_probes[i].type)
        {
            bsp_printf("mmu: 0x%08x mapped as %u, expected %u\n", k_probes[i].addr,
                       (uint32_t)bsp_mmu_type_of(k_probes[i].addr), (uint32_t)k_probes[i].type);
            ok = false;
        }
    }
    return ok;
}

static void selftest_step(selftest_t *st, uint32_t now_ms)
{
    static bool configured;
    static bool queued;
    if (!configured && (now_ms >= 500U))
    {
        selftest_send_config();
        configured = true;
    }
    if (!queued && (now_ms >= 3000U))
    {
        selftest_send_frames();
        queued = true;
    }
    selftest_drain(st);
    if (!st->done && (now_ms >= SELFTEST_END_MS))
    {
        st->done = true;
        const satlink_msg_status_t *s = &st->last_status;
        const bool mmu_ok = selftest_mmu_ownership();
        bsp_printf("mmu: ownership check %s\n", mmu_ok ? "ok" : "FAILED");
        const bool pass = mmu_ok && (st->rx_good == SELFTEST_FRAMES) && (st->rx_bad == 0U) &&
                          (s->locked != 0U) && (s->bit_errors == 0U) && (s->bits_checked > 0U) &&
                          (st->status_seen >= 10U);
        bsp_printf("SELFTEST %s: data frames %u/%u, bad %u, idle bits %u with %u errors\n",
                   pass ? "PASS" : "FAIL", st->rx_good, SELFTEST_FRAMES, st->rx_bad,
                   s->bits_checked, s->bit_errors);
    }
}

static void monitor_task(void *arg)
{
    (void)arg;
    selftest_t st;
    (void)memset(&st, 0, sizeof(st));
    TickType_t wake = xTaskGetTickCount();
    uint32_t count = 0U;
    for (;;)
    {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(PERIOD_MS));
        ipc_heartbeat();
        ++count;
        if ((count % 10U) == 0U)
        {
            update_load();
            check_stacks();
        }
        if (g_standalone)
        {
            selftest_step(&st, (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS);
        }
    }
}

void monitor_task_create(bool standalone)
{
    g_standalone = standalone;
    (void)xTaskCreateStatic(&monitor_task, "monitor", MONITOR_STACK_WORDS, NULL, MONITOR_PRIORITY,
                            g_stack, &g_tcb);
}
