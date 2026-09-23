# Yocto BSP

`meta-satlink` is the board support and image layer for the Zybo Z7-20. It is built with
[kas](https://kas.readthedocs.io), which fetches Yocto 5.0 (scarthgap), meta-openembedded,
meta-arm and meta-xilinx.

> **Status:** written but not yet built. Expect small fixes on the first build (layer
> dependencies, meta-xilinx variables); record them in the commit history.

## Build (Linux or WSL2)

Yocto does not run on Windows directly. Under WSL2, keep the build **inside the Linux file
system** (for example `~/work/satlink-z7`), not under `/mnt/d/...`: builds on the Windows drive
are very slow and fail on case-insensitive paths. You need about 100 GB of free space and
16 GB RAM (8 GB works with `BB_NUMBER_THREADS = "4"`).

```bash
sudo apt install gawk wget git diffstat unzip texinfo gcc build-essential chrpath socat \
     cpio python3 python3-pip python3-pexpect xz-utils debianutils iputils-ping python3-git \
     python3-jinja2 python3-subunit zstd liblz4-tool file locales libacl1 qemu-system-arm pipx
sudo locale-gen en_US.UTF-8
pipx install kas==4.7

kas build yocto/kas/zybo-z7-20.yml                 # standard kernel
SATLINK_RT=1 kas build yocto/kas/zybo-z7-20.yml    # with the PREEMPT_RT fragment
```

`SATLINK_PL_DT=1` builds and boots the board device tree that describes the PL blocks and the
extra PS peripherals (`linux/dts`). Use it only with a bitstream that contains those blocks
(hw-v1 or later); with the PS-only bitstream the drivers would read PL registers that are not
there. The default is the mainline PS-only tree used for the first boot.

```bash
SATLINK_PL_DT=1 kas build yocto/kas/zybo-z7-20.yml
```

Outputs are in `build/tmp/deploy/images/zybo-z7-20/`: `satlink-image-zybo-z7-20.rootfs.wic`,
`u-boot.elf`, `boot.scr`, `zImage`, `zynq-zybo-z7.dtb`.

## QEMU smoke test

```bash
python3 yocto/scripts/qemu_smoke.py --deploy-dir build/tmp/deploy/images/zybo-z7-20
```

Boots kernel + device tree + root file system in QEMU's `xilinx-zynq-a9` machine, logs in and
runs a probe command. Exit code 0 means pass; the console log is in `qemu-smoke.log`.

## SD card and first boot

1. Write the `.wic` image to the SD card (Linux: `sudo bmaptool copy <wic> /dev/sdX`; Windows:
   balenaEtcher or Rufus in DD mode).
2. Create `BOOT.BIN` (see [bootbin/README.md](bootbin/README.md)) and copy it onto the FAT
   partition `boot`.
3. Set jumper JP5 to SD, connect the USB-UART (115200 8N1), power on.
4. Log in as `root` (no password, development image), then `ssh root@<board-ip>`.

## Reproducibility

After the first successful build, pin every layer to its commit:

```bash
kas dump --lock --inplace yocto/kas/zybo-z7-20.yml   # writes zybo-z7-20.lock.yml
```

Commit the lock file. CI (`.github/workflows/yocto.yml`) builds on a self-hosted runner
labelled `yocto`, because GitHub's hosted runners lack the disk space.

## Layer contents

| Path | Purpose |
|---|---|
| `conf/machine/zybo-z7-20.conf` | Machine: Cortex-A9 tune, linux-xlnx, u-boot-xlnx, device tree, SD layout |
| `recipes-kernel/linux/` | Kernel config fragments (`satlink.cfg`, `preempt-rt.cfg`) |
| `recipes-bsp/u-boot/` | U-Boot config fragment |
| `recipes-bsp/satlink-boot-scr/` | U-Boot boot script |
| `recipes-core/images/satlink-image.bb` | Image: SSH, CAN, I2C, rt-tests, debug tools |
| `wic/satlink-sd.wks` | SD card partition layout |
