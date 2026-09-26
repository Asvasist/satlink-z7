"""SatLink-Z7 mission definitions (docs/icd section 5): telecommand builders and decoders for
the payload's reports. Mirrors linux/payload/include/satlink/payload/mission.hpp.

@implements SRS-GS-004
"""
from __future__ import annotations

import enum
import math
import struct
from dataclasses import dataclass
from typing import Optional, Union

from .pus import Telecommand, Telemetry

APID_PAYLOAD = 0x010
GROUND_ID = 0x0001
TC_PORT = 10025
TM_PORT = 10026
SCPI_PORT = 5025
NOISE_SCALE = 4096.0
MODCOD_NAMES = ("BPSK 1/2", "QPSK 1/2", "QPSK 3/4", "8PSK 2/3", "8PSK 5/6")


class Function(enum.IntEnum):
    SET_MODCOD = 1
    SET_ACM = 2
    SET_CHANNEL = 3
    START_PASS = 4
    STOP_PASS = 5
    SET_LOOPBACK = 6
    RESTART_MODEM = 7


class Event(enum.IntEnum):
    LINK_LOCKED = 1
    LINK_LOST = 2
    MODCOD_CHANGED = 3
    AOS = 4
    LOS = 5
    FIRMWARE_LOG = 6
    MODEM_RESTARTED = 7


class FailureCode(enum.IntEnum):
    UNKNOWN_SERVICE = 1
    UNKNOWN_SUBTYPE = 2
    BAD_DATA = 3
    UNKNOWN_FUNCTION = 4
    MODEM_ERROR = 5
    BAD_APID = 6
    CORRUPT_PACKET = 7


class Loopback(enum.IntEnum):
    ANALOG = 0
    DIGITAL = 1
    SOFTWARE = 2


def noise_level_for(esn0_db: float) -> int:
    """Channel emulator noise level for an Es/N0."""
    return max(0, min(65535, round(10 ** (-esn0_db / 20.0) * NOISE_SCALE)))


class Commander:
    """Builds telecommands with a running 14-bit sequence count."""

    def __init__(self, first_sequence: int = 0):
        self.next_sequence = first_sequence & 0x3FFF

    def build(self, service: int, subtype: int, data: bytes = b"") -> tuple[int, bytes]:
        seq = self.next_sequence
        self.next_sequence = (seq + 1) & 0x3FFF
        return seq, Telecommand(APID_PAYLOAD, seq, service, subtype, data,
                                source_id=GROUND_ID).encode()

    def _function(self, f: Function, args: bytes = b"") -> tuple[int, bytes]:
        return self.build(8, 1, bytes([f]) + args)

    def ping(self):
        return self.build(17, 1)

    def set_modcod(self, modcod: int):
        return self._function(Function.SET_MODCOD, bytes([modcod]))

    def set_acm(self, enabled: bool, lowest: int = 0, highest: int = 4, margin_db: float = 1.0,
                hysteresis_db: float = 1.0):
        return self._function(Function.SET_ACM, struct.pack(
            ">BBBhh", int(enabled), lowest, highest, round(margin_db * 100),
            round(hysteresis_db * 100)))

    def set_channel(self, noise_level: int, gain_q15: int = 0x7FFF):
        return self._function(Function.SET_CHANNEL, struct.pack(">HH", noise_level, gain_q15))

    def set_esn0(self, esn0_db: float):
        return self.set_channel(noise_level_for(esn0_db))

    def start_pass(self, max_elevation_deg: float = 60.0, zenith_esn0_db: float = 20.0,
                   time_scale: int = 10):
        return self._function(Function.START_PASS, struct.pack(
            ">HhB", round(max_elevation_deg * 100), round(zenith_esn0_db * 100), time_scale))

    def stop_pass(self):
        return self._function(Function.STOP_PASS)

    def set_loopback(self, mode: Loopback):
        return self._function(Function.SET_LOOPBACK, bytes([mode]))

    def restart_modem(self):
        return self._function(Function.RESTART_MODEM)

    def enable_hk(self, sids, enable: bool = True):
        sids = list(sids)
        return self.build(3, 5 if enable else 6, bytes([len(sids), *sids]))

    def one_shot_hk(self, sids):
        sids = list(sids)
        return self.build(3, 27, bytes([len(sids), *sids]))

    def set_hk_period(self, sid: int, period_ms: int):
        return self.build(3, 31, struct.pack(">BI", sid, period_ms))


# ---- reports ----

