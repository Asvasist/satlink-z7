# Stage 4: AMP and the real-time modem

Goal: a software-defined modem on Cortex-A9 Core 1 under FreeRTOS, started and supervised by
Linux on Core 0, with the whole signal chain (framing, FEC, synchronisation) verified in
simulation and the firmware itself booted in CI.

## Exit criteria

| # | Criterion | Requirement | Status |
|---|---|---|---|
| 1 | CCSDS K=7 convolutional code, 4 punctured rates, soft Viterbi | SRS-MDM-001 | done (13 tests) |
| 2 | BPSK/QPSK/8PSK, soft demapping, 5 MODCODs | SRS-MDM-002 | done |
| 3 | RRC pulse shaping, reference taps for the PL | SRS-MDM-003 | done |
| 4 | PL frame: ASM, protected header, pilots, randomizer, CRC, idle PN9 frames | SRS-MDM-004 | done (10 tests) |
| 5 | Receiver: timing, frame sync, carrier PLL, Es/N0, decoding; every MODCOD at threshold + 1 dB | SRS-MDM-005 | done (16 link tests) |
| 6 | Channel model (gain, phase, frequency, fractional delay, AWGN) | SRS-MDM-006 | done |
| 7 | Modem application (messages, TX queue, idle frames, status, software loopback) | SRS-MDM-007 | done (8 tests) |
| 8 | FreeRTOS 11.1 on Core 1: startup, MMU, GIC, tick, UART, tasks | SRS-AMP-001 | done, boots in QEMU in CI |
| 9 | Shared-memory rings (one source for kernel, firmware, host) | SRS-AMP-002 | done (7 tests, 100 000-message two-thread stress) |
| 10 | Message set, `/dev/satlink-amp`, C++ client, `satlink-amp` tool | SRS-AMP-003 | done (5 client tests); tool cross-compiles |
| 11 | MMU enforces ownership; Linux reserves Core 1 memory | SRS-AMP-004 | done; live table checked by the QEMU self-test |
| 12 | Fault reports, heartbeat, automatic restart | SRS-AMP-005 | written; restart path needs the board |
| 13 | AXI DMA scatter-gather rings, payload_ctrl driver | SRS-AMP-006 | done (6 tests against an engine model); PL pending |
| 14 | Self-test in QEMU, simulated Core 1 for host software | SRS-AMP-007 | done |
| 15 | satlink_amp kernel module, AMP device tree | SRS-AMP-001/002/005 | builds and links warning-free (W=1) against linux-xlnx 6.6 (all symbols exported); DTBs compile; CI job; needs the board |
| 16 | On the board: firmware starts at boot, doorbells, PL loopback, restart after an injected fault | all | pending (needs the board; the PL datapath needs hw-v2) |

## Design

```
Core 0: Linux                                  Core 1: FreeRTOS                          PL
+------------------------------+  ipc_shm      +-------------------------------+
| satlink-payloadd / satlink-amp|  rings + IRQ | modem task (owns modem_app)   |  axi_dma_modem
|   ModemClient                 | <==========> |   drain IPC, TX symbols,      | <============> payload_ctrl
| /dev/satlink-amp              |  84 / 85     |   RX samples -> receiver      |  symbols / samples
| satlink_amp.ko: load, start,  |              | monitor task: heartbeat, load,|
|   heartbeat watchdog, restart |              |   stack margins, self-test    |
+------------------------------+               +-------------------------------+
```

- **No remoteproc on this kernel.** AMD's 6.6 kernel has no Zynq-7000 remoteproc driver, so
  `satlink_amp` loads and supervises the firmware itself and the IPC is two shared-memory rings
  ([ADR-0005](../architecture/adr/0005-amp-without-remoteproc.md)).
- **Everything that can run on the host does.** The modem (`libs/modem`), the application
  (`libs/modem_app`), the rings and messages (`libs/amp`) and the DMA ring logic
  (`firmware/rtos/dma`) have no OS or register access. The firmware is thin glue: about 1 900 lines
  of BSP and tasks around them.
- **One task owns the modem.** Interrupts (IPC doorbell, DMA completions) only set notification
  bits; the modem task does all the work in order, so the modem code needs no locks.
- **Real time in software loopback.** Without the PL the modem task produces what 6 kSym/s
  would have produced since its last run, so the link runs at its real rate: about 7.5
  frames/s at QPSK 3/4, 4 % CPU on a Cortex-A9 in QEMU.
- **2 samples per symbol on the RX side.** The PL delivers the matched-filtered baseband
  decimated to 12 kS/s (ICD section 2). Timing recovery is Gardner with an 8-tap windowed-sinc
  polyphase interpolator; the noiseless Es/N0 floor is 39 dB.
- **Memory isolation.** The firmware's translation table maps only what the address map gives
  it (1 MiB sections, 4 KiB pages where Linux and RTOS blocks share a megabyte); Linux reserves
  those regions with `no-map`. The QEMU self-test walks the live table and checks it.

## Measured (simulation and QEMU)

| What | Value |
|---|---|
| Frames decoded at MODCOD threshold + 1 dB (25 frames, AWGN, timing and phase offsets) | >= 22 / 25 for every MODCOD |
| Es/N0 estimate error at threshold + 1 dB | < 1.5 dB |
| Noiseless Es/N0 floor (implementation loss) | 39 dB |
| QEMU self-test: data frames through the IPC rings and the modem | 8 / 8, 0 bit errors in 96 256 idle bits |
| CPU load of the modem at 6 kSym/s (QEMU) | about 4 % |

Link performance per MODCOD over the whole Es/N0 range is measured in Stage 6 (performance
report).

## To verify on the board

- [ ] `satlink_amp` probes, `remove_cpu(1)` succeeds, the firmware reports RUNNING within 2 s
- [ ] The trampoline start works after a warm restart (`echo stop/start > .../state`)
- [ ] Doorbells: the IRQ counter in `satlink-amp status` rises with traffic in both directions
- [ ] `satlink-amp ping`, `monitor` in software loopback (no PL needed)
- [ ] Injected fault (e.g. a debug command writing to Linux memory) is reported and Core 1 is
      restarted automatically
- [ ] With hw-v2: digital loopback, then the 3.5 mm analog loopback; `payload_ctrl` version,
      DMA underruns/overruns stay at 0, RX latency within one DMA block
- [ ] Bring-up report from [docs/bringup/TEMPLATE.md](../bringup/TEMPLATE.md)
