# Stage 2: Linux drivers, C++ HAL, diagnostics, board bring-up

Goal: every Linux-owned PL block (`ccsds_frame_accel`, `spec_tap`) has a platform driver and a
typed C++ HAL on top of it, the peripherals Linux owns directly (audio codec over I2C, CAN over
SPI) are configured, and a small tool exercises all of it on the board.

## Exit criteria

| # | Criterion | Requirement | Status |
|---|---|---|---|
| 1 | `ccsds_frame_accel` platform driver: DT-bound, char device, IRQ, dmaengine channels | SRS-DRV-001..004 | written, first kernel build pending |
| 2 | `spec_tap` platform driver: DT-bound, char device, dmaengine channel | SRS-DRV-001/002/004 | written, first kernel build pending |
| 3 | UAPI headers shared verbatim by kernel and user space | SRS-DRV-002 | done |
| 4 | `satlink::hal::FrameAccelerator` and `SpecTap`, mock-tested without hardware | SRS-HAL-001/002 | done (12 tests) |
| 5 | SSM2603 configured over I2C: `Ssm2603` HAL with register shadow, i2c-dev backend | SRS-PER-001 | done (10 tests) |
| 6 | MCP2515 as SocketCAN on `ps_spi1`: device tree, kernel config, loopback script | SRS-PER-002 | written, needs the board |
| 7 | `satlink-diag`: command logic separated from device opening, tested against fakes | SRS-DIAG-001 | done (21 tests) |
| 8 | Drivers and tool built by the Yocto recipes and installed in `satlink-image` | SRS-BSP-001 | written, first Yocto build pending |
| 9 | Board bring-up: drivers probe, IRQ fires, ioctl round trip, codec configured, CAN loopback passes | SRS-DRV-001..004, SRS-PER-001/002 | pending (needs the hw-v1 XSA, Stage 1 item 11) |

## Design notes

- **Ownership split.** `payload_ctrl` and its DMA (`axi_dma_modem`) belong to Core 1 / FreeRTOS
  (Stage 4), not Linux; see [ADR-0003](../architecture/adr/0003-amp-resource-ownership.md).
  Stage 2 only touches the blocks the [address map](../../icd/address_map.yaml) gives to Linux.
- **One UAPI header, two consumers.** `linux/include/uapi/satlink/*.h` is included unchanged by
  the kernel drivers and by the C++ HAL, so struct layouts cannot drift apart (SRS-DRV-002).
- **HAL is portable, backends are not.** HAL logic (`FrameAccelerator`, `SpecTap`, `Ssm2603`)
  makes no OS calls. It talks to an abstract `CharDeviceIo` or `I2cBus`; only
  `posix_char_device.cpp` and `i2c_dev_bus.cpp` call `open()`/`ioctl()`/`write()`, and they are
  compiled only for `SATLINK_TARGET=linux`. Host unit tests inject fakes (SRS-HAL-002).
- **Kernel headers on a host build.** The UAPI headers include `<linux/types.h>` and
  `<linux/ioctl.h>`. On Linux hosts (CI) those are the real ones; on other hosts
  `linux/hal/compat/` provides the few definitions needed, with the kernel's request-number
  encoding, so the HAL tests build there too.
- **The codec cannot be read back.** The SSM2603 registers are write-only, so `Ssm2603` keeps a
  shadow of what it wrote. Every `satlink-diag` run is a new process, so `codec volume` and
  `codec mute` use `AssumeConfigured()`, which fills the shadow with the state `codec init`
  leaves behind (the unit tests check both paths end in the same shadow).
- **Only `ccsds_frame_accel` has an interrupt.** `spec_tap` has none in the ICD, so its status is
  an ioctl read; SRS-DRV-003 says so explicitly.
- **DMA is claimed, not used yet.** Both drivers request their AXI DMA channels at probe and log
  if they are not available. Streaming frames through them belongs to the payload manager
  (Stage 5).
- **Kernel style in `linux/drivers/`.** Tabs and K&R braces, with its own `.clang-format`, like
  the rest of the kernel. Register offsets there are kept in sync with the ICD by hand because
  the generated headers include `<stdint.h>`; a kernel-style output from
  `tools/regmap/regmap_gen.py` would remove that duplication.
- **The PL device tree is opt-in.** Reading a PL block that is not in the bitstream stalls the
  AXI bus, so `SATLINK_PL_DT=1` selects `zynq-zybo-z7-satlink.dts` (PL and PS additions) and the
  default stays the mainline PS-only tree used in Stage 1.

## Status of the checks

- ICD generator, traceability matrix and their Python tests run and pass.
- `clang-format` (18.1.8, the version CI and pre-commit use) passes on every C and C++ file.
- clang-tidy (18.1.8) reports no error-level findings on any C or C++ source.
- All host unit tests (the stage 1 libraries plus the 43 HAL, codec and diagnostic tests) build
  with clang at `-Werror` with the project's warning set and pass on a Windows host, using the
  compat headers for the missing kernel headers.
- The Linux-only sources (POSIX and i2c-dev backends, `satlink-diag`) cross-compile for
  `arm-linux-gnueabihf` at `-Werror`, and `satlink-diag` links into an ARM executable. It has not
  been run on a board.
- Stage 4 update: both kernel modules build and link against linux-xlnx `xlnx_rebase_v6.6_LTS`
  (`xilinx_zynq_defconfig`) with `W=1` and no warnings, and the board device trees compile with
  the kernel's `dtc` at `W=1`. That build found a duplicate `can0` label (the Zynq's own CAN
  controller uses it; the MCP2515 node is now `mcp2515_can`). CI repeats both on every push.

## Bring-up checklist (hardware)

Values assumed in code or device tree that have to be confirmed on the board:

- [x] `dtc` accepts `zynq-zybo-z7-satlink.dts` (Stage 4, CI)
- [ ] pinctrl group names (`spi1_0_grp`, `gpio0_0_grp`) match the kernel's Zynq pinctrl at runtime
- [ ] PL clock: `clocks = <&clkc 15>` (FCLK0) matches the hw-v1 block design
- [ ] DMA channel numbering in `dmas = <&axi_dma_frame 0>, <&axi_dma_frame 1>` (0 = MM2S, 1 = S2MM)
- [ ] IRQ numbers and base addresses against the Address Editor (then set `frozen` in the ICD)
- [ ] Codec: 12.288 MHz MCLK from the PL, 24-bit I2S, I2C address 0x1A, `codec init` passes
      audio in and out
- [ ] PmodCAN crystal is 16 MHz; `can-loopback-test.sh` prints PASS
- [ ] `insmod` both modules, `satlink-diag fa version` and `spec version` return sensible values
- [ ] Bring-up report written from [docs/bringup/TEMPLATE.md](../bringup/TEMPLATE.md)
