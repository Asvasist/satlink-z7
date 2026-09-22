# Stage 1: Foundation and BSP

Goal: a repository that builds, tests and documents itself, and a Linux image that boots on the
board and in QEMU.

## Exit criteria

| # | Criterion | Requirement | Status |
|---|---|---|---|
| 1 | Monorepo layout, license, contribution rules | - | done |
| 2 | One CMake build, presets for host / linux / rtos / hkc | SRS-BLD-001 | done (first `cmake` run on your machine) |
| 3 | Warning-free with `-Werror` | SRS-BLD-002 | done for current code |
| 4 | CI: format, clang-tidy, cppcheck, MISRA report, unit tests, sanitizers, cross builds | SRS-BLD-003/004 | written, first run after push |
| 5 | Coverage gate >= 80 % | SRS-BLD-005 | written, first run after push |
| 6 | SRS (StrictDoc), traceability matrix | SRS-DOC-002 | done |
| 7 | Architecture (arc42) and ADRs | SRS-DOC-001 | done |
| 8 | ICD: address map, register maps, generator with validation | SRS-ICD-001..004 | done (addresses provisional) |
| 9 | Reference library: CRC-16, CCSDS randomizer | SRS-LIB-001..003 | done, 12 unit tests |
| 10 | Yocto layer meta-satlink + kas config | SRS-BSP-001/002, SRS-BLD-006 | written, first build pending |
| 11 | Boot on the board from SD (FSBL + U-Boot + Linux), SSH | SRS-BSP-003, SRS-BOOT-001 | pending (needs hw-v1 XSA) |
| 12 | QEMU smoke test | SRS-BSP-004 | script done, run after first image build |
| 13 | Repository public with CI badge | - | pending |

## Checklist for the hardware part

- [ ] Vivado hw-v1 (and later hw-v2) exported: `hw/` Tcl committed, XSA attached to a GitHub release
- [ ] Address Editor values compared with `icd/address_map.yaml`; `status` set to `frozen`
- [ ] FSBL built in Vitis, BOOT.BIN created with `yocto/bootbin/satlink.bif`
- [ ] Board boots to login on the USB-UART (115200 8N1); `ssh root@<ip>` works
- [ ] Bring-up report written from [docs/bringup/TEMPLATE.md](../bringup/TEMPLATE.md)
