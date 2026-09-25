"""Checks that need the Zybo Z7: the programmable-logic loopbacks, the Core 1 restart and the
housekeeping controller on the CAN bus.

@verifies SRS-HIL-001
@verifies SRS-AMP-004
"""
import pytest

from satlink.mission import Event, EventReport, PlatformHk
from conftest import wait_until

pytestmark = pytest.mark.board


@pytest.mark.parametrize("mode", ["DIG", "ANAL"])
def test_pl_loopbacks_lock(scpi, mode):
    scpi.write(f"MOD:LOOP {mode}")
    wait_until(lambda: scpi.query("MEAS:LOCK?") == "1", 10, f"lock in {mode} loopback")
    assert scpi.query_float("MEAS:ESN0?") > 15.0
    scpi.write("MOD:LOOP SOFT")


def test_core1_restart_is_reported_and_the_link_returns(pus, scpi):
    scpi.write("MOD:REST")
    pus.wait_for(EventReport, 15, lambda e: e.event == Event.MODEM_RESTARTED)
    wait_until(lambda: scpi.query("MEAS:LOCK?") == "1", 20, "lock after the restart")


def test_housekeeping_controller_reports_over_can(pus):
    pus.execute(pus.cmd.one_shot_hk([3]))
    hk = pus.wait_for(PlatformHk, 5)
    assert hk.hkc_valid
    assert 0 < hk.temperature_c < 85
    assert 950 <= hk.vccint_mv <= 1050
    assert hk.rtos_state == 2  # running
