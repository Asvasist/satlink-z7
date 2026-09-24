# Start-up code for the MicroBlaze V housekeeping controller (RV32).
#
# Used by both the bootloader and the application. The linker scripts place this first in the
# image, at the reset vector for the bootloader and directly behind the 16-byte header for the
# application (the bootloader jumps there). The code and the initial data are already in memory
# (bitstream initialisation or CAN upload), so only the stack pointer and .bss need setting up.
#
# @implements SRS-HKC-004

    .section .text.start, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    la      sp, __stack_top

    la      t0, __bss_start
    la      t1, __bss_end
1:
    bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    la      t0, trap_entry
    csrw    mtvec, t0

    call    main
3:
    j       3b                      # main must not return

    .balign 4
    .type trap_entry, @function
trap_entry:
    j       trap_entry              # no interrupts are enabled; anything unexpected stops here
