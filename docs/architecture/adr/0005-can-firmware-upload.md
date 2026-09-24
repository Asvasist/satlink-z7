# ADR-0005: Firmware upload to the housekeeping controller over CAN

- Status: Accepted
- Date: 2026-09-24

## Context

The MicroBlaze V housekeeping controller (HKC) runs from LMB block RAM that the bitstream
initialises, so changing its firmware would normally mean a new bitstream. Linux already has a
CAN link to it (PmodCAN on both sides). The link is classic CAN: 8 bytes per frame, no
transport layer, and frames can be lost, duplicated or arrive after a bus-off recovery.

## Decision

The LMB memory is split into a 16 KiB **bootloader** at 0x0000 (part of the bitstream, never
overwritten) and a 112 KiB **application** at 0x4000 that the bootloader loads. The bootloader
takes the application over CAN with a small sequenced protocol
(`libs/boot/include/satlink/boot/can_boot.h`):

- Two identifiers: 0x7E0 host to node, 0x7E1 node to host. Commands: PING, BEGIN (size and
  CRC-16), DATA (16-bit sequence number and 5 image bytes), END, BOOT, ABORT, ENTER.
- **Go-back-N** with a window of 16 DATA frames and cumulative ACKs. A gap makes the receiver
  answer NAK with the sequence number it expects; a timeout makes the sender resume from the
  last acknowledged frame. Duplicates are ignored. CAN's own CRC and retransmission protect
  each frame; the protocol adds ordering and completeness.
- **Whole-image CRC-16-CCITT**, sent in BEGIN and checked at END. BOOT is refused until END
  passed the check.
- The application carries a 16-byte header (`SLAP` magic, version, header length, size, CRC-16
  of the body) written by `tools/hkc/mkapp.py`. After a reset the bootloader starts the image
  in memory only if that header and CRC are valid, so an interrupted upload cannot be started.
- A running application restarts into the bootloader when it receives ENTER, so an update needs
  no button or power cycle.
- The protocol code is one portable C library. The receiver runs in the bootloader; the sender
  runs on the host and on Linux (`satlink-diag hkc upload`). Unit tests connect the two through
  a bus model that loses, duplicates and corrupts frames.

## Consequences

- An update can be applied to a running board, and a corrupted or partial upload can never be
  started.
- Five image bytes per frame limit throughput to under 20 kB/s at 500 kbit/s. The current 4 KiB
  application takes well under a second and a full 112 KiB one about six seconds, which is
  acceptable for a firmware update.
- CRC-16 detects corruption, not tampering: the upload is not authenticated. Anyone on the bus
  can replace the housekeeping firmware. For a real system the image would be signed and the
  bootloader would verify the signature; this is recorded as an open item, next to the signed
  FIT images of SRS-BOOT-002.
- The bootloader itself cannot be updated over CAN; changing it means a new bitstream.
