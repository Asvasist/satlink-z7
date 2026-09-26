/*
 * Core 1 firmware: rtos_fw partition (0x38000000, 16 MiB) for code, data and stacks; DMA
 * descriptors and sample buffers in modem_dma (0x39100000, 15 MiB), mapped non-cacheable.
 * Must match icd/address_map.yaml (ddr_partitions).
 *
 * @implements SRS-AMP-001
 */
ENTRY(_vector_table)

MEMORY
{
    FW  (rwx) : ORIGIN = 0x38000000, LENGTH = 16M
    DMA (rw)  : ORIGIN = 0x39100000, LENGTH = 15M
}

SECTIONS
{
    .vectors : ALIGN(32) { KEEP(*(.vectors)) } > FW
    .text : { *(.text .text.*) *(.glue_7) *(.glue_7t) } > FW
    .rodata : { *(.rodata .rodata.*) } > FW
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FW
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx* .gnu.linkonce.armexidx.*) __exidx_end = .; } > FW
    .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) PROVIDE_HIDDEN(__init_array_end = .); } > FW
    .data : ALIGN(8) { *(.data .data.*) } > FW

    .bss (NOLOAD) : ALIGN(8)
    {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > FW

    .mmu (NOLOAD) : ALIGN(16384) { *(.mmu_tables) } > FW

    .stacks (NOLOAD) : ALIGN(16)
    {
        . += 0x4000; __svc_stack_top = .;
        . += 0x4000; __irq_stack_top = .;
        . += 0x1000; __fiq_stack_top = .;
        . += 0x1000; __abt_stack_top = .;
        . += 0x1000; __und_stack_top = .;
        . += 0x4000; __sys_stack_top = .;
    } > FW

    /* newlib's sbrk is unused (no malloc outside FreeRTOS' heap_4), but must link. */
    end = .;
    PROVIDE(_end = .);

    .dma (NOLOAD) : ALIGN(64) { *(.dma_buffers) } > DMA
}
