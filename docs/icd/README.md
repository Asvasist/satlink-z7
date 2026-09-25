# Interface Control Document (ICD)

Version 0.5 (Stage 6). Register maps and the address map are generated from
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
| Ground interface: CCSDS/PUS over UDP, TM frames | this page, `libs/pus`, `linux/payload/.../mission.hpp` | implemented (Stage 5, SID 4 in Stage 6) |
| Instrument interface: SCPI over TCP | this page, `libs/scpi` | implemented (Stage 6) |

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
| 0x8050 | R → L | `CONSTELLATION` (with `STATUS`, after a new frame) | MODCOD (u8), count N ≤ 64 (u8), N × (I s8, Q s8); symbols after gain and carrier correction, spread over the last frame's payload, 64 = unit amplitude |

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

## 5. Ground interface (CCSDS Space Packets, PUS-C)

Telecommands: UDP to the payload, port **10025**, one Space Packet per datagram. Telemetry: UDP
to the ground station, port **10026** (to the configured station, or to the sender of the last
TC). Packets: CCSDS 133.0-B with PUS-C (ECSS-E-ST-70-41C) secondary headers and a CRC-16-CCITT
packet error control field ([`libs/pus`](../../libs/pus/include/satlink/pus/space_packet.hpp)).
All fields big-endian.

| Item | Value |
|---|---|
| Spacecraft ID (TM frames) | 0x2A7 |
| APID of the payload manager (TC and TM) | 0x010 |
| IP datagrams satellite → ground / ground → satellite | APID 0x3F0 / 0x3F1 (no secondary header) |
| Ground station source/destination ID | 0x0001 |
| TM time | CUC, 4 + 2 bytes, epoch 2000-01-01T00:00:00Z |

**Downlink.** TM packets travel through the modem: packed into 128-byte TM Transfer Frames
(CCSDS 132.0-B, [`tm_frame.hpp`](../../libs/pus/include/satlink/pus/tm_frame.hpp)) on
virtual channel 0, IP packets on VC 1, idle frames on VC 7; one transfer frame is one modem
frame. After the RF loopback the frames are demultiplexed and the TM is forwarded over UDP. While
the link is down (no lock, or the emulated satellite below the horizon) packets are stored and
downlinked when contact returns. Telecommands reach the payload over Ethernet directly.

**Services** (APID 0x010):

| TC | TM | Meaning |
|---|---|---|
| — | 1,1 / 1,2 | acceptance success / failure (TC packet ID u16, sequence control u16, failure code u16) |
| — | 1,7 / 1,8 | completion success / failure (same layout) |
| 3,5 / 3,6 | — | enable / disable periodic housekeeping: N (u8), SIDs (u8 each) |
| 3,27 | 3,25 | one-shot report: N (u8), SIDs |
| 3,31 | — | collection interval: SID (u8), period ms (u32, 100..60000) |
| — | 3,25 | housekeeping report: SID (u8), parameters (below) |
| — | 5,1..5,4 | event (informative, low, medium, high): event ID (u16), auxiliary data |
| 8,1 | — | perform function: function ID (u8), arguments (below) |
| 17,1 | 17,2 | are-you-alive |

Success reports are sent when the TC's acknowledgement flags ask for them; failures always.
Failure codes: 1 unknown service, 2 unknown subtype, 3 bad data, 4 unknown function, 5 modem
error, 6 bad APID, 7 corrupt packet.

Functions (TC[8,1]):

| ID | Name | Arguments |
|---|---|---|
| 1 | SET_MODCOD | MODCOD u8 (turns ACM off) |
| 2 | SET_ACM | enable u8, min u8, max u8, margin s16 (0.01 dB), hysteresis s16 (0.01 dB) |
| 3 | SET_CHANNEL | noise level u16, gain Q15 u16 (stops a running pass) |
| 4 | START_PASS | max elevation u16 (0.01°, 6..90°), zenith Es/N0 s16 (0.01 dB), time scale u8 |
| 5 | STOP_PASS | — |
| 6 | SET_LOOPBACK | 0 analog, 1 digital, 2 software |
| 7 | RESTART_MODEM | — (restarts the Core 1 firmware) |

Housekeeping structures (TM[3,25], after the SID byte):

| SID | Parameters |
|---|---|
| 1 modem | locked u8, TX MODCOD u8, ACM u8, Es/N0 s16 (0.01 dB), frames ok u32, CRC errors u32, header errors u32, bit errors u32, bits checked u32, RX latency max u32 / avg u32 (µs), CPU load u16 (0.1 %), TX queue u8, firmware uptime u32 (ms) |
| 2 link | pass active u8, elevation s16 (0.01°), range u16 (km), range rate s16 (m/s), model Es/N0 s16 (0.01 dB), frames sent u32, frames received u32, lost frames u32, packets u32, resyncs u32, IP down u32, IP up u32, packets dropped u32 |
| 3 platform | HKC valid u8, die temperature s16 (0.01 °C), VCCINT u16, VCCAUX u16, VBRAM u16 (mV), HKC uptime u32 (s), HKC error flags u8, Core 1 state u8, Core 1 restarts u32 |
| 4 constellation | MODCOD u8, count N u8, N × (I s8, Q s8) in 1/64 of the unit symbol amplitude. **Disabled by default**: 131 bytes per report, about 1 kbit/s at the default period, a third of the BPSK 1/2 downlink |

SIDs 1 to 3 are enabled at start-up with a period of 1 s.

## 6. Instrument interface (SCPI)

TCP port **5025**, one program message per line (LF), responses terminated by LF. SCPI-1999 and
IEEE 488.2 behaviour: [`libs/scpi`](../../libs/scpi/include/satlink/scpi/parser.hpp); command
set: [`scpi_instrument.hpp`](../../linux/payload/include/satlink/payload/scpi_instrument.hpp).

| Group | Commands |
|---|---|
| IEEE 488.2 | `*IDN?` `*RST` `*TST?` `*CLS` `*ESE` `*ESR?` `*OPC` `*OPC?` `*SRE` `*STB?` `*WAI` |
| System | `SYSTem:ERRor[:NEXT]?` `SYSTem:ERRor:COUNt?` `SYSTem:ERRor:ALL?` `SYSTem:VERSion?` `SYSTem:COUNters?` |
| Modem | `MODem:MODCod` `MODem:ACM[:STATe]` `MODem:ACM:LIMits` `MODem:ACM:MARGin` `MODem:ACM:HYSTeresis` `MODem:LOOPback` `MODem:RESTart` (settings also as queries) |
| Channel emulator | `CHANnel:ESN0` `CHANnel:NOISe` `CHANnel:GAIN` `CHANnel:CLEar` |
| Measurements | `MEASure:ESN0?` `MEASure:LOCK?` `MEASure:MODCod?` `MEASure:BER?` `MEASure:FER?` `MEASure:COUNters?` `MEASure:LATency?` `MEASure:LOAD?` `MEASure:RESet` |
| Pass emulation | `PASS:STARt [<max el>[,<zenith Es/N0>[,<time lapse>]]]` `PASS:STOP` `PASS:STATe?` |

`*IDN?` answers `SatLink-Z7,Payload Modem,<serial>,<version>`. Not-a-number is `9.91E+37`,
±infinity `±9.9E+37`. BER and FER cover the window since the last `MEASure:RESet`.

Events: 1 link locked, 2 link lost, 3 MODCOD changed (from u8, to u8), 4 AOS, 5 LOS,
6 firmware log (text), 7 modem restarted.
