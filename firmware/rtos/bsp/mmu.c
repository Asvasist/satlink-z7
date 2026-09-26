/**
 * @file mmu.c
 * @brief Translation table that enforces the AMP ownership of icd/address_map.yaml on Core 1.
 *
 * Only what the address map gives to "rtos" (or "shared") is mapped; everything else is a
 * translation fault, so a stray pointer into Linux memory or a Linux-owned peripheral aborts
 * instead of silently corrupting the other OS. 1 MiB sections where a whole megabyte has one
 * type, 4 KiB small pages (second-level tables) where the PL and PS peripherals of different
 * owners share a megabyte.
 *
 * @implements SRS-AMP-004
 */
#include <stddef.h>

#include "rtos/bsp.h"

#define SECTION_SIZE (0x00100000UL)
#define PAGE_SIZE    (0x00001000UL)
#define L2_TABLES    (8U)

/* Short-descriptor format (ARMv7-A). Section: TEX[14:12] AP[11:10] S(16) XN(4) C(3) B(2). */
#define SECT_TYPE   (0x2UL)
#define SECT_B      (1UL << 2U)
#define SECT_C      (1UL << 3U)
#define SECT_XN     (1UL << 4U)
#define SECT_AP_RW  (0x3UL << 10U)
#define SECT_TEX(x) ((uint32_t)(x) << 12U)
#define SECT_S      (1UL << 16U)
#define COARSE_TYPE (0x1UL)
/* Small page: XN(0) type(1) B(2) C(3) AP[5:4] TEX[8:6] S(10). */
#define PAGE_TYPE   (0x2UL)
#define PAGE_XN     (1UL << 0U)
#define PAGE_B      (1UL << 2U)
#define PAGE_C      (1UL << 3U)
#define PAGE_AP_RW  (0x3UL << 4U)
#define PAGE_TEX(x) ((uint32_t)(x) << 6U)
#define PAGE_S      (1UL << 10U)

static const bsp_mem_region_t k_regions[] = {
    /* DDR partitions */
    {SATLINK_DDR_RTOS_FW_BASE, SATLINK_DDR_RTOS_FW_SIZE, BSP_MEM_NORMAL_WB, true},
    {SATLINK_DDR_IPC_SHM_BASE, SATLINK_DDR_IPC_SHM_SIZE, BSP_MEM_NORMAL_NC, false},
    {SATLINK_DDR_MODEM_DMA_BASE, SATLINK_DDR_MODEM_DMA_SIZE, BSP_MEM_NORMAL_NC, false},
    /* PL blocks owned by the RTOS */
    {SATLINK_PS_AXI_DMA_MODEM_BASE, SATLINK_PS_AXI_DMA_MODEM_SIZE, BSP_MEM_DEVICE, false},
    {SATLINK_PS_AXI_GPIO_0_BASE, SATLINK_PS_AXI_GPIO_0_SIZE, BSP_MEM_DEVICE, false},
    {SATLINK_PS_PAYLOAD_CTRL_BASE, SATLINK_PS_PAYLOAD_CTRL_SIZE, BSP_MEM_DEVICE, false},
    /* PS peripherals owned by the RTOS, and the per-core MPCore block */
    {SATLINK_PS_PS_UART0_BASE, SATLINK_PS_PS_UART0_SIZE, BSP_MEM_DEVICE, false},
    {SATLINK_PS_PS_TTC1_BASE, SATLINK_PS_PS_TTC1_SIZE, BSP_MEM_DEVICE, false},
    {SATLINK_PS_MPCORE_BASE, SATLINK_PS_MPCORE_SIZE, BSP_MEM_DEVICE, false},
};

static uint32_t g_l1[4096] __attribute__((section(".mmu_tables"), aligned(16384)));
static uint32_t g_l2[L2_TABLES][256] __attribute__((section(".mmu_tables"), aligned(1024)));
static uint32_t g_l2_used;

static bsp_mem_type_t region_type(uint32_t addr)
{
    for (size_t i = 0U; i < (sizeof(k_regions) / sizeof(k_regions[0])); ++i)
    {
        const bsp_mem_region_t *r = &k_regions[i];
        if ((addr >= r->base) && ((addr - r->base) < r->size))
        {
            return r->type;
        }
    }
    return BSP_MEM_FAULT;
}

/* Type as the live table says (walks L1 and, if present, the L2 table), so the self-test checks
 * what the hardware uses, not the region list. */
