# Bring-up report: hw-v1 first boot, AMP and Core 1 (2026-10-11)

- Hardware: Digilent Zybo Z7-20, hardware baseline hw-v1 ([`hw/scripts/create_project.tcl`](../../hw/scripts/create_project.tcl), Vivado 2025.2)
- Image: `satlink-image`, Yocto 5.0.21 (scarthgap), AMD release 2024.2 (linux-xlnx 6.6.40,
  u-boot-xlnx 2024.01), signed FIT, A/B layout; firmware as of PR #4
- Equipment: PROG/UART micro-USB (Linux console ttyPS0, 115200 8N1, and JTAG through the same
  cable), Ethernet to a router. No USB-UART adapter (Core 1 and HKC consoles not visible), no
  PmodCAN, no oscilloscope.

## Objective

Boot the hw-v1 bitstream and the Yocto image from SD through the signed boot chain, start the
FreeRTOS firmware on Core 1 from Linux, and exchange messages between the two cores.
Verifies on the board: SRS-BSP-003 (boot from SD), SRS-BOOT-001/002 (signed FIT), SRS-AMP-001
(Core 1 started by Linux), SRS-AMP-002 (IPC rings and doorbells).

## Setup

- JP5 on SD. `BOOT.BIN` from [`yocto/bootbin/satlink-ab.bif`](../../yocto/bootbin/satlink-ab.bif):
  FSBL (Vitis 2025.2, from the hw-v1 XSA), hw-v1 bitstream, U-Boot with the signed device tree
  (`u-boot.bin` from the Yocto deploy directory).
- SD card written with `satlink-image-zybo-z7-20.rootfs.wic`, `BOOT.BIN` copied onto the FAT
  partition `boot`.

## Procedure and results

| Step | Expected | Observed | Pass |
|---|---|---|---|
| 1 | FSBL and U-Boot start from SD | `U-Boot 2024.01` | yes |
| 2 | U-Boot verifies the signed configuration | `Verifying Hash Integrity ... sha256,rsa2048:satlink-dev+ OK` | yes |
| 3 | Linux boots, login on ttyPS0 | `Linux version 6.6.40-xilinx-...`, `zybo-z7-20 login:` | yes |
| 4 | Ethernet link | `macb e000b000.ethernet end0: Link is Up - 1Gbps/Full` | yes |
| 5 | CPU1 taken from Linux | `/sys/devices/system/cpu/online` = `0` | yes |
| 6 | Core 1 firmware RUNNING at boot | first boots: `firmware did not report RUNNING (state 0)`; after the fixes in PR #4: `Core 1 firmware 1.0.0 running` | yes, after fixes |
| 7 | IPC round trip | `satlink-amp ping`: `PONG: firmware up 62233 ms` | yes |
| 8 | Modem software loopback on Core 1 | `satlink-amp monitor`: `lock=1`, 395 frames ok, 0 CRC errors, 0 bit errors | yes |

## Measurements

| Quantity | Value | How |
|---|---|---|
| Core 1 time to RUNNING before PR #4 (MMU table build with caches off) | 9 to 10 s | `rtos_state` (0x39000008) polled once per second after a manual release |
| Core 1 heartbeat | 662 to 840 in about 18 s (10 Hz monitor task) | `heartbeat` (0x3900000C) |
| IPC traffic after one ping | 1 message to Core 1, 123 from Core 1, 84 doorbells | `satlink-amp status` |
| Modem loopback (MODCOD 1) | 0 bit errors in 404480 bits, Es/N0 38.7 dB | `satlink-amp monitor`, t = 62 to 77 s |
| Core 1 CPU load | 7.3 to 8.3 % | same |
| RX latency | average 387 us, maximum 5487 us | same |

Console logs: [before the fixes](logs/2026-10-10-first-boot.txt),
[after the fixes](logs/2026-10-11-amp-running.txt).

## Conclusion

The signed boot chain, Linux on Core 0 and FreeRTOS on Core 1 run on the board, and the two
cores exchange messages through the IPC rings and doorbells. Core 1 needed three fixes
(PR #4), all visible only on hardware: Linux gated UART0's clocks, the firmware probed a PL
block that hw-v1 does not have, and the MMU table build took about 9 s with caches off.

Open points:

- Check on the next boot that the AMP boot counter starts at 1 (it showed a random value).
- HKC bootloader: LED6 stays blue without a PmodCAN (SPI transfer hang); the fix on branch
  `fix/hkc-spi-hang` still has to go into the bitstream.
- Core 1 and HKC consoles need a 3.3 V USB-UART adapter on Pmod JE.
- CAN tests need two PmodCAN modules.
- A/B update and rollback, watchdog and FDIR tests (stage 3 and stage 6 checklists).
- `satlink-payloadd.service`: `StartLimitIntervalSec` belongs in `[Unit]`, not `[Service]`.
- The kernel version ends in `-dirty` because the device trees are copied into the kernel tree.
