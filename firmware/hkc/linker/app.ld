/*
 * MicroBlaze V application: lmb_app (0x4000, 112 KiB). The 32-byte image header written by
 * tools/hkc/mkimage.py sits at 0x4000, code starts right after it. The top 16 KiB hold data,
 * bss and the stack and are not part of the image.
 *
 * @implements SRS-HKC-004
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ROM (rx)  : ORIGIN = 0x00004020, LENGTH = 0x17FE0
    RAM (rw)  : ORIGIN = 0x0001C000, LENGTH = 0x4000
}

__stack_size = 0x1000;

INCLUDE hkc_sections.ld
