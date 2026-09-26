# Host software (ground segment)

Everything here runs on the PC and talks to the payload over Ethernet. The payload is either the
board or `satlink-payloadd --sim` on the same PC.

| Directory | What | Build / run |
|---|---|---|
| [`gs/`](gs/) | `satlink-gs`: Qt 6 ground station (Windows, Linux) | `cmake -S host/gs -B build/gs && cmake --build build/gs` |
| [`cli/`](cli/) | `satlink`: command-line client in Rust, no dependencies | `cd host/cli && cargo build --release` |
| [`python/`](python/) | `satlink` Python package: PUS client, report decoders, SCPI driver | `pip install -e host/python[visa]` |
| [`hil/`](hil/) | pytest hardware-in-the-loop suite (PUS + SCPI) | `pytest host/hil --target sim` or `--target board --host <ip>` |

Interfaces: [ICD section 5](../docs/icd/README.md#5-ground-interface-ccsds-space-packets-pus-c)
(PUS over UDP 10025/10026) and
[section 6](../docs/icd/README.md#6-instrument-interface-scpi) (SCPI over TCP 5025).

## Ground station

![Ground station](../docs/images/ground-station.png)

- **Left panel.** Every mission telecommand: are-you-alive, MODCOD and ACM, channel emulator
  (Es/N0), LEO pass, loopback, Core 1 restart, housekeeping enable and one-shot.
- **Centre.** Live Es/N0 (measured, and the pass model), MODCOD, BER per second, elevation, and
  the constellation (HK SID 4).
- **Bottom.** Event log, telecommand history with acceptance and completion, and the decoded
  housekeeping of all structures.

```
satlink-gs [--payload HOST[:TC_PORT]] [--tm-port N] [--connect] [--pass] [--constellation]
           [--screenshot FILE --after SECONDS]
```

Windows: install Qt 6 with the Charts module (online installer, or `aqt install-qt windows
desktop 6.7.2 win64_msvc2019_64 -m qtcharts`). Then configure with
`-DCMAKE_PREFIX_PATH=C:/Qt/6.7.2/msvc2019_64` and build with Visual Studio 2022. `cmake --install`
runs `windeployqt` and produces a self-contained folder.

## CLI

```
$ satlink ping
TC 13388: acceptance OK
alive (TM[17,2])
TC 13388: completion OK
$ satlink channel 9 && sleep 3 && satlink hk --sid 1 --count 1
TC 688: acceptance OK
TC 688: completion OK
modem: LOCKED QPSK 3/4 (ACM) Es/N0 8.96 dB frames 67/9 crc bits 1254/54272 load 0.0% latency 0/0 us
$ satlink modcod 7; echo $?
satlink: modcod needs a value 0..4
64
```

(Output against `satlink-payloadd --sim`. The counters are cumulative since the modem started,
and the simulator does not measure load and latency.)

Exit status: 0 done, 1 rejected by the payload (ST[01] failure), 2 no answer, 64 usage error.

## Python

```python
from satlink import PayloadClient, ScpiInstrument
from satlink.mission import ModemHk

with PayloadClient("192.168.1.10") as sat, ScpiInstrument("192.168.1.10") as inst:
    sat.execute(sat.cmd.start_pass(max_elevation_deg=70, time_scale=20))
    hk = sat.wait_for(ModemHk, timeout=3)
    print(hk.modcod, hk.esn0_db, inst.query_float("MEAS:BER?"))
```

## HIL suite

`--target sim` starts `satlink-payloadd --sim` on free ports and runs the same tests that
`--target board` runs against the hardware. Tests marked `board` (PL loopbacks, Core 1 restart,
housekeeping controller on CAN) are skipped in simulation.

| File | Covers |
|---|---|
| `test_instrument.py` | `*IDN?`, `*TST?`, error queue and status byte, compound messages, `*RST` |
| `test_link.py` | error-free link per MODCOD at threshold + 2 dB, loss and recovery of lock, counters |
| `test_acm.py` | ACM settles on the right MODCOD across Es/N0, limits, fade step-down |
| `test_pus.py` | PUS services end to end, rejection reasons, store and forward during an outage |
| `test_pass.py` | a full LEO pass: elevation, MODCOD range, LOS event |
| `test_board.py` | board only |
