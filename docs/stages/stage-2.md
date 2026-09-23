# Stage 2: Linux drivers, C++ HAL, diagnostics, board bring-up

Goal: every Linux-owned PL block (`ccsds_frame_accel`, `spec_tap`) has a platform driver, a
typed C++ HAL on top of it, and the peripherals Linux owns directly (codec over I2C, CAN over
SPI) are brought up and testable from user space.

## Exit criteria

| # | Criterion | Requirement | Status |
|---|---|---|---|
| 1 | `ccsds_frame_accel` platform driver: DT-bound, char device, IRQ, dmaengine channels | SRS-DRV-001..004 | done (code + Yocto recipe written; unverified - toolchain blocked, see below) |
| 2 | UAPI header shared verbatim by kernel and user space | SRS-DRV-002 | done |
| 3 | `satlink::hal::FrameAccelerator`, mock-tested without hardware | SRS-HAL-001/002 | done, 6 unit tests |
| 4 | `spec_tap` platform driver + HAL | SRS-DRV-001..004, SRS-HAL-001/002 | pending |
| 5 | SSM2603 codec brought up over I2C (Linux ASoC or a minimal control-only driver) | - | pending |
| 6 | MCP2515 brought up as SocketCAN on `ps_spi1`, CAN loopback test | - | pending |
| 7 | Diagnostic CLI exercising both HAL classes | - | pending |
| 8 | Driver + HAL built for real by the Yocto eSDK and CI's `linux-arm` cross build | SRS-BLD-004 | pending (needs Stage 1's first Yocto build) |
| 9 | Board bring-up: driver probes on hardware, IRQ fires, ioctl round-trip confirmed on the Zybo | SRS-DRV-001..004 | pending (needs hw-v1 XSA, Stage 1 item 11) |

## Design notes

- **Ownership split.** `payload_ctrl` and its DMA (`axi_dma_modem`) belong to Core 1 / FreeRTOS
  (Stage 4), not Linux - see [ADR-0003](../architecture/adr/0003-amp-resource-ownership.md).
  Stage 2 only touches the two Linux-owned blocks in
  [icd/address_map.yaml](../../icd/address_map.yaml): `ccsds_frame_accel` and `spec_tap`.
- **One UAPI header, two consumers.** `linux/include/uapi/satlink/ccsds_frame_accel.h` is
  included unchanged by the kernel driver and by the C++ HAL, so their struct layouts can never
  drift apart (SRS-DRV-002).
- **HAL is portable, the backend isn't.** `linux/hal`'s `FrameAccelerator` logic has zero OS
  calls; only `posix_char_device.cpp` touches `open()`/`ioctl()`/`close()`, and it is compiled
  only for `SATLINK_TARGET=linux`. That's what lets `tests/unit/hal/test_ccsds_frame_accel_hal.cpp`
  run as an ordinary host unit test against a GoogleMock `CharDeviceIo` (SRS-HAL-002).
- **Kernel code follows kernel style, not the repo's.** `linux/drivers/` has its own
  `.clang-format` (`BasedOnStyle: Linux`): tabs and checkpatch-shaped code, because that's what
  a kernel maintainer - or a hiring manager who's done kernel work - expects to see there.
- **Register offsets are hand-kept in sync with the ICD**, not generated, inside the kernel
  driver: `libs/regs`'s generated header pulls in `<stdint.h>`, which kernel style avoids
  (`u32`/`u16`/`u8` from `<linux/types.h>` instead). Extending
  `tools/regmap/regmap_gen.py` to also emit a kernel-style variant from the same YAML is a
  natural follow-up, not yet done.
- **What's genuinely unverified right now:** this machine's C/C++ compiler (`cc1.exe`, and
  `cmake.exe` before it) is being silently killed at launch, most likely by security software -
  see the project's own working notes. Every file in this stage has been written and reviewed
  by hand and the ICD/traceability tooling (pure Python, unaffected) confirms every register
  offset, IRQ number, and `@implements`/`@verifies` tag is internally consistent - but nothing
  here has compiled yet. First priority once the toolchain is unblocked: `cmake --build
  --preset host-debug && ctest --preset host-debug`.
