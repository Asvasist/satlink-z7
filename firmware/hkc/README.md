# Housekeeping controller firmware (MicroBlaze V)

Stage 3: bare-metal housekeeping on the MicroBlaze V soft processor in the PL, and a bootloader
in LMB block RAM that takes the application over CAN. Built with the `hkc-riscv` CMake preset
(RV32IMC, freestanding, no C library). Exit criteria and bring-up checklist:
[docs/stages/stage-3.md](../../docs/stages/stage-3.md).

```
bootloader/main.c     starts a valid application, or waits for an upload over CAN
app/main.c            monitors temperature and supplies, supervises its tasks, sends telemetry
platform/             UART, timer, watchdog, XADC, AXI Quad SPI and CAN access; crt0; libc helpers
linker/bootloader.ld  16 KiB at 0x0000
linker/app.ld         112 KiB at 0x4000, header first, room for a 4 KiB stack
```

The portable logic is in `libs/can` (MCP2515 driver), `libs/boot` (upload protocol, image
header) and `libs/hk` (XADC conversion, limits, watchdog supervisor, telemetry frames); it has host
unit tests. This directory holds only what touches the hardware.

## Build

```bash
cmake --preset hkc-riscv        # add -DSATLINK_CROSS_PREFIX=<prefix> if it is not riscv64-unknown-elf-
cmake --build --preset hkc-riscv
```

The result is `hkc_bootloader.elf/.bin` (goes into the bitstream's LMB initialisation) and
`hkc_app.elf/.bin/.img`. `hkc_app.img` is the application with the 16-byte header written by
`tools/hkc/mkapp.py`; that is the file to upload. The linker scripts fail the build when an image
does not fit its region.

`HKC_CLOCK_HZ` (default 100 MHz) and `HKC_CAN_OSC_HZ` (default 16 MHz) are CMake cache variables;
change them if the block design or the PmodCAN differ.

## Update over CAN

On the Linux side, with the CAN interface up (`linux/scripts/can-up.sh can0 500000`):

```bash
satlink-diag hkc telemetry              # one snapshot: temperature, supplies, uptime
satlink-diag hkc upload hkc_app.img     # restarts into the bootloader, uploads, starts
satlink-diag hkc ping                   # bootloader state, if it is the bootloader that runs
```

Protocol and design reasons: [ADR-0005](../../docs/architecture/adr/0005-can-firmware-upload.md).

## Behaviour

- **Bootloader.** After a reset it waits about 2 s for an upload; if none starts and the image in
  memory validates (magic, sizes, CRC-16), it starts it. Otherwise it stays and waits. An upload
  is only started on the host's BOOT command, after the whole-image CRC matched.
- **Application.** Polls CAN every 10 ms, samples the XADC every 200 ms, sends one snapshot as
  three frames (0x100, 0x101, 0x102) every second. The hardware watchdog is kicked every 100 ms
  tick only if both tasks reported in during the last 500 ms. An ENTER command makes it
  acknowledge and jump back to the bootloader.
- **Provisional values.** Temperature limits (75 and 85 °C) and supply limits (4 % and 6 % below
  nominal) are placeholders until they are checked against the datasheet.
