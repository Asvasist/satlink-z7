/**
 * @file ipc.c
 * @brief Shared-memory IPC with Linux (ADR-0005): control block, two rings, software-pended GIC
 *        SPIs as doorbells.
 *
 * @implements SRS-AMP-002
 */
#include "rtos/ipc.h"

#include "satlink/amp/ipc_ring.h"
#include "satlink/amp/msg.h"

#include "rtos/bsp.h"
#include "version.h"

#define SHM_BASE ((uintptr_t)SATLINK_DDR_IPC_SHM_BASE)
#define CTRL     ((volatile satlink_shm_ctrl_t *)(SHM_BASE + SATLINK_SHM_CTRL_OFFSET))

static satlink_ring_t g_to_rtos;    /* consumer */
static satlink_ring_t g_to_linux;   /* producer */
static satlink_ring_t g_sa_to_rtos; /* standalone: producer handle of the same memory */
static satlink_ring_t g_sa_to_linux;
static bool g_standalone;
static TaskHandle_t g_irq_task;
static uint32_t g_irq_bit;

static void publish_fault(uint32_t code, uint32_t addr)
{
    CTRL->fault_code = code;
    CTRL->fault_addr = addr;
    CTRL->rtos_state = SATLINK_RTOS_STATE_FAULT;
    if (!g_standalone)
    {
        bsp_gic_set_pending(CTRL->irq_to_linux);
    }
}

bool ipc_init(void)
{
    void *to_rtos = (void *)(SHM_BASE + SATLINK_SHM_TO_RTOS_OFFSET);
    void *to_linux = (void *)(SHM_BASE + SATLINK_SHM_TO_LINUX_OFFSET);

    g_standalone = (CTRL->magic != SATLINK_SHM_MAGIC) || (CTRL->version != SATLINK_SHM_VERSION);
    if (g_standalone)
    {
        CTRL->magic = SATLINK_SHM_MAGIC;
        CTRL->version = SATLINK_SHM_VERSION;
        CTRL->boot_count = 1U;
        CTRL->irq_to_rtos = SATLINK_PS_MPCORE_IRQ_IPC_TO_RTOS;
        CTRL->irq_to_linux = SATLINK_PS_MPCORE_IRQ_IPC_TO_LINUX;
        (void)satlink_ring_init(&g_sa_to_rtos, to_rtos, SATLINK_SHM_RING_BYTES);
        (void)satlink_ring_init(&g_sa_to_linux, to_linux, SATLINK_SHM_RING_BYTES);
    }
    (void)satlink_ring_attach(&g_to_rtos, to_rtos, SATLINK_SHM_RING_BYTES);
    (void)satlink_ring_attach(&g_to_linux, to_linux, SATLINK_SHM_RING_BYTES);
    CTRL->fault_code = 0U;
    CTRL->fault_addr = 0U;
    CTRL->heartbeat = 0U;
    CTRL->fw_version = ((uint32_t)SATLINK_RTOS_VERSION_MAJOR << 16U) |
                       ((uint32_t)SATLINK_RTOS_VERSION_MINOR << 8U) |
                       (uint32_t)SATLINK_RTOS_VERSION_PATCH;
    bsp_set_fault_hook(&publish_fault);
    return g_standalone;
}

static void ipc_isr(void *ctx)
{
    (void)ctx;
    BaseType_t woken = pdFALSE;
    if (g_irq_task != NULL)
    {
        xTaskNotifyFromISR(g_irq_task, g_irq_bit, eSetBits, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void ipc_attach_irq(TaskHandle_t task, uint32_t bit)
{
    g_irq_task = task;
    g_irq_bit = bit;
    if (!g_standalone)
    {
        bsp_gic_attach(CTRL->irq_to_rtos, 20U, true, &ipc_isr, NULL);
        bsp_gic_enable(CTRL->irq_to_rtos);
    }
}

void ipc_set_state(uint32_t state)
{
    CTRL->rtos_state = state;
}

void ipc_heartbeat(void)
{
    CTRL->heartbeat = CTRL->heartbeat + 1U;
}

satlink_status_t ipc_send(uint16_t type, const uint8_t *payload, uint16_t len)
{
    taskENTER_CRITICAL();
    const int rc = satlink_ring_write(&g_to_linux, type, payload, len);
    taskEXIT_CRITICAL();
    if (rc != SATLINK_RING_OK)
    {
        return (rc == SATLINK_RING_FULL) ? SATLINK_ERR_FULL : SATLINK_ERR_IO;
    }
    if (!g_standalone)
    {
        bsp_gic_set_pending(CTRL->irq_to_linux);
    }
    return SATLINK_OK;
}

satlink_status_t ipc_receive(uint16_t *type, uint8_t *payload, uint16_t *len)
{
    const int rc = satlink_ring_read(&g_to_rtos, type, NULL, payload, len);
    if (rc == SATLINK_RING_EMPTY)
    {
        return SATLINK_ERR_EMPTY;
    }
    return (rc == SATLINK_RING_OK) ? SATLINK_OK : SATLINK_ERR_IO;
}

satlink_status_t ipc_standalone_send_to_rtos(uint16_t type, const uint8_t *payload, uint16_t len)
{
    taskENTER_CRITICAL();
    const int rc = satlink_ring_write(&g_sa_to_rtos, type, payload, len);
    taskEXIT_CRITICAL();
    if (rc == SATLINK_RING_OK)
    {
        if (g_irq_task != NULL)
        {
            (void)xTaskNotify(g_irq_task, g_irq_bit, eSetBits);
        }
        return SATLINK_OK;
    }
    return SATLINK_ERR_FULL;
}

satlink_status_t ipc_standalone_receive_from_rtos(uint16_t *type, uint8_t *payload, uint16_t *len)
{
    const int rc = satlink_ring_read(&g_sa_to_linux, type, NULL, payload, len);
    if (rc == SATLINK_RING_EMPTY)
    {
        return SATLINK_ERR_EMPTY;
    }
    return (rc == SATLINK_RING_OK) ? SATLINK_OK : SATLINK_ERR_IO;
}
