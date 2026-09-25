# BOOT.BIN (Stage 1)

`BOOT.BIN` = FSBL + bitstream + U-Boot. In Stage 1 it is assembled by hand (ADR-0004):

1. **Vivado:** export the hardware with bitstream (`File > Export > Export Hardware`, include
   bitstream) to `satlink_hw.xsa`.
2. **Vitis:** create a platform from the XSA and build the `Zynq FSBL` application: `fsbl.elf`.
3. **Yocto:** take `u-boot.elf` from `build/tmp/deploy/images/zybo-z7-20/`.
4. Copy `fsbl.elf`, the bitstream (renamed to `system.bit`) and `u-boot.elf` next to
   `satlink.bif` and run bootgen (it ships with Vitis):

```bash
bootgen -arch zynq -image satlink.bif -o BOOT.BIN -w on
```

Copy `BOOT.BIN` to the FAT `boot` partition of the SD card. None of these binaries are
committed; they are attached to GitHub releases.

## Secure A/B boot (Stage 3)

With `SATLINK_SECURE_BOOT = "1"` (the default from Stage 3 on) U-Boot only boots signed FIT
images, so its device tree must contain the public key. Yocto's `uboot-sign.bbclass` writes
that device tree into `u-boot-dtb.bin`; use `satlink-ab.bif`, which loads it at U-Boot's text
base (0x04000000):

```bash
bootgen -arch zynq -image satlink-ab.bif -o BOOT.BIN -w on
```

The FAT partition then holds only `BOOT.BIN` (U-Boot writes `uboot.env` itself on the first
`saveenv`). Kernel and device tree are in `/boot/fitImage` inside each root file system slot;
see [docs/stages/stage-3.md](../../docs/stages/stage-3.md) for the slot layout and the rollback
logic.
