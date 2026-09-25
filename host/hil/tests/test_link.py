"""Physical layer through the loopback: lock, error rates, MODCODs, link loss.

@verifies SRS-MDM-005
@verifies SRS-HIL-001
"""
import time

import pytest

from conftest import wait_until

# ACM thresholds (libs/modem/src/modcod.c) and a margin that must give an error-free link.
THRESHOLDS = [2.0, 4.5, 7.0, 10.0, 12.5]
MARGIN_DB = 2.0


def test_clean_link_is_error_free(scpi):
    time.sleep(1.5)
    scpi.write("MEAS:RES")
    time.sleep(3.0)
    assert scpi.query_float("MEAS:ESN0?") > 25.0
    assert scpi.query_float("MEAS:BER?") == 0.0
    assert scpi.query_float("MEAS:FER?") == 0.0


@pytest.mark.parametrize("modcod", range(5))
def test_every_modcod_is_error_free_above_its_threshold(scpi, modcod):
    esn0 = THRESHOLDS[modcod] + MARGIN_DB
    scpi.write(f"CHAN:ESN0 {esn0};:MOD:MODC {modcod}")
    wait_until(lambda: scpi.query("MEAS:MODC?") == str(modcod), 5, "MODCOD")
    # The receiver smooths its estimate over frames (BPSK 1/2 frames last 0.4 s).
    wait_until(lambda: abs(scpi.query_float("MEAS:ESN0?") - esn0) < 1.0, 10, "Es/N0 estimate")
    assert scpi.query("MEAS:LOCK?") == "1"
    scpi.write("MEAS:RES")
    time.sleep(3.0)
    assert scpi.query_float("MEAS:FER?") == 0.0


def test_deep_fade_loses_and_regains_lock(scpi):
    scpi.write("MOD:MODC 4;:CHAN:ESN0 3")
    wait_until(lambda: scpi.query("MEAS:LOCK?") == "0", 5, "loss of lock")
    scpi.write("CHAN:CLE")
    wait_until(lambda: scpi.query("MEAS:LOCK?") == "1", 5, "lock")


def test_counters_and_latency_are_reported(scpi):
    ok, crc, bit_errors, bits = scpi.query_floats("MEAS:COUN?")
    assert ok > 0 and bits > 0
    worst, avg = scpi.query_floats("MEAS:LAT?")
    assert worst >= avg >= 0
    assert 0.0 <= scpi.query_float("MEAS:LOAD?") <= 100.0
