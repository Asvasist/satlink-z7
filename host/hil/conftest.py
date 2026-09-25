"""Hardware-in-the-loop test configuration.

    pytest host/hil --target sim                         # satlink-payloadd --sim on this PC
    pytest host/hil --target board --host 192.168.1.10   # the Zybo Z7 running the payload

The same tests run against both: telecommands and telemetry over UDP (PUS), control and
measurements over SCPI (pyVISA). Tests marked `board` need the real hardware.

@verifies SRS-HIL-001
@verifies SRS-PLM-003
@verifies SRS-SYS-003
"""
from __future__ import annotations

import os
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "host" / "python"))

from satlink import PayloadClient, ScpiInstrument  # noqa: E402


def pytest_addoption(parser):
    g = parser.getgroup("satlink")
    g.addoption("--target", choices=("sim", "board"), default="sim")
    g.addoption("--host", default="127.0.0.1", help="payload address (board)")
    g.addoption("--tc-port", type=int, default=10025)
    g.addoption("--tm-port", type=int, default=10026)
    g.addoption("--scpi-port", type=int, default=5025)
    g.addoption("--payloadd", default=os.environ.get(
        "SATLINK_PAYLOADD", str(ROOT / "build/host-debug/linux/payload/satlink-payloadd")),
        help="daemon to start for --target sim")
    g.addoption("--no-visa", action="store_true", help="plain socket instead of pyVISA")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--target") == "board":
        return
    skip = pytest.mark.skip(reason="needs the board (--target board)")
    for item in items:
        if "board" in item.keywords:
            item.add_marker(skip)


def _free_port(kind=socket.SOCK_STREAM) -> int:
    with socket.socket(socket.AF_INET, kind) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Target:
    def __init__(self, host, tc_port, tm_port, scpi_port, simulated):
        self.host, self.tc_port, self.tm_port, self.scpi_port = host, tc_port, tm_port, scpi_port
        self.simulated = simulated


@pytest.fixture(scope="session")
def target(request):
    opt = request.config.getoption
    if opt("--target") == "board":
        yield Target(opt("--host"), opt("--tc-port"), opt("--tm-port"), opt("--scpi-port"), False)
        return

    daemon = Path(opt("--payloadd"))
    if not daemon.exists():
        pytest.exit(f"{daemon} not found: build the host preset or pass --payloadd", 2)
    tc, tm, scpi = _free_port(socket.SOCK_DGRAM), _free_port(socket.SOCK_DGRAM), _free_port()
    log = open(ROOT / "build" / "hil-payloadd.log", "w") if (ROOT / "build").is_dir() else None
    proc = subprocess.Popen(
        [str(daemon), "--sim", "--tc-port", str(tc), "--scpi-port", str(scpi),
         "--gs", f"127.0.0.1:{tm}", "--verbose"],
        stdout=log or subprocess.DEVNULL, stderr=subprocess.STDOUT)
    deadline = time.monotonic() + 10
    while True:
        try:
            socket.create_connection(("127.0.0.1", scpi), timeout=0.2).close()
            break
        except OSError:
            if proc.poll() is not None or time.monotonic() > deadline:
                pytest.exit("satlink-payloadd did not start", 2)
            time.sleep(0.1)
    yield Target("127.0.0.1", tc, tm, scpi, True)
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    if log:
        log.close()


@pytest.fixture(scope="session")
def _instrument(target, request):
    inst = ScpiInstrument(target.host, target.scpi_port,
                          use_visa=False if request.config.getoption("--no-visa") else None)
    yield inst
    inst.close()


@pytest.fixture
def scpi(_instrument):
    """The instrument in its reset state, link locked."""
    _instrument.write("*RST;*CLS")
    wait_until(lambda: _instrument.query("MEAS:LOCK?") == "1", 15, "lock after *RST")
    yield _instrument
    _instrument.check()


@pytest.fixture(scope="session")
def _client(target):
    client = PayloadClient(target.host, target.tc_port, target.tm_port)
    yield client
    client.close()


@pytest.fixture
def pus(_client, scpi):
    """PUS client with the telemetry of earlier tests drained."""
    _client.collect(0.2)
    _client.history.clear()
    return _client


def wait_until(condition, timeout, what):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.2)
    raise AssertionError(f"timed out after {timeout} s waiting for {what}")
