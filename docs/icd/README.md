# Interface Control Document (ICD)

Version 0.3 (Stage 4). Register maps and the address map are generated from
[`icd/`](../../icd/); this page defines the conventions and the interfaces that are not
registers.

| Part | Source | Status |
|---|---|---|
| [Address map, interrupts, DDR partitions](generated/address_map.md) | `icd/address_map.yaml` | provisional until hw-v2 export |
| [payload_ctrl registers](generated/payload_ctrl.md) | `icd/regmap/payload_ctrl.yaml` | draft |
| [ccsds_frame_accel registers](generated/ccsds_frame_accel.md) | `icd/regmap/ccsds_frame_accel.yaml` | draft |
| [spec_tap registers](generated/spec_tap.md) | `icd/regmap/spec_tap.yaml` | draft |
| Stream formats | this page | draft |
| AMP IPC protocol v1 (Linux ↔ FreeRTOS) | this page, `libs/amp` | implemented (Stage 4) |
| CAN protocol v1 (Linux ↔ housekeeping MCU) | this page, `libs/hkc_proto` | implemented (Stage 3) |

## 1. Conventions

- All registers are 32 bits wide, little-endian, on 4-byte aligned offsets. Byte access is not
  supported.
- Access types: `RO` read-only, `RW` read/write, `WO` write-only, `W1C` write 1 to clear.
- Bits not assigned to a field read as 0 and must be written as 0.
- Every block has `VERSION` at offset 0x00 (`MAJOR[31:16]`, `MINOR[15:0]`). Drivers check
  `MAJOR` at probe time and refuse unknown major versions.

## 2. Stream formats (AXI4-Stream)

| Stream | Width | Format | Rate |
|---|---|---|---|
| TX symbols (axi_dma_modem MM2S) | 32 bit | `[15:0]` I, `[31:16]` Q, signed 16 bit; unit symbol amplitude = 16384 (-6 dBFS, headroom for the RRC overshoot) | 6 kSym/s |
| RX baseband (axi_dma_modem S2MM) | 32 bit | `[15:0]` I, `[31:16]` Q, signed 16 bit, same scaling; after the RRC matched filter, decimated to 2 samples per symbol; TLAST every `RX_FRAME_LEN` samples | 12 kS/s |
| Codec samples (internal) | 32 bit | `[15:0]` left, `[31:16]` right, signed 16 bit | 48 kS/s |
| Frames (axi_dma_frame) | 8 bit | one CCSDS transfer frame per packet (TLAST on the last byte) | burst |
| Spectrum (axi_dma_spec S2MM) | 32 bit | `[15:0]` Re, `[31:16]` Im, 1024 bins in natural order, scaled by 1/1024 | on demand |

A 12 kHz tone appears in FFT bin 256 (48 kHz / 1024 points = 46.875 Hz per bin).

## 3. AMP IPC protocol v1 (Linux ↔ FreeRTOS)

Design: [ADR-0005](../architecture/adr/0005-amp-without-remoteproc.md). The shared partition
`ipc_shm` (1 MiB at 0x39000000) holds a control block and two single-producer/single-consumer
rings ([`libs/amp/include/satlink/amp/shm.h`](../../libs/amp/include/satlink/amp/shm.h)):

| Offset | Size | Content |
|---|---|---|
| 0x00000 | 4 KiB | Control block: magic, version, firmware state, heartbeat, fault code/address, boot count, doorbell IRQs, firmware version |
| 0x01000 | 256 KiB | Ring Linux → RTOS |
| 0x41000 | 256 KiB | Ring RTOS → Linux |

Ring record: `u16 length | u16 type | u32 sequence | payload` padded to 4 bytes, little-endian.
Doorbells: Linux sets IRQ 84 pending, the firmware sets IRQ 85 pending (unused PL-to-PS lines
IRQ_F2P[8] and [9]).

Messages ([`libs/amp/include/satlink/amp/msg.h`](../../libs/amp/include/satlink/amp/msg.h)):

| Type | Direction | Name | Payload |
|---|---|---|---|
| 0x0001 | L → R | `PING` | token (u32), 0 (u32) |
| 0x8001 | R → L | `PONG` | token (u32), firmware uptime in ms (u32) |
| 0x0010 | L → R | `MODEM_CONFIG` | TX enable, RX enable, MODCOD, loopback (analog / digital / software), 1 byte each |
| 0x0011 | L → R | `CHANNEL` | emulator noise level (u16), gain Q15 (u16) |
| 0x0012 | L → R | `ACM_CONFIG` | enable (u8), min MODCOD (u8), max MODCOD (u8), margin (s16, 0.01 dB), hysteresis (s16, 0.01 dB) |
| 0x0013 | L → R | `TIME` | Unix time in ms (u64) |
| 0x0020 | L → R | `TX_FRAME` | 128 bytes for the next data frame |
| 0x8020 | R → L | `RX_FRAME` | MODCOD (u8), CRC ok (u8), Es/N0 (s16, 0.01 dB), frame sequence (u8), 128 bytes |
| 0x8030 | R → L | `STATUS` (1 Hz) | 12 counters (u32): uptime, frames ok / CRC error / header error, bit errors, bits checked, TX data / idle frames, DMA underruns, RX overruns, RX latency max / avg (µs); Es/N0 (s16), locked, TX MODCOD, ACM enabled, TX queue depth (u8 each), CPU load (u16, 0.1 %) |
| 0x8040 | R → L | `LOG` | level (u8), text (up to 120 bytes) |

Linux user space reaches the rings through `/dev/satlink-amp` (one message per `read()` /
`write()`, `struct satlink_amp_msg_hdr` + payload, see
[`linux/include/uapi/satlink/amp.h`](../../linux/include/uapi/satlink/amp.h)).

## 4. CAN protocol v1 (Linux ↔ housekeeping MCU)

500 kbit/s, 11-bit identifiers, multi-byte fields **little-endian**
([`libs/hkc_proto`](../../libs/hkc_proto/include/satlink/hkc/hk_proto.h)).

| ID | Direction | Name | Payload |
|---|---|---|---|
| 0x080 | Linux → MCU | `TIME_SYNC` | Unix seconds (u32), milliseconds (u16) |
| 0x100 | MCU → Linux | `HK_ENV` (period, default 1 s) | die temperature (s16, 0.01 °C), VCCINT, VCCAUX, VBRAM (u16, mV) |
| 0x101 | MCU → Linux | `HK_SUPPLY` | VCCPINT, VCCPAUX, VCCO_DDR (u16, mV) |
| 0x102 | MCU → Linux | `HK_STATUS` | uptime (u32, s), reset cause, switches, commands accepted, error flags (u8 each) |
| 0x180 | Linux → MCU | `HK_CMD` | opcode (u8), arguments: SET_LED, SET_PERIOD, ENTER_BOOT (key B0 07), GET_VERSION, CLEAR_ERRORS |
| 0x181 | MCU → Linux | `HK_ACK` | opcode (u8), status (u8), data |
| 0x7E0 | Linux → MCU | `BOOT_REQ` | bootloader: PING, START (size), DATA (seq + 6 bytes), END (CRC-32), BOOT, ABORT |
| 0x7E8 | MCU → Linux | `BOOT_RESP` | opcode \| 0x80, status, data |

Application images: 32-byte header (magic `SLHK`, load address, entry, size, version, payload
and header CRC-32), see [`image.h`](../../libs/hkc_proto/include/satlink/hkc/image.h).
