"""CCSDS space packets (133.0-B-2) with PUS-C secondary headers (ECSS-E-ST-70-41C).

Same layout as libs/pus: TC data field header of 5 bytes, TM data field header of 13 bytes with
a CUC time (4 + 2 bytes) since 2000-01-01T00:00:00Z, CRC-16-CCITT at the end of every packet.

@implements SRS-GS-004
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

PUS_VERSION = 2
MISSION_EPOCH_UNIX = 946_684_800
ACK_ACCEPTANCE = 0x1
ACK_COMPLETION = 0x8


def crc16_ccitt(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16-CCITT, polynomial 0x1021, initial value 0xFFFF."""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


class DecodeError(ValueError):
    """The bytes are not a valid PUS telemetry packet."""


@dataclass
class Telecommand:
    apid: int
    sequence_count: int
    service: int
    subtype: int
    data: bytes = b""
    ack_flags: int = ACK_ACCEPTANCE | ACK_COMPLETION
    source_id: int = 1

    def encode(self) -> bytes:
        body = bytes([(PUS_VERSION << 4) | (self.ack_flags & 0xF), self.service, self.subtype])
        body += struct.pack(">H", self.source_id) + bytes(self.data)
        return _finish(True, self.apid, self.sequence_count, body)


@dataclass
class Telemetry:
    apid: int
    sequence_count: int
    service: int
    subtype: int
    message_counter: int = 0
    destination_id: int = 1
    seconds: int = 0
    fraction: int = 0
    data: bytes = field(default=b"")

    @property
    def unix_time(self) -> float:
        return MISSION_EPOCH_UNIX + self.seconds + self.fraction / 65536.0

    def encode(self) -> bytes:
        body = bytes([PUS_VERSION << 4, self.service, self.subtype])
        body += struct.pack(">HHIH", self.message_counter, self.destination_id, self.seconds,
                            self.fraction) + bytes(self.data)
        return _finish(False, self.apid, self.sequence_count, body)

    @classmethod
    def decode(cls, packet: bytes) -> "Telemetry":
        if len(packet) < 6:
            raise DecodeError("too short")
        word0, word1, length = struct.unpack_from(">HHH", packet)
        if packet[0] >> 5:
            raise DecodeError("bad packet version")
        if 6 + length + 1 != len(packet):
            raise DecodeError("length mismatch")
        if word0 & 0x1000:
            raise DecodeError("not a telemetry packet")
        if not word0 & 0x0800:
            raise DecodeError("no secondary header")
        if len(packet) < 6 + 13 + 2:
            raise DecodeError("too short")
        if packet[6] >> 4 != PUS_VERSION:
            raise DecodeError("bad PUS version")
        if crc16_ccitt(packet) != 0:
            raise DecodeError("CRC error")
        service, subtype = packet[7], packet[8]
        counter, dest, seconds, fraction = struct.unpack_from(">HHIH", packet, 9)
        return cls(word0 & 0x7FF, word1 & 0x3FFF, service, subtype, counter, dest, seconds,
                   fraction, bytes(packet[19:-2]))


def _finish(tc: bool, apid: int, seq: int, body: bytes) -> bytes:
    data_len = len(body) + 2
    header = struct.pack(">HHH", (int(tc) << 12) | 0x0800 | (apid & 0x7FF),
                         0xC000 | (seq & 0x3FFF), data_len - 1)
    packet = header + body
    return packet + struct.pack(">H", crc16_ccitt(packet))
