# SatLink-Z7

[![ci](https://github.com/Asvasist/satlink-z7/actions/workflows/ci.yml/badge.svg)](https://github.com/Asvasist/satlink-z7/actions/workflows/ci.yml)

**A software-defined satellite payload and link testbed on a single Zynq-7020 board.**

SatLink-Z7 turns a Digilent Zybo Z7-20 into a miniature communications payload with a ground
link. Three processors share the chip the way they would in a real payload or instrument:

| Processor | Software | Role |
|---|---|---|
| Cortex-A9 Core 0 | Embedded Linux (Yocto, PREEMPT_RT) | Payload manager, drivers, networking, SCPI, telemetry |
| Cortex-A9 Core 1 | FreeRTOS (AMP via OpenAMP/RPMsg) | Hard-real-time modem control, ACM, framing |
| MicroBlaze V (in the PL) | Bare metal, own bootloader | Housekeeping: temperatures, voltages, watchdog, CAN |

The PL hosts a baseband modem (NCOs, RRC filters, channel emulator), a CCSDS frame accelerator
and an FFT spectrum monitor. The "RF" link is the on-board audio codec: a 3.5 mm cable from
HPH OUT to LINE IN carries a real modulated carrier (12 kHz, 6 kSym/s), so modulation, noise,
synchronization and adaptive coding all happen on physical signals.

```mermaid
flowchart LR
    subgraph PS["Zynq PS"]
        L["Core 0: Linux<br/>payload manager, drivers,<br/>SCPI, CCSDS/PUS, TUN"]
        R["Core 1: FreeRTOS<br/>modem control, ACM"]
        L <-- "RPMsg" --> R
    end
    subgraph PL["Programmable logic"]
        M["Modem datapath<br/>NCO, RRC, channel emulator"]
        F["CCSDS frame accelerator"]
        S["FFT spectrum monitor"]
        H["MicroBlaze V<br/>housekeeping MCU"]
    end
    R -- "AXI DMA / regs" --> M
    L -- "AXI DMA / regs" --> F
    L -- "AXI DMA" --> S
    M -- "I2S" --> C["SSM2603 codec"]
    C -- "3.5 mm loopback" --> C
    L -- "SocketCAN" --> CAN(("CAN bus"))
    H -- "MCP2515" --> CAN
    G["Ground station<br/>Qt GUI, Rust CLI, pytest/pyVISA"] -- "Ethernet: UDP TM/TC, SCPI" --> L
```

## Status

| Stage | Content | Status |
|---|---|---|
| 1 | Foundation and BSP: build system, CI, requirements, ICD, Yocto layer, boot | **in progress** |
| 2 | Linux drivers, C++ HAL, diagnostics, board bring-up | planned |
| 3 | Housekeeping MCU, CAN bootloader, secure A/B boot | planned |
| 4 | AMP and real-time modem (FreeRTOS, FEC, synchronization) | planned |
| 5 | Adaptive link (ACM, LEO pass emulation) and on-board networking | planned |
| 6 | SCPI server, Qt ground station, Rust CLI, HIL tests, performance report | planned |

Stage 1 details and checklist: [docs/stages/stage-1.md](docs/stages/stage-1.md)

## Engineering practices

- **One build for four targets.** CMake presets for host, Linux (Core 0), FreeRTOS (Core 1)
  and MicroBlaze V (RV32). See [ADR-0001](docs/architecture/adr/0001-one-cmake-build.md).
- **Interfaces as code.** Register maps, base addresses, interrupts and DDR partitions live in
  YAML under [`icd/`](icd/). C/C++ headers and the [ICD pages](docs/icd/generated/README.md) are
  generated and validated; CI fails when they drift. See
  [ADR-0002](docs/architecture/adr/0002-icd-yaml-single-source.md).
- **Requirements traceability.** [SRS](docs/requirements/satlink_srs.sdoc) in StrictDoc
  (exportable to ReqIF for DOORS), `@implements`/`@verifies` tags in code, and a generated
  [traceability matrix](docs/traceability/traceability.md).
- **Shift-left testing.** Unity (C) and GoogleTest (C++) unit tests, coverage gate, ASan/UBSan,
  clang-tidy, cppcheck with a MISRA C:2012 report, QEMU boot test, and later hardware-in-the-loop
  tests with pytest/pyVISA.
- **Reference models first.** The C library in `libs/common` is the bit-exact reference the
  FPGA blocks and drivers are verified against.

## Repository layout

```
cmake/           toolchain files (arm-linux-gnueabihf, arm-none-eabi, riscv) and CMake modules
docs/            architecture (arc42 + ADRs), ICD, requirements, traceability, bring-up reports
firmware/rtos/   FreeRTOS firmware for Core 1                                  (Stage 4)
firmware/hkc/    MicroBlaze V housekeeping firmware and bootloader             (Stage 3)
host/            ground station (Qt), Rust CLI, HIL tests                      (Stage 6)
hw/              Vivado block design Tcl, RTL, constraints, filter coefficients
icd/             interface control: address map and register maps (YAML)
libs/common/     portable C11 library (CRC-16, CCSDS randomizer, ...)
libs/regs/       generated register headers (C and C++)
linux/           kernel drivers, C++ HAL, payload manager                      (Stage 2)
tests/           unit tests
tools/           register-map generator, traceability matrix
yocto/           kas configuration, meta-satlink BSP layer, QEMU smoke test, BOOT.BIN recipe
```

## Quick start

Linux or WSL2 (Ubuntu 24.04):

```bash
sudo apt install cmake ninja-build gcc g++ python3-venv
python3 -m venv ~/.venvs/satlink && source ~/.venvs/satlink/bin/activate   # Ubuntu 24.04: no global pip

cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug

pip install -r tools/requirements.txt
python3 -m pytest tools                      # generator and traceability tool tests
python3 tools/regmap/regmap_gen.py --check   # generated headers match icd/
python3 tools/trace/trace_matrix.py --check  # traceability matrix is current
```

Cross builds (install the matching toolchain first):

```bash
cmake --preset linux-arm && cmake --build --preset linux-arm   # gcc-arm-linux-gnueabihf
cmake --preset rtos-a9   && cmake --build --preset rtos-a9     # gcc-arm-none-eabi
cmake --preset hkc-riscv && cmake --build --preset hkc-riscv   # gcc-riscv64-unknown-elf
```

Linux image: see [yocto/README.md](yocto/README.md).

## Hardware

Digilent Zybo Z7-20, 2x Digilent PmodCAN, 2x 3.3 V USB-UART adapters, 3.5 mm audio cable,
twisted pair with 120 Ω terminations. Pin-out and wiring: [hw/README.md](hw/README.md).

## License

MIT, see [LICENSE](LICENSE).
