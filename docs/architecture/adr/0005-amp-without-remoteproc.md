# ADR-0005: AMP with an own loader driver and shared-memory rings instead of remoteproc/RPMsg

- Status: Accepted
- Date: 2026-09-25

## Context

The plan was the standard AMP stack: Linux remoteproc loads the FreeRTOS image on Core 1 and
OpenAMP/RPMsg (virtio rings) carries the messages. On the Zynq-7000 that needs a remoteproc
driver for the second Cortex-A9. Mainline Linux never had one. AMD's `zynq_remoteproc` depended
on a vendor IPI hook and is gone from the 6.6 kernel that Yocto scarthgap / meta-xilinx
builds; only the ZynqMP/Versal R5 drivers remain (checked in `xlnx_rebase_v6.6_LTS`).

Porting the old driver would mean carrying forward vendor kernel patches. Running the firmware
without any Linux involvement (FSBL loads both images, `maxcpus=1`) would lose restart, fault
recovery and firmware updates through the root file system.

## Decision

- A small platform driver, `satlink_amp` (`linux/drivers/satlink_amp`), does what remoteproc
  would: `request_firmware()`, checks every ELF segment against the reserved regions Core 1
  owns, takes CPU1 from Linux with `remove_cpu()`, holds it in reset through the SLCR and
  releases it through the same trampoline at physical address 0 that the Zynq SMP code uses
  (the platform reserves that page for it).
- Messages travel through two single-producer/single-consumer rings in the `ipc_shm` partition
  (`libs/amp/src/ipc_ring.c`). The same source file is compiled into the kernel module, the
  firmware and the host tests; both cores map the partition non-cacheable, so only ordering
  (barriers) matters.
- Doorbells are two PL-to-PS interrupt lines the PL does not use (IRQ 84, 85). Each core sets
  "its" line pending in the GIC distributor: the firmware writes `GICD_ISPENDR`, Linux calls
  `irq_set_irqchip_state()`.
- A control block next to the rings carries the firmware state, a heartbeat and fault reports.
  The driver restarts Core 1 after a fault report or a stalled heartbeat (FDIR).
- User space sees `/dev/satlink-amp` (one message per `read()`/`write()`, `poll()`), plus a
  `state` sysfs attribute.

## Consequences

- No out-of-tree vendor kernel patches; the driver uses exported APIs only.
- The message format is ours and fixed-size, which suits the modem (at most 160 bytes per
  message) better than a general transport.
- The whole IPC path (rings, message set, the firmware's application logic) runs on the host:
  in unit tests, in the payload simulator (`SimulatedCore1`) and, with the firmware itself, in
  QEMU. Only the doorbells and the CPU release need the board.
- The name service, dynamic endpoints and virtio compatibility of RPMsg are not available. If a
  future kernel regains a Zynq-7000 remoteproc driver, the message set can move to an RPMsg
  endpoint without changing the firmware's modem code (`libs/modem_app` is transport-neutral).
