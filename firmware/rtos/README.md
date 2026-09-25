# Modem firmware (FreeRTOS, Cortex-A9 Core 1)

FreeRTOS 11.1 (GCC/ARM_CA9 port) running the software modem on Core 1. Linux loads it with the
`satlink_amp` driver; design notes in [docs/stages/stage-4.md](../../docs/stages/stage-4.md) and
[ADR-0005](../../docs/architecture/adr/0005-amp-without-remoteproc.md).

```
bsp/startup.S     vectors, cache/TLB invalidation, mode stacks, VFP, .bss
bsp/mmu.c         translation table built from the address map (only owned regions mapped)
bsp/gic.c         per-SPI routing to this core, IRQ dispatch for the FreeRTOS port
bsp/timer.c       tick from the private timer, microseconds from the global timer
bsp/uart.c        console on PS UART0 (Pmod JE), minimal printf
bsp/fault.c       aborts, asserts, stack overflow -> fault report in shared memory
dma/              AXI DMA scatter-gather rings (host-tested)
app/ipc.c         shared control block, IPC rings, doorbells
app/payload.c     payload_ctrl and axi_dma_modem streams
app/modem_task.c  event loop owning the modem application
app/monitor.c     heartbeat, CPU load, stack margins, standalone self-test
linker/rtos.ld    rtos_fw (code, data, stacks) and modem_dma (DMA buffers)
```

```bash
cmake --preset rtos-a9 && cmake --build --preset rtos-a9
python3 tools/rtos/qemu_selftest.py build/rtos-a9/firmware/rtos/satlink_rtos.elf
```

The QEMU run needs no board: without Linux the firmware sets up the IPC rings itself, plays the
Linux side, runs the modem in software loopback and prints `SELFTEST PASS`.
