/*
 * MicroBlaze V bootloader: first 16 KiB of LMB BRAM (lmb_bootloader in icd/address_map.yaml),
 * baked into the bitstream. Code and initial data values stay in ROM so a warm reset (jump to
 * the reset vector) starts from the pristine values; the last 16 bytes are the mailbox shared
 * with the application.
 *
 * @implements SRS-HKC-002
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ROM (rx)  : ORIGIN = 0x00000000, LENGTH = 0x3000
    RAM (rw)  : ORIGIN = 0x00003000, LENGTH = 0x0FF0   /* 0x3FF0..0x3FFF: mailbox */
}

__stack_size = 0x800;

INCLUDE hkc_sections.ld