bsp_mem_type_t bsp_mmu_type_of(uint32_t addr)
{
    uint32_t d = g_l1[addr >> 20U];
    if ((d & 0x3U) == COARSE_TYPE)
    {
        const uint32_t *l2 = (const uint32_t *)(uintptr_t)(d & 0xFFFFFC00UL);
        d = l2[(addr >> 12U) & 0xFFU];
        if ((d & 0x2U) == 0U)
        {
            return BSP_MEM_FAULT;
        }
        const bool c = (d & PAGE_C) != 0U;
        const bool b = (d & PAGE_B) != 0U;
        const uint32_t tex = (d >> 6U) & 0x7U;
        return c ? BSP_MEM_NORMAL_WB
                 : ((tex == 1U) ? BSP_MEM_NORMAL_NC : (b ? BSP_MEM_DEVICE : BSP_MEM_FAULT));
    }
    if ((d & 0x3U) != SECT_TYPE)
    {
        return BSP_MEM_FAULT;
    }
    const bool c = (d & SECT_C) != 0U;
    const bool b = (d & SECT_B) != 0U;
    const uint32_t tex = (d >> 12U) & 0x7U;
    return c ? BSP_MEM_NORMAL_WB
             : ((tex == 1U) ? BSP_MEM_NORMAL_NC : (b ? BSP_MEM_DEVICE : BSP_MEM_FAULT));
}

static bool exec_of(uint32_t addr)
{
    for (size_t i = 0U; i < (sizeof(k_regions) / sizeof(k_regions[0])); ++i)
    {
        const bsp_mem_region_t *r = &k_regions[i];
        if ((addr >= r->base) && ((addr - r->base) < r->size))
        {
            return r->exec;
        }
    }
    return false;
}

static uint32_t section_desc(uint32_t base, bsp_mem_type_t type, bool exec)
{
    uint32_t d = base | SECT_TYPE | SECT_AP_RW | (exec ? 0U : SECT_XN);
    switch (type)
    {
    case BSP_MEM_NORMAL_WB:
        d |= SECT_TEX(1U) | SECT_C | SECT_B | SECT_S; /* write-back, write-allocate */
        break;
    case BSP_MEM_NORMAL_NC:
        d |= SECT_TEX(1U) | SECT_S; /* normal, non-cacheable */
        break;
    case BSP_MEM_DEVICE:
        d |= SECT_B; /* shareable device */
        break;
    default:
        d = 0U;
        break;
    }
    return d;
}

static uint32_t page_desc(uint32_t base, bsp_mem_type_t type, bool exec)
{
    uint32_t d = base | PAGE_TYPE | PAGE_AP_RW | (exec ? 0U : PAGE_XN);
    switch (type)
    {
    case BSP_MEM_NORMAL_WB:
        d |= PAGE_TEX(1U) | PAGE_C | PAGE_B | PAGE_S;
        break;
    case BSP_MEM_NORMAL_NC:
        d |= PAGE_TEX(1U) | PAGE_S;
        break;
    case BSP_MEM_DEVICE:
        d |= PAGE_B;
        break;
    default:
        d = 0U;
        break;
    }
    return d;
}

static void build_tables(void)
{
    g_l2_used = 0U;
    for (uint32_t s = 0U; s < 4096U; ++s)
    {
        const uint32_t base = s * SECTION_SIZE;
        /* Uniform megabyte? */
        const bsp_mem_type_t first = region_type(base);
        const bool first_exec = exec_of(base);
        bool uniform = true;
        for (uint32_t p = 1U; (p < 256U) && uniform; ++p)
        {
            const uint32_t a = base + (p * PAGE_SIZE);
            uniform = (region_type(a) == first) && (exec_of(a) == first_exec);
        }
        if (uniform || (g_l2_used >= L2_TABLES))
        {
            g_l1[s] = (first == BSP_MEM_FAULT) ? 0U : section_desc(base, first, first_exec);
            continue;
        }
        uint32_t *l2 = g_l2[g_l2_used];
        ++g_l2_used;
        for (uint32_t p = 0U; p < 256U; ++p)
        {
            const uint32_t a = base + (p * PAGE_SIZE);
            const bsp_mem_type_t t = region_type(a);
            l2[p] = (t == BSP_MEM_FAULT) ? 0U : page_desc(a, t, exec_of(a));
        }
        g_l1[s] = (uint32_t)(uintptr_t)l2 | COARSE_TYPE;
    }
}

void satlink_mmu_enable(void)
{
    build_tables();
    __asm__ volatile("dsb" ::: "memory");
    /* TTBR0 (table walks non-cacheable), TTBCR = 0, all domains client. */
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"((uint32_t)(uintptr_t)g_l1));
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 2" ::"r"(0U));
    __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" ::"r"(0x55555555U));
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" ::"r"(0U)); /* TLBIALL */
    __asm__ volatile("dsb\n isb" ::: "memory");

    /* Join coherency (ACTLR.SMP) like Core 0, then MMU, caches, branch prediction. */
    uint32_t actlr = 0U;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 1" : "=r"(actlr));
    actlr |= (1U << 6U);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 1" ::"r"(actlr));
    uint32_t sctlr = 0U;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= (1U << 0U) | (1U << 2U) | (1U << 11U) | (1U << 12U); /* M, C, Z, I */
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" ::"r"(sctlr));
    __asm__ volatile("dsb\n isb" ::: "memory");
}

uint32_t bsp_cpu_id(void)
{
    uint32_t mpidr = 0U;
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
    return mpidr & 0x3U;
}
