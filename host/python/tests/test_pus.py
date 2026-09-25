"""Offline tests of the Python client: packet layouts match the flight software.

@verifies SRS-GS-004
"""
import struct

import pytest

from satlink import mission, pus


def test_crc_check_value():
    assert pus.crc16_ccitt(b"123456789") == 0x29B1


def test_telecommand_layout_matches_flight_encoder():
    packet = pus.Telecommand(0x010, 0, 17, 1).encode()
    assert packet[:11] == bytes([0x18, 0x10, 0xC0, 0x00, 0x00, 0x06, 0x29, 0x11, 0x01, 0x00, 0x01])
    assert pus.crc16_ccitt(packet) == 0


def test_telemetry_round_trip_and_errors():
    tm = pus.Telemetry(0x010, 5, 3, 25, 7, 1, 815_000_000, 0x8000, b"\x01\x02")
    packet = tm.encode()
    back = pus.Telemetry.decode(packet)
    assert back == tm
    assert back.unix_time == pytest.approx(946_684_800 + 815_000_000.5)
    for bad, reason in ((packet[:3], "too short"), (packet[:-1], "length"),
                        (bytes([packet[0] | 0x10]) + packet[1:], "telemetry"),
                        (packet[:-1] + bytes([packet[-1] ^ 1]), "CRC")):
        with pytest.raises(pus.DecodeError, match=reason):
            pus.Telemetry.decode(bad)


def test_function_arguments():
    c = mission.Commander()
    seq, p = c.set_acm(True, 0, 4, 1.0, 0.5)
    assert seq == 0 and p[11:19] == bytes([2, 1, 0, 4, 0, 100, 0, 50])
    _, p = c.start_pass(80.0, 20.0, 25)
    assert p[11:17] == bytes([4, 0x1F, 0x40, 0x07, 0xD0, 25])
    _, p = c.set_hk_period(1, 500)
    assert p[7:8] == b"\x03" and p[11:16] == bytes([1, 0, 0, 1, 0xF4])
    assert mission.noise_level_for(10.0) == 1295
    assert c.next_sequence == 3


def _tm(service, subtype, data):
    return pus.Telemetry.decode(pus.Telemetry(0x010, 0, service, subtype, data=data).encode())


def test_reports():
    modem = struct.pack(mission.ModemHk.FORMAT, 1, 1, 2, 1, 1200, 10, 1, 0, 3, 1024, 50, 20,
                        45, 2, 5000)
    hk = mission.interpret(_tm(3, 25, modem))
    assert isinstance(hk, mission.ModemHk) and hk.locked and hk.esn0_db == 12.0
    assert hk.cpu_load_pct == 4.5
    link = struct.pack(mission.LinkHk.FORMAT, 2, 1, 6000, 1200, -3500, 1000, *range(8))
    lk = mission.interpret(_tm(3, 25, link))
    assert isinstance(lk, mission.LinkHk) and lk.elevation_deg == 60.0 and lk.range_rate_km_s == -3.5
    plat = struct.pack(mission.PlatformHk.FORMAT, 3, 1, 3002, 1000, 1800, 1000, 60, 0, 2, 1)
    pl = mission.interpret(_tm(3, 25, plat))
    assert isinstance(pl, mission.PlatformHk) and pl.temperature_c == pytest.approx(30.02)
    assert isinstance(mission.interpret(_tm(3, 25, b"\x01\x00")), mission.Unknown)
    c = mission.interpret(_tm(3, 25, bytes([4, 3, 2, 64, 0, 0, 0xC0])))
    assert isinstance(c, mission.ConstellationHk) and c.points == [(1.0, 0.0), (0.0, -1.0)]
    assert isinstance(mission.interpret(_tm(3, 25, bytes([4, 3, 2, 64]))), mission.Unknown)

    ev = mission.interpret(_tm(5, 1, b"\x00\x03\x01\x02"))
    assert ev.describe() == "modcod changed: QPSK 1/2 -> QPSK 3/4"
    assert mission.interpret(_tm(5, 2, b"\x00\x06hi")).describe() == "firmware log: hi"
    assert mission.interpret(_tm(5, 1, b"\x00\x63")).describe() == "event 99"

    v = mission.interpret(_tm(1, 8, b"\x18\x10\xC0\x05\x00\x05"))
    assert not v.success and v.completed and v.failure == mission.FailureCode.MODEM_ERROR
    assert isinstance(mission.interpret(_tm(17, 2, b"")), mission.Pong)
    assert mission.modcod_name(7) == "MODCOD 7"
