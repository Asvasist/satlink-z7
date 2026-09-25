"""SCPI instrument behaviour (IEEE 488.2 / SCPI-1999).

@verifies SRS-SCPI-002
@verifies SRS-SYS-004
@verifies SRS-HIL-001
"""
from conftest import wait_until


def test_identity_and_self_test(scpi):
    manufacturer, model, serial, version = scpi.idn.split(",")
    assert (manufacturer, model) == ("SatLink-Z7", "Payload Modem")
    assert version
    assert scpi.query("*TST?") == "0"
    assert scpi.query("SYST:VERS?") == "1999.0"
    assert scpi.query("*OPC?") == "1"


def test_errors_are_queued_and_flag_the_status_byte(scpi):
    scpi.write("MOD:MODC 9")
    scpi.write("NOT:A:COMMAND")
    assert int(scpi.query("*STB?")) & 0x04
    assert scpi.errors() == [(-222, "Data out of range;9"),
                             (-113, "Undefined header;NOT:A:COMMAND")]
    assert int(scpi.query("*ESR?")) & 0x30 == 0x30
    assert scpi.query("SYST:ERR?") == '0,"No error"'


def test_compound_messages(scpi):
    scpi.write("MOD:ACM:MARG 1.5;HYST 0.5")
    assert scpi.query("MOD:ACM:MARG?;HYST?;:MEAS:LOCK?") == "1.5;0.5;1"


def test_channel_settings_read_back(scpi):
    scpi.write("CHAN:ESN0 12.5")
    assert scpi.query_float("CHAN:ESN0?") == 12.5
    scpi.write("CHAN:CLE")
    assert scpi.query("CHAN:NOIS?;GAIN?") == "0;32767"


def test_rst_restores_the_defaults(scpi):
    scpi.write("MOD:MODC 0;:CHAN:ESN0 8;:MOD:ACM:LIM 0,2")
    scpi.write("*RST")
    wait_until(lambda: scpi.query("MEAS:LOCK?") == "1", 10, "lock")
    assert scpi.query("MOD:ACM?;:CHAN:NOIS?") == "1;0"
    assert scpi.query("MOD:ACM:LIM?") == "0,4"
