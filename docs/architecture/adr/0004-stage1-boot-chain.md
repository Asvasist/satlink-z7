# ADR-0004: Stage 1 boot chain

- Status: Accepted (Stage 3 added A/B slots, see ADR-0006; FSBL, bitstream and signed images remain open)
- Date: 2026-09-22

## Context

A Zynq-7000 boots the BootROM, then the FSBL (which initializes DDR and clocks from the
hardware design), then U-Boot. The FSBL and the bitstream depend on the Vivado design, which is
developed in parallel with the software.

## Decision

In Stage 1 the FSBL is built in Vitis from the exported XSA and combined with the bitstream and
Yocto's `u-boot.elf` into `BOOT.BIN` using `bootgen` (`yocto/bootbin/satlink.bif`). Yocto builds
U-Boot, the boot script, the kernel, the device tree and the root file system. Linux uses the
mainline PS-only device tree `zynq-zybo-z7.dts` until the PL nodes are added in Stage 2.

## Consequences

- Linux work does not wait for the FPGA design, and QEMU can boot the same kernel and root file
  system.
- One manual step (copying BOOT.BIN to the SD card) remains until Stage 3 moves FSBL and
  bitstream into the Yocto build together with signed FIT images and A/B boot.
