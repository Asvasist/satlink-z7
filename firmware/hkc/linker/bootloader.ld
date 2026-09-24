/* Bootloader: the 16 KiB at the bottom of the LMB memory, at the reset vector.
 *
 * Code, data, .bss and stack all live here: the rest of the memory is the download target for
 * the application and must not be touched by the bootloader itself. The ICD gives the region
 * (SATLINK_HKC_LMB_BOOTLOADER_*); the linker refuses an image that does not fit, and at least
 * 1 KiB must stay free for the stack.
 *
 * @implements SRS-HKC-004
 */
ENTRY(_start)

MEMORY
{
    BL (rwx) : ORIGIN = 0x00000000, LENGTH = 0x4000
}

SECTIONS
{
    .text :
    {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > BL

    .rodata :
    {
        *(.rodata .rodata.* .srodata .srodata.*)
    } > BL

    .data :
    {
        *(.data .data.* .sdata .sdata.*)
    } > BL

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > BL

    __stack_top = ORIGIN(BL) + LENGTH(BL);
    ASSERT(__bss_end + 0x400 <= __stack_top, "bootloader: less than 1 KiB left for the stack")

    /DISCARD/ : { *(.comment) *(.note*) }
}
