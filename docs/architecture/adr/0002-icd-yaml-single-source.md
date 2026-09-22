# ADR-0002: ICD YAML files are the single source of truth

- Status: Accepted
- Date: 2026-09-22

## Context

Register maps, base addresses and interrupt numbers are used by the FPGA design, the Linux
device tree and drivers, the FreeRTOS firmware, the MCU firmware and the documentation. Kept by
hand in five places, they drift.

## Decision

Interfaces are defined once in YAML under `icd/`. `tools/regmap/regmap_gen.py` validates them
(alignment, overlaps, field ranges, reset values, duplicate interrupts, DDR partitions) and
generates C headers, C++ `constexpr` descriptors and Markdown ICD pages. Generated files are
committed so they can be read on GitHub; CI regenerates them and fails on any difference.

## Consequences

- Changing an interface is one YAML edit plus regeneration, reviewed in one pull request.
- Every address has a `status` (fixed / provisional / frozen): values are frozen only after they
  are confirmed against an exported hardware baseline.
- Later: generate device-tree fragments and a Verilog register package from the same YAML.
