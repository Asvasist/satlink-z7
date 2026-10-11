# Hardware (Vivado)

Vivado 2025.2, Digilent Zybo Z7-20 (`xc7z020clg400-1`). Only the Tcl script and constraints are
committed; the project is regenerated into `hw/vivado/` (git-ignored).

```
hw/scripts/create_project.tcl   recreates the project and block design
hw/scripts/run_selftest.tcl     JTAG bring-up: bitstream, PS init, Core 1 self-test
hw/xdc/satlink_zybo_z7_20.xdc   pin constraints
```

## Baseline hw-v1 (current)

| Block | Contents | Owner (ICD) |
|---|---|---|
| B1 PS7 | Board preset; UART0 via EMIO (Pmod JE1/JE2), I2C0 via EMIO (codec), SPI1 on MIO 10..15 (PmodCAN #1, INT on MIO0), TTC1, SWDT, FCLK0 = 100 MHz, M_AXI_GP0, IRQ_F2P[9:0] | Linux / Core 1 per `icd/address_map.yaml` |
| B2 AXI GPIO | `axi_gpio_0` at 0x41200000: LD0-3 (ch1), BTN0-3 (ch2), IRQ 64 | Core 1 |
| B3 Housekeeping controller | MicroBlaze V (rv32imc), 128 KiB LMB BRAM with the bootloader baked in, AXI INTC, UART Lite 115200, GPIO (SW0-3, RGB LED6), timer, timebase watchdog (resets this subsystem only), Quad SPI (PmodCAN #2 on JB), XADC Wizard | HKC |
| B6 Glue | Interrupts and addresses exactly as in the ICD, constraints | - |

IRQ_F2P[3] (GIC 64) is `axi_gpio_0`; IRQ_F2P[8]/[9] (GIC 84/85) are tied low because both cores
pend the AMP doorbells in software (ADR-0005). The other lines are reserved for hw-v2.

Not in hw-v1: the Linux PL accelerators (`ccsds_frame_accel`, `spec_tap` + FFT) and the modem
datapath (`payload_ctrl`, I2S, NCOs, RRC filters, channel emulator, AXI DMAs). Until they exist,
build the Linux image without the PL device tree (`SATLINK_PL_DT=0`).

The XADC belongs to the HKC (XADC Wizard in the PL); the Linux device tree must not enable the
PS XADC interface as well.

## Build

The bootloader goes into the bitstream, so build it first (Vitis RISC-V toolchain on `PATH`):

```bash
cmake --preset hkc-riscv && cmake --build --preset hkc-riscv
vivado -mode batch -source hw/scripts/create_project.tcl
```

On Windows keep the repository at a short path such as `D:\ANEK_GIT\satlink-z7`: generated IP
paths under `hw/vivado/` exceed the 260-character limit from about 60 characters of prefix.

Open `hw/vivado/satlink_hw/satlink_hw.xpr`, then Generate Bitstream. The four critical warnings
about negative `PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY` values come from the Digilent board preset
(the board's DDR trace delays) and are expected. Export the hardware with
*File > Export > Export Hardware > Include bitstream* for Vitis and Yocto.

After a confirmed export, compare the Address Editor with `icd/address_map.yaml`, set the
confirmed entries to `status: frozen` and tag the commit `hw-v1`.

## Bring-up over JTAG

Board jumper JP5 on JTAG, PROG/UART USB cable connected, a 3.3 V USB-UART adapter on Pmod JE
(adapter RX to the board TX pin, adapter TX to the board RX pin, GND to JE5).

```bash
cmake --preset rtos-a9 && cmake --build --preset rtos-a9
xsdb hw/scripts/run_selftest.tcl
```

| Check | Where | Expected |
|---|---|---|
| HKC alive | RGB LED6 | blue: the bootloader runs from BRAM. Without a PmodCAN on JB it should turn red ("MCP2515 init failed"); with the current bootloader it stays blue (SPI transfer hang, see the [bring-up report](../docs/bringup/2026-10-11-hw-v1-first-boot.md)) |
| HKC console | adapter on JE7 (board TX) / JE8 (board RX) | `SatLink-Z7 HKC bootloader 1.0` |
| Core 1 firmware | adapter on JE1 (board TX) / JE2 (board RX) | banner with `standalone`, then `SELFTEST PASS` after about 15 s |

## External wiring

| Connector | Pins | Function |
|---|---|---|
| USB-UART (J12) | on board | Linux console (PS UART1, ttyPS0) |
| Pmod JE | JE1 (V12) board TX, JE2 (W16) board RX | FreeRTOS console, PS UART0 via EMIO |
| Pmod JE | JE7 (V13) board TX, JE8 (U17) board RX | MicroBlaze V console (hkc_uart) |
| Pmod JF | JF1-4 = MIO13/10/11/12, JF7 = MIO0 | PmodCAN #1 on PS SPI1 (CS/MOSI/MISO/SCK), INT on MIO0 |
| Pmod JB | JB1 V8 CS, JB2 W8 MOSI, JB3 U7 MISO, JB4 V7 SCK, JB7 Y7 INT | PmodCAN #2 on the MicroBlaze V Quad SPI |
| Audio | HPH OUT → LINE IN | 3.5 mm loopback cable: the "RF" link (hw-v2) |
| CAN | PmodCAN #1 ↔ PmodCAN #2 | twisted pair, 120 Ω at both ends |