@dataclass
class ModemHk:
    locked: bool
    modcod: int
    acm: bool
    esn0_db: float
    frames_ok: int
    frames_crc_error: int
    header_errors: int
    bit_errors: int
    bits_checked: int
    latency_max_us: int
    latency_avg_us: int
    cpu_load_pct: float
    tx_queue: int
    uptime_ms: int

    FORMAT = ">BBBBhIIIIIIIHBI"

    @classmethod
    def decode(cls, d: bytes) -> "ModemHk":
        v = struct.unpack(cls.FORMAT, d)
        return cls(bool(v[1]), v[2], bool(v[3]), v[4] / 100, *v[5:12], v[12] / 10, v[13], v[14])


@dataclass
class LinkHk:
    pass_active: bool
    elevation_deg: float
    range_km: float
    range_rate_km_s: float
    pass_esn0_db: float
    frames_sent: int
    frames_received: int
    lost_frames: int
    packets: int
    resyncs: int
    ip_down: int
    ip_up: int
    dropped: int

    FORMAT = ">BBhHhhIIIIIIII"

    @classmethod
    def decode(cls, d: bytes) -> "LinkHk":
        v = struct.unpack(cls.FORMAT, d)
        return cls(bool(v[1]), v[2] / 100, float(v[3]), v[4] / 1000, v[5] / 100, *v[6:])


@dataclass
class PlatformHk:
    hkc_valid: bool
    temperature_c: float
    vccint_mv: int
    vccaux_mv: int
    vbram_mv: int
    hkc_uptime_s: int
    hkc_error_flags: int
    rtos_state: int
    rtos_restarts: int

    FORMAT = ">BBhHHHIBBI"

    @classmethod
    def decode(cls, d: bytes) -> "PlatformHk":
        v = struct.unpack(cls.FORMAT, d)
        return cls(bool(v[1]), v[2] / 100, *v[3:])


@dataclass
class ConstellationHk:
    """Received symbols of a recent frame (SID 4), unit symbol amplitude = 1."""
    modcod: int
    points: list

    @classmethod
    def decode(cls, d: bytes) -> "ConstellationHk":
        if len(d) < 3 or d[0] != 4 or len(d) != 3 + 2 * d[2]:
            raise struct.error("constellation length")
        raw = struct.unpack_from(f">{2 * d[2]}b", d, 3)
        return cls(d[1], [(raw[k] / 64, raw[k + 1] / 64) for k in range(0, len(raw), 2)])


@dataclass
class EventReport:
    severity: int
    event: Union[Event, int]
    aux: bytes
    time: float

    def describe(self) -> str:
        name = self.event.name.lower().replace("_", " ") if isinstance(self.event, Event) \
            else f"event {self.event}"
        if self.event == Event.MODCOD_CHANGED and len(self.aux) == 2:
            return f"{name}: {modcod_name(self.aux[0])} -> {modcod_name(self.aux[1])}"
        if self.event == Event.FIRMWARE_LOG and self.aux:
            return f"{name}: {self.aux.decode(errors='replace')}"
        return name


@dataclass
class Verification:
    subtype: int
    sequence_count: int
    failure: Optional[Union[FailureCode, int]]

    @property
    def success(self) -> bool:
        return self.subtype in (1, 3, 5, 7)

    @property
    def completed(self) -> bool:
        return self.subtype in (7, 8)


@dataclass
class Pong:
    pass


@dataclass
class Unknown:
    service: int
    subtype: int


Report = Union[ModemHk, LinkHk, PlatformHk, ConstellationHk, EventReport, Verification, Pong,
               Unknown]


def modcod_name(m: int) -> str:
    return MODCOD_NAMES[m] if 0 <= m < len(MODCOD_NAMES) else f"MODCOD {m}"


def _enum(kind, value):
    try:
        return kind(value)
    except ValueError:
        return value


def interpret(tm: Telemetry) -> Report:
    d = tm.data
    try:
        if (tm.service, tm.subtype) == (3, 25) and d:
            decoder = {1: ModemHk, 2: LinkHk, 3: PlatformHk, 4: ConstellationHk}.get(d[0])
            if decoder is not None:
                return decoder.decode(d)
        elif tm.service == 5 and 1 <= tm.subtype <= 4 and len(d) >= 2:
            return EventReport(tm.subtype, _enum(Event, struct.unpack_from(">H", d)[0]),
                               bytes(d[2:]), tm.unix_time)
        elif tm.service == 1 and len(d) in (4, 6):
            failure = _enum(FailureCode, struct.unpack_from(">H", d, 4)[0]) if len(d) == 6 else None
            return Verification(tm.subtype, struct.unpack_from(">H", d, 2)[0] & 0x3FFF, failure)
        elif (tm.service, tm.subtype) == (17, 2):
            return Pong()
    except struct.error:
        pass
    return Unknown(tm.service, tm.subtype)


def ber(errors: int, bits: int) -> float:
    return errors / bits if bits else math.nan
