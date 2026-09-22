# Interface Control Document (ICD)

Version 0.1 (Stage 1, draft). Register maps and the address map are generated from
[`icd/`](../../icd/); this page defines the conventions and the interfaces that are not
registers.

| Part | Source | Status |
|---|---|---|
| [Address map, interrupts, DDR partitions](generated/address_map.md) | `icd/address_map.yaml` | provisional until hw-v2 export |
| [payload_ctrl registers](generated/payload_ctrl.md) | `icd/regmap/payload_ctrl.yaml` | draft |
| [ccsds_frame_accel registers](generated/ccsds_frame_accel.md) | `icd/regmap/ccsds_frame_accel.yaml` | draft |
| [spec_tap registers](generated/spec_tap.md) | `icd/regmap/spec_tap.yaml` | draft |
| Stream formats | this page | draft |
| RPMsg protocol v0 | this page | draft, finalized in Stage 4 |
| CAN protocol v0 | this page | draft, finalized in Stage 3 |

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
| TX symbols (axi_dma_modem MM2S) | 32 bit | `[15:0]` I, `[31:16]` Q, signed Q1.15 | 6 kSym/s |
| RX baseband (axi_dma_modem S2MM) | 32 bit | `[15:0]` I, `[31:16]` Q, signed Q1.15; TLAST every `RX_FRAME_LEN` samples | 12 kS/s |
| Codec samples (internal) | 32 bit | `[15:0]` left, `[31:16]` right, signed 16 bit | 48 kS/s |
| Frames (axi_dma_frame) | 8 bit | one CCSDS transfer frame per packet (TLAST on the last byte) | burst |
| Spectrum (axi_dma_spec S2MM) | 32 bit | `[15:0]` Re, `[31:16]` Im, 1024 bins in natural order, scaled by 1/1024 | on demand |

A 12 kHz tone appears in FFT bin 256 (48 kHz / 1024 points = 46.875 Hz per bin).

## 3. RPMsg protocol v0 (Linux ↔ FreeRTOS), draft

Channel name `satlink-modem`. Every message starts with a common header (little-endian):

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 2 | `type` | Message type |
| 2 | 2 | `length` | Payload length in bytes |
| 4 | 4 | `seq` | Sequence number, incremented by the sender |

Planned types: `MODEM_CONFIG` (L→R), `MODEM_STATUS` (R→L, 1 Hz), `TX_FRAME` (L→R),
`RX_FRAME` (R→L), `ACM_EVENT` (R→L), `TIME_SYNC` (L→R).

## 4. CAN protocol v0 (Linux ↔ housekeeping MCU), draft

500 kbit/s, 11-bit identifiers, payload big-endian.

| ID | Direction | Name | Payload |
|---|---|---|---|
| 0x100 | MCU → Linux | `HK_TEMP_VCC` (1 Hz) | die temperature (int16, 0.01 °C), VCCINT (uint16, mV), VCCAUX (uint16, mV), flags (uint8), sequence (uint8) |
| 0x101 | MCU → Linux | `HK_SUPPLIES` (1 Hz) | VCCBRAM, VCCPINT, VCCPAUX, VCCO_DDR (4x uint16, mV) |
| 0x200 | Linux → MCU | `HK_CMD` | opcode (uint8), arguments |
| 0x201 | MCU → Linux | `HK_ACK` | opcode (uint8), status (uint8) |
| 0x7F0 | Linux → MCU | `BL_CMD` | bootloader commands: START (size, CRC-32, version), DATA (sequence + 6 bytes), END, JUMP |
| 0x7F1 | MCU → Linux | `BL_RESP` | bootloader responses |
