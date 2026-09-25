"""Adaptive coding and modulation against the channel emulator.

@verifies SRS-ACM-001
@verifies SRS-SYS-005
@verifies SRS-HIL-001
"""
import pytest

from conftest import wait_until


@pytest.mark.parametrize("esn0,expected", [(3.0, 0), (6.5, 1), (9.0, 2), (12.0, 3), (20.0, 4)])
def test_acm_settles_on_the_best_modcod(scpi, esn0, expected):
    scpi.write(f"CHAN:ESN0 {esn0}")
    wait_until(lambda: scpi.query("MEAS:MODC?") == str(expected), 15,
               f"MODCOD {expected} at {esn0} dB")


def test_limits_and_fade_step_down(scpi):
    scpi.write("MOD:ACM:LIM 1,3")
    wait_until(lambda: scpi.query("MEAS:MODC?") == "3", 10, "upper limit")
    scpi.write("CHAN:ESN0 7")
    wait_until(lambda: scpi.query("MEAS:MODC?") == "1", 5, "step down")
    # Frames in flight at the old MODCOD are lost; the receiver locks on the new one.
    wait_until(lambda: scpi.query("MEAS:LOCK?") == "1", 5, "lock at MODCOD 1")
