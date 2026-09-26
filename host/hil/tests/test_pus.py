"""PUS services end to end: telecommands over UDP, telemetry through the modem link.

@verifies SRS-PLM-001
@verifies SRS-PLM-002
@verifies SRS-HIL-001
"""
import time

import pytest

from satlink import TelecommandFailed
from satlink.mission import (Event, EventReport, FailureCode, LinkHk, ModemHk, Pong,
                             Verification)
from conftest import wait_until


def test_are_you_alive(pus):
    reports = pus.execute(pus.cmd.ping())
    assert any(isinstance(r, Pong) for r in reports)


def test_housekeeping_agrees_with_the_instrument(pus, scpi):
    hk = pus.wait_for(ModemHk, timeout=3)
    assert hk.locked and hk.acm
    assert hk.esn0_db == pytest.approx(scpi.query_float("MEAS:ESN0?"), abs=2.0)
    link = pus.wait_for(LinkHk, timeout=3)
    assert link.frames_received > 0 and not link.pass_active


def test_rejected_telecommands_carry_the_reason(pus):
    with pytest.raises(TelecommandFailed) as err:
        pus.execute(pus.cmd.set_modcod(7))
    assert err.value.report.failure == FailureCode.BAD_DATA
    with pytest.raises(TelecommandFailed) as err:
        pus.execute(pus.cmd.build(99, 1))
    assert err.value.report.failure == FailureCode.UNKNOWN_SERVICE


def test_fixed_modcod_telecommand_reaches_the_modem(pus, scpi):
    pus.execute(pus.cmd.set_modcod(0))
    pus.wait_for(ModemHk, timeout=5, where=lambda h: h.modcod == 0 and not h.acm)
    assert scpi.query("MOD:ACM?;:MEAS:MODC?") == "0;0"


def test_acm_changes_are_reported_as_events(pus, scpi):
    wait_until(lambda: scpi.query("MEAS:MODC?") == "4", 15, "ACM at the top")
    scpi.write("CHAN:ESN0 9")
    event = pus.wait_for(EventReport, timeout=10,
                         where=lambda e: e.event == Event.MODCOD_CHANGED and e.aux[0] == 4)
    # A sudden 20 dB fade loses the frames in flight: ACM falls back to the most robust MODCOD
    # (or straight to the target if the estimate got through) and climbs back to QPSK 3/4.
    assert event.aux[1] <= 2 and event.severity == 1
    assert event.describe().startswith("modcod changed: 8PSK 5/6 -> ")
    pus.wait_for(EventReport, timeout=15,
                 where=lambda e: e.event == Event.MODCOD_CHANGED and e.aux[1] == 2)


def test_telemetry_is_stored_during_an_outage_and_forwarded(pus, scpi):
    scpi.write("CHAN:ESN0 -5")
    wait_until(lambda: scpi.query("MEAS:LOCK?") == "0", 5, "outage")
    seq = pus.send(pus.cmd.ping())
    got = pus.collect(2.0)
    assert not any(isinstance(r, Pong) for r in got)  # nothing gets down
    scpi.write("CHAN:CLE")
    pus.wait_for(Verification, timeout=10,
                 where=lambda v: v.sequence_count == seq and v.completed and v.success)
    assert any(isinstance(r, Pong) for _, r in pus.history)


def test_housekeeping_rate_can_be_changed(pus):
    pus.execute(pus.cmd.set_hk_period(1, 250))
    start = time.monotonic()
    n = sum(isinstance(r, ModemHk) for r in pus.collect(3.0))
    pus.execute(pus.cmd.set_hk_period(1, 1000))
    assert n >= 8, f"{n} modem reports in {time.monotonic() - start:.1f} s"
