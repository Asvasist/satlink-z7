# ADR-0003: AMP with exclusive resource ownership

- Status: Accepted
- Date: 2026-09-22

## Context

Linux and FreeRTOS run on the two Cortex-A9 cores without a hypervisor. Both can reach every
peripheral and all of DDR, and nothing stops them from interfering with each other.

## Decision

Every peripheral, PL block, interrupt and DDR region has exactly one owner in
`icd/address_map.yaml`. Linux disables (device tree `status = "disabled"`) or reserves
(`reserved-memory`, `no-map`) everything it does not own. Shared data goes only through the
IPC shared-memory partition (ADR-0005). The interrupt distributor is configured by Linux; FreeRTOS only
routes its own interrupts to Core 1.

## Consequences

- Ownership conflicts are visible in review and checked by the generator.
- Both cores share the L2 cache and the DDR controller, so timing isolation is not perfect;
  this is measured in Stage 4 and Stage 6.
