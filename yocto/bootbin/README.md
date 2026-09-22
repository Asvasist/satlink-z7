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
