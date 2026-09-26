/* Output sections shared by the bootloader and application linker scripts. */
SECTIONS
{
    .text :
    {
        KEEP(*(.text.init))
        *(.text .text.*)
    } > ROM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > ROM

    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT > ROM
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        . += __stack_size;
        __stack_top = .;
    } > RAM

    /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame) *(.riscv.attributes) }
}
