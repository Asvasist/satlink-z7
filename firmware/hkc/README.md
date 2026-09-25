# Housekeeping controller firmware (MicroBlaze V)

Bare-metal RV32 firmware for the MicroBlaze V soft processor in the PL. Built with the
`hkc-riscv` CMake preset; design notes in [docs/stages/stage-3.md](../../docs/stages/stage-3.md).

```
bsp/crt0.S          reset entry (.data copy, .bss clear), trap vector, calls main()
bsp/libc_min.c      mem* functions (no C library is linked)
drivers/            AXI UART Lite, GPIO, Timer, INTC, Timebase WDT, Quad SPI, XADC
include/hkc/        board constants, MMIO and CPU helpers, driver API
linker/             bootloader.ld (BRAM 0x0000), app.ld (0x4020, after the image header)
bootloader/main.c   CAN bootloader (canboot protocol from libs/hkc_proto)
app/main.c          application: interrupts, CAN, watchdog around app_logic
app_logic/          portable application logic, unit-tested on the host
```

Outputs in `build/hkc-riscv/firmware/hkc/`:

| File | Use |
|---|---|
| `hkc_bootloader.elf` | Associate with the MicroBlaze V in Vivado (or `updatemem`) so it is in the bitstream |
| `hkc_app.slhk` | Application image; `satlink-hkc flash hkc_app.slhk --boot` or ship it in `/lib/firmware/satlink/` |
| `*.map`, `*.bin` | Link maps and raw binaries |

```bash
cmake --preset hkc-riscv && cmake --build --preset hkc-riscv
python3 tools/hkc/mkimage.py --info build/hkc-riscv/firmware/hkc/hkc_app.slhk
```

Consoles: the MicroBlaze V UART is on Pmod JE7/JE8 (115200 8N1, see `hw/README.md`).
