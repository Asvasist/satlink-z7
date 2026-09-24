# ADR-0006: A/B boot slots kept in the U-Boot environment

- Status: Accepted
- Date: 2026-09-24

## Context

A failed update of the Linux image must not leave the board unbootable. The board has one SD
card and no external recovery path, so the boot chain itself has to notice that a new image does
not come up and go back to the old one. The FSBL, the bitstream and U-Boot are not updated by
this mechanism; they stay in `BOOT.BIN`.

## Decision

The SD card holds a FAT boot partition, two root file systems (a and b) and a data partition
(`yocto/meta-satlink/wic/satlink-sd-ab.wks`). The kernel and device tree of each slot are files
on the boot partition (`zImage.a`, `zImage.b`, `system-a.dtb`, `system-b.dtb`). The state is five
U-Boot environment variables, stored as `uboot.env` on the FAT partition:

| Variable | Meaning |
|---|---|
| `boot_slot` | slot to boot, `a` or `b` |
| `prev_slot` | slot to fall back to |
| `bootcount` | boot attempts of a slot on trial |
| `bootlimit` | attempts a slot on trial gets (1 to 9) |
| `upgrade_available` | 1 while `boot_slot` is on trial |

`boot-ab.cmd` decides at every boot: a slot on trial gets `bootlimit` attempts; if `bootcount` has
reached the limit the script switches back to `prev_slot` and ends the trial, otherwise it
counts the attempt. Linux ends the trial with `satlink-bootctl confirm` (run 30 s after boot by an
init script, and only if a health check passes), and starts one with `satlink-bootctl
begin-update` after the update wrote the other slot. The variable writes in both are ordered so
that a power loss in between never leaves a slot on trial without a way back. The boot script,
`satlink-bootctl` and the C reference model `libs/boot/src/ab_slot.c` implement the same rules,
and the unit tests run all three through the same scenarios.

`libubootenv` reads and writes the file from Linux (`/etc/fw_env.config`), so no raw flash
access is needed. The option is opt-in (`SATLINK_AB=1`) because it changes the partition layout.

## Consequences

- The rollback logic is testable without a board: `tools/boot/ubootsim.py` runs the real boot
  script text against an environment file, and `satlink-bootctl` runs against fake
  `fw_printenv`/`fw_setenv` tools sharing that file.
- The simulator implements only the handful of U-Boot commands the script uses and rejects
  anything else; it reproduces the details that matter (only `saveenv` persists, `setexpr` works
  in hex), but it is not U-Boot. The first boot on hardware still has to confirm the script,
  the environment on FAT and `fw_printenv` against the same file.
- A power loss while U-Boot or Linux rewrites `uboot.env` on FAT can corrupt the file. U-Boot
  then falls back to its default environment, which has no slot state, so the board boots slot
  a, which may be the older image. Redundant environment copies would remove this; they are not
  worth the extra complexity yet.
- The boot files are not signed. A signed FIT image per slot (SRS-BOOT-002) is the next step and
  needs a key management decision first.
