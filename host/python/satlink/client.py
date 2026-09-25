"""UDP client for satlink-payloadd: sends telecommands, collects and decodes telemetry.

    with PayloadClient("192.168.1.10") as sat:
        sat.execute(sat.cmd.ping())
        hk = sat.wait_for(ModemHk, timeout=3)

@implements SRS-GS-004
"""
from __future__ import annotations

import socket
import time
from typing import Callable, Optional, Type, TypeVar

from .mission import TC_PORT, TM_PORT, Commander, Report, Verification, interpret
from .pus import DecodeError, Telemetry

T = TypeVar("T")


class TelecommandFailed(RuntimeError):
    def __init__(self, report: Verification):
        name = getattr(report.failure, "name", report.failure)
        super().__init__(f"TC {report.sequence_count} rejected: {name}")
        self.report = report


class PayloadClient:
    def __init__(self, host: str = "127.0.0.1", tc_port: int = TC_PORT, tm_port: int = TM_PORT,
                 bind_host: str = "0.0.0.0"):
        self.address = (host, tc_port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((bind_host, tm_port))
        self.cmd = Commander(int(time.time() * 1000))
        self.history: list[tuple[float, Report]] = []
        self.bad_packets = 0

    def close(self) -> None:
        self.sock.close()

    def __enter__(self) -> "PayloadClient":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # ---- telemetry ----

    def poll(self, timeout: float) -> Optional[Report]:
        """Next report within @p timeout seconds, or None."""
        self.sock.settimeout(max(timeout, 1e-3))
        try:
            packet, _ = self.sock.recvfrom(4096)
        except (socket.timeout, BlockingIOError):
            return None
        try:
            report = interpret(Telemetry.decode(packet))
        except DecodeError:
            self.bad_packets += 1
            return None
        self.history.append((time.monotonic(), report))
        return report

    def wait_for(self, kind: Type[T], timeout: float = 5.0,
                 where: Callable[[T], bool] = lambda r: True) -> T:
        """First report of type @p kind (received from now on) for which @p where holds."""
        deadline = time.monotonic() + timeout
        while (left := deadline - time.monotonic()) > 0:
            report = self.poll(left)
            if isinstance(report, kind) and where(report):
                return report
        raise TimeoutError(f"no {kind.__name__} within {timeout} s")

    def collect(self, seconds: float) -> list[Report]:
        deadline = time.monotonic() + seconds
        out = []
        while (left := deadline - time.monotonic()) > 0:
            report = self.poll(left)
            if report is not None:
                out.append(report)
        return out

    # ---- telecommands ----

    def send(self, tc: tuple[int, bytes]) -> int:
        seq, packet = tc
        self.sock.sendto(packet, self.address)
        return seq

    def execute(self, tc: tuple[int, bytes], timeout: float = 10.0) -> list[Report]:
        """Sends a telecommand and waits for its completion report. Raises TelecommandFailed on
        a failure report, TimeoutError without an answer. Returns the reports received."""
        seq = self.send(tc)
        deadline = time.monotonic() + timeout
        seen = []
        while (left := deadline - time.monotonic()) > 0:
            report = self.poll(left)
            if report is None:
                continue
            seen.append(report)
            if isinstance(report, Verification) and report.sequence_count == seq:
                if not report.success:
                    raise TelecommandFailed(report)
                if report.completed:
                    return seen
        raise TimeoutError(f"TC {seq}: no completion within {timeout} s")
