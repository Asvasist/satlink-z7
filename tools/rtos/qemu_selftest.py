#!/usr/bin/env python3
"""Boot the Core 1 modem firmware in QEMU's Zynq-7000 machine and wait for its self-test.

Without Linux the firmware finds no initialised shared memory, sets up the IPC rings itself,
plays the Linux side over them and runs the modem in software loopback. It prints one STATUS
line per second and finally "SELFTEST PASS" or "SELFTEST FAIL".

    python3 tools/rtos/qemu_selftest.py build/rtos-a9/firmware/rtos/satlink_rtos.elf

Exit code 0 on PASS; the console log is written next to the ELF (qemu-selftest.log).

@verifies SRS-AMP-007
@verifies SRS-AMP-001
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import time

PASS = "SELFTEST PASS"
FAIL = "SELFTEST FAIL"


def run(elf: pathlib.Path, timeout_s: float, qemu: str = "qemu-system-arm") -> tuple[bool, str]:
    log_path = elf.with_name("qemu-selftest.log")
    log_path.unlink(missing_ok=True)
    cmd = [qemu, "-M", "xilinx-zynq-a9", "-m", "1G", "-display", "none", "-monitor", "none",
           "-serial", f"file:{log_path}", "-serial", "null", "-kernel", str(elf)]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    deadline = time.monotonic() + timeout_s
    text = ""
    try:
        while time.monotonic() < deadline and proc.poll() is None:
            time.sleep(0.5)
            text = log_path.read_text(errors="replace") if log_path.exists() else ""
            if PASS in text or FAIL in text:
                break
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    text = log_path.read_text(errors="replace") if log_path.exists() else ""
    return PASS in text, text


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("elf", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=240.0, help="seconds (wall clock)")
    parser.add_argument("--qemu", default="qemu-system-arm")
    args = parser.parse_args(argv)
    if not args.elf.is_file():
        print(f"qemu_selftest: {args.elf} not found", file=sys.stderr)
        return 2
    ok, text = run(args.elf, args.timeout, args.qemu)
    print(text)
    print("qemu_selftest:", "PASS" if ok else "FAIL (no PASS line before the timeout)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
