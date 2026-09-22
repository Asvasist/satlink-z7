# ADR-0001: One CMake build for all targets

- Status: Accepted
- Date: 2026-09-22

## Context

The project has four software targets: host (tests, tools), Linux on Core 0, FreeRTOS on
Core 1 and bare metal on the MicroBlaze V. Vendor flows (Vitis for bare metal, Yocto for Linux)
each come with their own build system.

## Decision

All SatLink code builds from one CMake project. Each target is a CMake preset with its own
toolchain file. Vendor BSPs (AMD standalone, FreeRTOS kernel) are consumed as libraries by that
build instead of owning it. Yocto builds the Linux user-space parts through the same CMake
project (via its SDK toolchain file).

## Consequences

- Shared code (`libs/common`) is compiled and tested once and cross-compiled for every target
  in CI, so portability problems show up immediately.
- Vitis workspaces are not the build of record; BSP sources must be exported into the repo.
- Developers need CMake 3.25+ and Ninja.
