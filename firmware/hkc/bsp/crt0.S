/*
 * crt0.S - reset entry and trap vector for the MicroBlaze V (RV32, machine mode only).
 *
 * Shared by the bootloader (linked at the reset vector) and the application (linked right after
 * its image header). Copies .data from its load address, clears .bss, installs the trap vector
 * and calls main().
 *
 * @implements SRS-HKC-001
 */
    .section .text.init, "ax"
    .global _start
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, __stack_top
    csrw    mie, zero
    csrci   mstatus, 0x8            /* MIE = 0 until main enables interrupts */

    la      a0, __data_load
    la      a1, __data_start
    la      a2, __data_end
1:  bgeu    a1, a2, 2f
    lw      t0, 0(a0)
    sw      t0, 0(a1)
    addi    a0, a0, 4
    addi    a1, a1, 4
    j       1b

2:  la      a0, __bss_start
    la      a1, __bss_end
3:  bgeu    a0, a1, 4f
    sw      zero, 0(a0)
    addi    a0, a0, 4
    j       3b

4:  la      t0, hkc_trap_entry
    csrw    mtvec, t0               /* direct mode */
    call    main
5:  wfi
    j       5b

/* Trap entry: save the caller-saved registers, call hkc_trap_handler(mcause, mepc). */
    .section .text, "ax"
    .balign 4
    .global hkc_trap_entry
hkc_trap_entry:
    addi    sp, sp, -64
    sw      ra, 0(sp)
    sw      t0, 4(sp)
    sw      t1, 8(sp)
    sw      t2, 12(sp)
    sw      a0, 16(sp)
    sw      a1, 20(sp)
    sw      a2, 24(sp)
    sw      a3, 28(sp)
    sw      a4, 32(sp)
    sw      a5, 36(sp)
    sw      a6, 40(sp)
    sw      a7, 44(sp)
    sw      t3, 48(sp)
    sw      t4, 52(sp)
    sw      t5, 56(sp)
    sw      t6, 60(sp)
    csrr    a0, mcause
    csrr    a1, mepc
    call    hkc_trap_handler
    lw      ra, 0(sp)
    lw      t0, 4(sp)
    lw      t1, 8(sp)
    lw      t2, 12(sp)
    lw      a0, 16(sp)
    lw      a1, 20(sp)
    lw      a2, 24(sp)
    lw      a3, 28(sp)
    lw      a4, 32(sp)
    lw      a5, 36(sp)
    lw      a6, 40(sp)
    lw      a7, 44(sp)
    lw      t3, 48(sp)
    lw      t4, 52(sp)
    lw      t5, 56(sp)
    lw      t6, 60(sp)
    addi    sp, sp, 64
    mret
