"""A LEO pass through the emulated channel: AOS, ACM following the elevation, LOS.

@verifies SRS-ACM-002
@verifies SRS-HIL-001
"""
import pytest

from satlink.mission import Event, EventReport, LinkHk
from conftest import wait_until


@pytest.mark.slow
def test_pass_from_aos_to_los(pus, scpi):
    # 80 degree pass, 20 dB at zenith, 40x time lapse: about 15 s above the horizon.
    scpi.write("PASS:STAR 80,20,40")
    modcods = set()
    elevations = []

    def pass_over():
        active, elevation, _range, _esn0, _visible = scpi.query_floats("PASS:STAT?")
        elevations.append(elevation)
        modcods.add(int(scpi.query("MEAS:MODC?")))
        return active == 0

    wait_until(pass_over, 60, "the end of the pass")
    assert max(elevations) > 70
    assert max(modcods) >= 3, modcods  # high elevation: the efficient MODCODs
    assert min(modcods) <= 1, modcods  # low elevation: the robust ones
    # LOS is raised while the link is closed, stored, and downlinked once the channel is clear
    # again. AOS goes down at 5 degrees elevation, where frame errors can still take it: its
    # delivery is not guaranteed (no retransmission on the downlink), the geometry in the link
    # reports is.
    pus.wait_for(EventReport, 10, lambda e: e.event == Event.LOS)
    link = [r for _, r in pus.history if isinstance(r, LinkHk) and r.pass_active]
    assert link and max(r.elevation_deg for r in link) > 60
