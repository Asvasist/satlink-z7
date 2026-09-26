#!/usr/bin/env python3
"""Boot the SatLink-Z7 kernel, device tree and root file system in QEMU and check the shell.

QEMU's xilinx-zynq-a9 machine models the Zynq-7000 PS (UARTs, SD, GEM). The kernel is
loaded directly (-kernel), so this tests kernel + device tree + root file system; the
FSBL/U-Boot path is tested on the board.

Usage:
    python3 yocto/scripts/qemu_smoke.py --deploy-dir build/tmp/deploy/images/zybo-z7-20

Needs qemu-system-arm on the PATH (host package, or Yocto's qemu-system-native).
Linux/WSL2 only (uses select() on pipes).

@implements SRS-BSP-004
@verifies SRS-BSP-004
"""
from __future__ import annotations

import argparse
import os
import pathlib
import re
import select
import shutil
import subprocess
import sys
import tempfile
import time

MACHINE = "zybo-z7-20"
DTB_NAME = "zynq-zybo-z7-satlink-amp.dtb"
KERNEL_CMDLINE = "console=ttyPS0,115200 earlycon root=/dev/mmcblk0p2 rw rootwait"
# The command echo contains "$((20+6))", the output contains "26": no false match on the echo.
PROBE_CMD = "uname -r; echo SATLINK_SMOKE_$((20+6))_OK"
PROBE_OK = "SATLINK_SMOKE_26_OK"


class Console:
    """Minimal expect-style wrapper around a subprocess' stdin/stdout."""

    def __init__(self, proc: subprocess.Popen[bytes], log: pathlib.Path) -> None:
        self._proc = proc
        self._buffer = ""
        self._log = log.open("w", encoding="utf-8")

    def expect(self, pattern: str, timeout: float) -> str:
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        assert self._proc.stdout is not None
        fd = self._proc.stdout.fileno()
        while True:
            match = regex.search(self._buffer)
            if match:
                consumed = self._buffer[: match.end()]
                self._buffer = self._buffer[match.end():]
                return consumed
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for {pattern!r}")
            if self._proc.poll() is not None:
                raise RuntimeError(f"QEMU exited with code {self._proc.returncode}")
            ready, _, _ = select.select([fd], [], [], min(remaining, 1.0))
            if ready:
                chunk = os.read(fd, 4096).decode("utf-8", errors="replace")
                self._log.write(chunk)
                self._log.flush()
                sys.stdout.write(chunk)
                sys.stdout.flush()
                self._buffer += chunk

    def send(self, line: str) -> None:
        assert self._proc.stdin is not None
        self._proc.stdin.write((line + "\n").encode())
        self._proc.stdin.flush()

    def close(self) -> None:
        self._log.close()


def _find_one(deploy: pathlib.Path, pattern: str) -> pathlib.Path:
    matches = sorted(deploy.glob(pattern))
    if not matches:
        raise FileNotFoundError(f"no {pattern} in {deploy}")
    return matches[-1]


def _sd_image(wic: pathlib.Path, workdir: pathlib.Path) -> pathlib.Path:
    """QEMU requires SD card images whose size is a power of two: copy and pad."""
    image = workdir / "sd.img"
    shutil.copyfile(wic.resolve(), image)
    size = image.stat().st_size
    padded = 1 << (size - 1).bit_length()
    with image.open("r+b") as handle:
        handle.truncate(padded)
    return image


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--deploy-dir", type=pathlib.Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-arm")
    parser.add_argument("--boot-timeout", type=float, default=300.0)
    parser.add_argument("--log", type=pathlib.Path, default=pathlib.Path("qemu-smoke.log"))
    args = parser.parse_args(argv)

    deploy = args.deploy_dir
    kernel = _find_one(deploy, "zImage")
    dtb = _find_one(deploy, DTB_NAME)
    wic = _find_one(deploy, f"satlink-image-{MACHINE}*.wic")

    with tempfile.TemporaryDirectory() as tmp:
        sd_image = _sd_image(wic, pathlib.Path(tmp))
        cmd = [
            args.qemu, "-M", "xilinx-zynq-a9", "-m", "1024",
            "-display", "none", "-monitor", "none",
            "-serial", "null", "-serial", "stdio",  # Linux console is UART1 (ttyPS0)
            "-kernel", str(kernel), "-dtb", str(dtb), "-append", KERNEL_CMDLINE,
            "-drive", f"if=sd,format=raw,file={sd_image}",
            "-nic", "user",
        ]
        print("qemu_smoke: " + " ".join(cmd), flush=True)
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        console = Console(proc, args.log)
        try:
            console.expect(r"login:", args.boot_timeout)
            console.send("root")
            console.expect(r"# ", 60)
            console.send(PROBE_CMD)
            console.expect(PROBE_OK, 30)
            print("\nqemu_smoke: PASS", flush=True)
            result = 0
        except (TimeoutError, RuntimeError) as exc:
            print(f"\nqemu_smoke: FAIL: {exc} (full log: {args.log})", file=sys.stderr)
            result = 1
        finally:
            console.close()
            proc.kill()
            proc.wait()
    return result


if __name__ == "__main__":
    sys.exit(main())
