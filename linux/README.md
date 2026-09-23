# Linux software (Cortex-A9 Core 0)

Stage 2: platform drivers for the Linux-owned PL blocks, device-tree bindings, the C++ HAL, and
bring-up of the peripherals Linux owns directly. Stage 5 adds the payload manager on top.

```
drivers/ccsds_frame_accel/   out-of-tree platform driver: DT-bound, char device, IRQ, dmaengine
hal/                          satlink::hal - typed C++20 wrapper over the driver char devices
include/uapi/satlink/         ioctl structs shared verbatim by kernel and user space
dts/                           PL device-tree fragment for the Linux-owned blocks
```

Exit criteria and current status: [docs/stages/stage-2.md](../docs/stages/stage-2.md).

## Building the kernel module

Out-of-tree, against the Yocto eSDK (`bitbake satlink-image -c populate_sdk`, see
[yocto/README.md](../yocto/README.md)):

```bash
source /opt/satlink/5.x/environment-setup-cortexa9t2hf-neon-satlink-linux-gnueabi
make -C linux/drivers/ccsds_frame_accel
```

In the Yocto build itself it's `yocto/meta-satlink/recipes-kernel/satlink-ccsds-frame-accel`,
which builds this same source directory via `externalsrc` rather than a separate copy.

## Still open in Stage 2

- `spec_tap` platform driver + HAL (same shape as `ccsds_frame_accel`)
- SSM2603 codec bring-up over I2C
- MCP2515 as SocketCAN on `ps_spi1`
- A diagnostic CLI exercising both HAL classes
- Board bring-up once hw-v1's XSA exists (Stage 1 item 11)
