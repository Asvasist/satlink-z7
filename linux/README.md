# Linux software (Cortex-A9 Core 0)

Stage 2: platform drivers for the Linux-owned PL blocks, device-tree fragments, the C++ HAL, the
peripherals Linux owns directly, and a diagnostic tool. Stage 3 adds the housekeeping and boot
tools, Stage 4 the AMP driver and client, Stage 5 the payload manager on top.

```
drivers/ccsds_frame_accel/   platform driver: DT-bound, char device, IRQ, dmaengine
drivers/spec_tap/            platform driver: DT-bound, char device, dmaengine
drivers/satlink_amp/         AMP: loads and supervises the Core 1 firmware, IPC rings as a char
                             device, doorbell IRQs, heartbeat watchdog with restart
amp/                         ModemClient, SimulatedCore1, /dev/satlink-amp backend, satlink-amp
hkc/                         satlink-hkc: housekeeping controller over SocketCAN, CAN flasher
bootctl/                     satlink-bootctl: A/B boot slots through the U-Boot environment
hal/                         satlink::hal - typed C++20 wrappers (frame accelerator, spectrum
                             tap, SSM2603 codec) over abstract CharDeviceIo / I2cBus interfaces
diag/                        satlink-diag: command logic (portable) and the Linux executable
include/uapi/satlink/        ioctl structs shared verbatim by the kernel and user space
dts/                         PL, PS and AMP device-tree fragments and the board trees
scripts/                     CAN bring-up and loopback test (can-utils)
```

Exit criteria, design notes and the bring-up checklist: [docs/stages/stage-2.md](../docs/stages/stage-2.md).

## satlink-diag

```
satlink-diag fa version | ctrl [randomizer=on|off] [irq=on|off] | wait [timeout_ms=N]
satlink-diag spec version | ctrl [enable=on|off] [window=on|off] | status | clear
satlink-diag codec init [wordlength=16|20|24|32] | volume <0-127> | mute on|off
```

`--dev PATH` overrides the device node. Exit codes: 0 ok, 1 device error, 2 usage error,
3 timeout. Run `codec init` once after boot, then `codec volume` / `codec mute`.

## CAN

```bash
linux/scripts/can-loopback-test.sh can0    # MCP2515 internal loopback, no second node needed
linux/scripts/can-up.sh can0 500000        # normal mode, for the two-node test in Stage 3
```

## Building the kernel modules

Out-of-tree, against a configured kernel build tree, for example the one from the Yocto SDK:

```bash
make -C linux/drivers/ccsds_frame_accel KERNEL_SRC=/path/to/kernel-build ARCH=arm \
     CROSS_COMPILE=arm-linux-gnueabihf-
```

In the Yocto build they are `satlink-ccsds-frame-accel` and `satlink-spec-tap`, which build these
same directories with `externalsrc`. `satlink-diag` is in the `satlink-tools` recipe, and the
`linux-arm` CMake preset builds it as well.

## Licence note

The kernel modules are GPL-2.0 (they use GPL-only kernel symbols); everything else in the
repository is MIT.
