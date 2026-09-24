/* Housekeeping application: the 112 KiB above the bootloader in the LMB memory.
 *
 * The image starts with a 16-byte header that tools/hkc/mkapp.py fills in (magic, size, CRC), and
 * the entry point follows it: the bootloader starts the application at ORIGIN + 16. Code, data,
 * .bss and stack are all inside the region; the linker refuses an image that does not fit and at
 * least 4 KiB must stay free for the stack.
 *
 * @implements SRS-HKC-004
 */
ENTRY(_start)

MEMORY
{
    APP (rwx) : ORIGIN = 0x00004000, LENGTH = 0x1C000
}

SECTIONS
{
    .app_header :
    {
        LONG(0) LONG(0) LONG(0) LONG(0)
    } > APP

    .text :
    {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > APP

    .rodata :
    {
        *(.rodata .rodata.* .srodata .srodata.*)
    } > APP

    .data :
    {
        *(.data .data.* .sdata .sdata.*)
    } > APP

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > APP

    __stack_top = ORIGIN(APP) + LENGTH(APP);
    ASSERT(ADDR(.text) == ORIGIN(APP) + 16, "application: the entry point must follow the 16-byte header")
    ASSERT(__bss_end + 0x1000 <= __stack_top, "application: less than 4 KiB left for the stack")

    /DISCARD/ : { *(.comment) *(.note*) }
}
