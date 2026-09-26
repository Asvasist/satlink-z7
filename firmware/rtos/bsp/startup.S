/*
 * startup.S - reset entry and exception vectors of the Core 1 firmware (ARMv7-A, Cortex-A9).
 *
 * Linux (satlink_amp driver) loads the ELF into the rtos_fw DDR partition and releases Core 1
 * at _vector_table. Nothing here touches shared state: no L2 cache controller, no SCU, no GIC
 * distributor reset. Those belong to Linux (ADR-0003).
 *
 * @implements SRS-AMP-001
 */
    .syntax unified
    .arm
    .fpu vfpv3

    .equ MODE_FIQ, 0x11
    .equ MODE_IRQ, 0x12
    .equ MODE_SVC, 0x13
    .equ MODE_ABT, 0x17
    .equ MODE_UND, 0x1B
    .equ MODE_SYS, 0x1F
    .equ NO_INT,   0xC0

    .section .vectors, "ax"
    .global _vector_table
_vector_table:
    b       _reset
    b       satlink_undef_entry
    b       FreeRTOS_SWI_Handler
    b       satlink_prefetch_abort_entry
    b       satlink_data_abort_entry
    nop
    b       FreeRTOS_IRQ_Handler
    b       satlink_fiq_entry

    .text
    .global _reset
_reset:
    cpsid   if

    /* Vector base: our table, low vectors. */
    ldr     r0, =_vector_table
    mcr     p15, 0, r0, c12, c0, 0
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1 << 13)          /* V: low vectors */
    bic     r0, r0, #(1 << 0)           /* M: MMU off */
    bic     r0, r0, #(1 << 2)           /* C: D-cache off */
    bic     r0, r0, #(1 << 12)          /* I: I-cache off */
    mcr     p15, 0, r0, c1, c0, 0
    isb

    /* Invalidate TLBs, I-cache, branch predictor. */
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0
    mcr     p15, 0, r0, c7, c5, 0
    mcr     p15, 0, r0, c7, c5, 6
    dsb
    isb

    /* Invalidate the L1 data cache by set/way (32 KiB, 4 ways, 32-byte lines on the A9). */
    mov     r0, #0
    mcr     p15, 2, r0, c0, c0, 0       /* CSSELR: L1 data */
    isb
    mrc     p15, 1, r1, c0, c0, 0       /* CCSIDR */
    and     r2, r1, #7                  /* log2(line words) - 2 */
    add     r2, r2, #4                  /* set shift */
    ldr     r3, =0x3FF
    and     r3, r3, r1, lsr #3          /* ways - 1 */
    ldr     r4, =0x7FFF
    and     r4, r4, r1, lsr #13         /* sets - 1 */
    clz     r5, r3                      /* way shift */
1:  mov     r6, r4                      /* set counter */
2:  lsl     r7, r3, r5
    lsl     r8, r6, r2
    orr     r7, r7, r8
    mcr     p15, 0, r7, c7, c6, 2       /* DCISW */
    subs    r6, r6, #1
    bge     2b
    subs    r3, r3, #1
    bge     1b
    dsb

    /* One stack per exception mode, then continue in SVC. */
    cps     #MODE_IRQ
    ldr     sp, =__irq_stack_top
    cps     #MODE_FIQ
    ldr     sp, =__fiq_stack_top
    cps     #MODE_ABT
    ldr     sp, =__abt_stack_top
    cps     #MODE_UND
    ldr     sp, =__und_stack_top
    cps     #MODE_SYS
    ldr     sp, =__sys_stack_top
    cps     #MODE_SVC
    ldr     sp, =__svc_stack_top

    /* Enable VFP/NEON access (CPACR cp10/cp11 full) and the FPU itself. */
    mrc     p15, 0, r0, c1, c0, 2
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
    isb
    mov     r0, #0x40000000
    vmsr    fpexc, r0

    /* Clear .bss (the loader places the ELF segments; bss is not in the file). */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
3:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     3b

    bl      satlink_mmu_enable
    bl      main
4:  wfi
    b       4b

/* Exception entries: switch to a C handler with the faulting address; they do not return. */
    .global satlink_undef_entry
satlink_undef_entry:
    sub     r1, lr, #4
    mov     r0, #1
    b       satlink_fault
    .global satlink_prefetch_abort_entry
satlink_prefetch_abort_entry:
    sub     r1, lr, #4
    mov     r0, #2
    b       satlink_fault
    .global satlink_data_abort_entry
satlink_data_abort_entry:
    sub     r1, lr, #8
    mov     r0, #3
    b       satlink_fault
    .global satlink_fiq_entry
satlink_fiq_entry:
    mov     r1, lr
    mov     r0, #4
    b       satlink_fault
