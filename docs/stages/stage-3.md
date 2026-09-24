# Stage 3: housekeeping MCU, CAN bootloader, A/B boot

Goal: the MicroBlaze V housekeeping controller (HKC) monitors the payload's health on its own and
broadcasts it over CAN, its firmware can be replaced from Linux over the same CAN bus, and the
Linux image itself can be updated with a rollback that keeps the board bootable.

## Exit criteria

| # | Criterion | Requirement | Status |
|---|---|---|---|
| 1 | MCP2515 driver: SPI transactions built for a caller-supplied transfer function, polling, bit timing computed from the oscillator | SRS-HKC-002 | done (21 tests) |
| 2 | XADC conversion, limits with hysteresis, task-alive watchdog supervisor, telemetry frames | SRS-HKC-001 | done (15 tests) |
| 3 | CAN upload protocol: sequenced and acknowledged, survives lost and duplicated frames, detects corrupted ones; receiver and sender in one portable library | SRS-HKC-003 | done (24 tests) |
| 4 | Application header and CRC check before an image may start; `mkapp.py` writes the header | SRS-HKC-003 | done (7 C tests, 9 Python tests, one shared golden image) |
| 5 | Bootloader and application firmware for the MicroBlaze V, linker scripts that enforce the 16 KiB and 112 KiB regions | SRS-HKC-004 | builds with the hkc-riscv preset; not run on hardware |
| 6 | Linux side: `satlink-diag hkc ping\|enter\|upload\|telemetry`, tested against the bootloader's own receiver on a bad bus | SRS-HKC-005 | done (20 + 15 tests); the SocketCAN backend is compiled, not run |
| 7 | A/B root slots with boot counting and rollback: boot script, `satlink-bootctl`, reference model | SRS-BOOT-003 | done in simulation (8 C tests, 23 Python tests); not run on a board |
| 8 | Yocto wiring for the A/B layout, opt-in with `SATLINK_AB=1` | SRS-BOOT-003 | written, first Yocto build pending |
| 9 | Signed FIT images | SRS-BOOT-002 | not started (Draft): needs a key management decision |
| 10 | Board bring-up: bootloader and application on the MicroBlaze V, upload from Linux, telemetry, A/B rollback on the SD card | SRS-HKC-001..005, SRS-BOOT-003 | pending (needs the hw-v1 bitstream with the HKC subsystem) |

## Design notes

- **One protocol library, three users.** `libs/boot` is C11 without dynamic memory. The bootloader
  runs its receiver, the Linux tool runs its sender, and the unit tests connect them through a
  bus model that loses, duplicates and corrupts frames. See
  [ADR-0005](../architecture/adr/0005-can-firmware-upload.md).
- **Drivers take a function, not a peripheral.** The MCP2515 driver builds the SPI byte sequences
  and hands them to a caller-supplied transfer function. On the board that function drives AXI
  Quad SPI; in the tests it drives a register-level simulator of the chip.
- **Nothing starts without a valid image.** The application header carries magic, version,
  length and a CRC-16 of the body. The bootloader starts the image in memory only if it
  validates, and a finished upload only on the host's BOOT command after the whole-image CRC
  passed.
- **Watchdog only while everyone reports in.** The supervisor counts which tasks reported in a
  window; the hardware watchdog is kicked only if all did. A hung CAN task therefore resets the
  subsystem, even though the main loop still runs.
- **A/B state in the U-Boot environment.** Five variables on the FAT boot partition, one
  decision per boot, and a write order in `satlink-bootctl` that keeps a power loss from
  stranding the board. See [ADR-0006](../architecture/adr/0006-ab-boot-slots-in-uboot-env.md).
- **The boot script is tested as it is.** `tools/boot/ubootsim.py` runs the real `boot-ab.cmd`
  against an environment file; `satlink-bootctl` runs against fake `fw_printenv`/`fw_setenv`
  tools on the same file. The scenarios are those of the C reference model, plus a power loss in
  the middle of `begin-update`.
