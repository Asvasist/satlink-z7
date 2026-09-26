"""The payload as a SCPI instrument (TCP port 5025, satlink-payloadd).

Uses pyVISA (resource ``TCPIP0::<host>::5025::SOCKET``) when it is installed, a plain socket
otherwise, with the same interface:

    inst = ScpiInstrument("192.168.1.10")
    inst.write("CHAN:ESN0 12")
    print(inst.query("MEAS:ESN0?"))

@implements SRS-GS-004
"""
from __future__ import annotations

import math
import socket
from typing import Optional

from .mission import SCPI_PORT

NAN_VALUE = 9.91e37


class ScpiError(RuntimeError):
    pass


class _SocketTransport:
    def __init__(self, host: str, port: int, timeout: float):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buffer = b""

    def write(self, line: str) -> None:
        self.sock.sendall(line.encode() + b"\n")

    def read(self) -> str:
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("instrument closed the connection")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode()

    def close(self) -> None:
        self.sock.close()


class _VisaTransport:
    def __init__(self, host: str, port: int, timeout: float, backend: str):
        import pyvisa  # optional dependency

        self.rm = pyvisa.ResourceManager(backend)
        self.res = self.rm.open_resource(f"TCPIP0::{host}::{port}::SOCKET")
        self.res.read_termination = "\n"
        self.res.write_termination = "\n"
        self.res.timeout = int(timeout * 1000)

    def write(self, line: str) -> None:
        self.res.write(line)

    def read(self) -> str:
        return self.res.read()

    def close(self) -> None:
        self.res.close()
        self.rm.close()


class ScpiInstrument:
    def __init__(self, host: str = "127.0.0.1", port: int = SCPI_PORT, timeout: float = 5.0,
                 use_visa: Optional[bool] = None, visa_backend: str = "@py"):
        if use_visa is None:
            try:
                import pyvisa  # noqa: F401

                use_visa = True
            except ImportError:
                use_visa = False
        self.transport = (_VisaTransport(host, port, timeout, visa_backend) if use_visa
                          else _SocketTransport(host, port, timeout))
        self.uses_visa = use_visa

    def close(self) -> None:
        self.transport.close()

    def __enter__(self) -> "ScpiInstrument":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def write(self, command: str) -> None:
        self.transport.write(command)

    def query(self, command: str) -> str:
        self.transport.write(command)
        return self.transport.read()

    def query_float(self, command: str) -> float:
        value = float(self.query(command))
        return math.nan if abs(value) >= NAN_VALUE * 0.999 else value

    def query_floats(self, command: str) -> list[float]:
        return [float(v) for v in self.query(command).split(",")]

    def errors(self) -> list[tuple[int, str]]:
        """Empties the error queue."""
        out = []
        while True:
            code, _, text = self.query("SYST:ERR?").partition(",")
            if int(code) == 0:
                return out
            out.append((int(code), text.strip('"')))

    def check(self) -> None:
        """Raises ScpiError if the error queue is not empty (and empties it)."""
        errors = self.errors()
        if errors:
            raise ScpiError("; ".join(f"{c} {t}" for c, t in errors))

    @property
    def idn(self) -> str:
        return self.query("*IDN?")