- **Slot files are separate.** Each slot's kernel and device tree are files on the boot
  partition (`zImage.a`, `system-a.dtb`, ...). The image provides slot a; the update flow writes
  slot b and its files, then calls `satlink-bootctl begin-update`.

## Memory map (from the ICD, all `provisional`)

| Region | Base | Size | Contents |
|---|---|---|---|
| `lmb_bootloader` | 0x0000 | 16 KiB | bootloader (currently 4136 bytes), part of the bitstream |
| `lmb_app` | 0x4000 | 112 KiB | 16-byte header, application code, data, stack (currently a 3864-byte image) |

## Status of the checks

- All host unit tests build with clang at `-Werror` and the project's warning set through the
  full CMake configuration and pass (91 ctest entries on a Windows host).
- `pytest tools`: 56 tests pass, including the A/B scenarios (they need a POSIX `sh`).
- `clang-format` 18.1.8 passes on every C and C++ file; clang-tidy 18.1.8 reports no
  error-level findings on the new code; cppcheck 2.13 is clean on `libs/`.
- The bootloader and the application build with the Vitis RISC-V GCC 13.4 through the `hkc-riscv`
  preset, and `mkapp.py` turns the application into an image whose header the C validator
  accepts.
- `satlink-diag` cross-compiles for `arm-linux-gnueabihf` at `-Werror` and links into an ARM
  executable. It has not been run on a board, and the SocketCAN backend has not seen a real CAN
  interface.
- Not built yet: the Yocto A/B configuration (wic layout, U-Boot fragment, recipes) and everything
  that needs the board or the hw-v1 bitstream.

## Bring-up checklist (hardware)

Values assumed in code or configuration that have to be confirmed:

- [ ] HKC clock is 100 MHz (`HKC_CLOCK_HZ`): the millisecond and microsecond timing is derived from it
- [ ] PmodCAN #2 oscillator is 16 MHz (`HKC_CAN_OSC_HZ`); 500 kbit/s is accepted by both nodes
- [ ] AXI UART Lite, Timer, Timebase WDT, Quad SPI and XADC register offsets match the IP versions
      in the block design (checked against the embeddedsw drivers, not against silicon)
- [ ] The AXI Timebase WDT interval gives a timeout longer than the 500 ms window and short enough
      to matter; XADC is in a mode where the status registers at +0x200 hold the latest sample
- [ ] LMB memory is at least 128 KiB and is initialised from the bitstream with the bootloader;
      the reset vector is 0x0000 and the MicroBlaze V trap vectors do not collide with the
      bootloader's start-up code
- [ ] The bootloader starts the application about 2 s after reset when it validates, and stays in
      the loader when it does not
- [ ] `satlink-diag hkc enter` followed by `upload` replaces the application on a running board,
      and `telemetry` shows plausible temperature and supplies
- [ ] Temperature and supply limits in `firmware/hkc/app/main.c` against the Zynq-7000 datasheet
- [ ] `SATLINK_AB=1` image: U-Boot reads and writes `uboot.env` on the FAT partition,
      `fw_printenv` on Linux sees the same variables, `satlink-bootctl status` prints the state
- [ ] A slot that never confirms boots `bootlimit` times and the board falls back; a healthy slot
      is confirmed after 30 s
- [ ] Bring-up report written from [docs/bringup/TEMPLATE.md](../bringup/TEMPLATE.md)

## Open items

- **Signed FIT images (SRS-BOOT-002).** Verified boot needs a signing key, a place for the public
  key in U-Boot and a build step in Yocto. It is left out until the key handling is decided.
- **Authentication of the CAN upload.** CRC-16 detects corruption, not tampering.
- **FSBL and bitstream in the Yocto build.** `BOOT.BIN` is still assembled by hand
  ([ADR-0004](../architecture/adr/0004-stage1-boot-chain.md)); it depends on the hw-v1 design.
