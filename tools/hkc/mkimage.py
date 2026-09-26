#!/usr/bin/env python3
"""Wrap a raw MicroBlaze V application binary into an HKC image (header + payload).

The header format is defined in libs/hkc_proto/include/satlink/hkc/image.h; the C function
satlink_hkc_image_verify() checks exactly what this script writes.

Usage:
    python3 tools/hkc/mkimage.py --load-addr 0x4000 --region-size 0x1C000 --version 1.0.0 \
        hkc_app.bin hkc_app.slhk
    python3 tools/hkc/mkimage.py --info hkc_app.slhk

@implements SRS-HKC-004
"""
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import struct
import sys
import zlib

MAGIC = 0x4B484C53  # "SLHK" little-endian
HEADER_VERSION = 1
HEADER_SIZE = 32
_FIELDS = struct.Struct("<IHHIIII4B")  # everything up to (not including) header_crc32


@dataclasses.dataclass(frozen=True)
class Header:
    load_addr: int
    entry: int
    payload_size: int
    payload_crc32: int
    version: tuple[int, int, int]
    flags: int = 0
    magic: int = MAGIC
    header_version: int = HEADER_VERSION
    header_size: int = HEADER_SIZE

    def pack(self) -> bytes:
        body = _FIELDS.pack(self.magic, self.header_version, self.header_size, self.load_addr,
                            self.entry, self.payload_size, self.payload_crc32, *self.version,
                            self.flags)
        return body + struct.pack("<I", zlib.crc32(body))


class ImageError(ValueError):
    """The input cannot be turned into a valid image, or an image is invalid."""


def build(payload: bytes, load_addr: int, region_size: int, version: tuple[int, int, int],
          entry: int | None = None) -> bytes:
    if not payload:
        raise ImageError("payload is empty")
    if HEADER_SIZE + len(payload) > region_size:
        raise ImageError(f"image of {HEADER_SIZE + len(payload)} bytes does not fit the "
                         f"{region_size}-byte region")
    if any(not 0 <= v <= 255 for v in version):
        raise ImageError("version fields must be 0..255")
    entry = load_addr + HEADER_SIZE if entry is None else entry
    if not load_addr + HEADER_SIZE <= entry < load_addr + HEADER_SIZE + len(payload):
        raise ImageError("entry point lies outside the payload")
    header = Header(load_addr=load_addr, entry=entry, payload_size=len(payload),
                    payload_crc32=zlib.crc32(payload), version=version)
    return header.pack() + payload


def parse(image: bytes) -> Header:
    """Decode and fully verify an image (same checks as satlink_hkc_image_verify)."""
    if len(image) < HEADER_SIZE:
        raise ImageError("shorter than a header")
    fields = _FIELDS.unpack_from(image)
    (header_crc,) = struct.unpack_from("<I", image, 28)
    magic, hver, hsize, load_addr, entry, size, pcrc, major, minor, patch, flags = fields
    if magic != MAGIC:
        raise ImageError("bad magic")
    if hver != HEADER_VERSION or hsize != HEADER_SIZE:
        raise ImageError("unsupported header")
    if zlib.crc32(image[:28]) != header_crc:
        raise ImageError("header CRC mismatch")
    if len(image) < HEADER_SIZE + size:
        raise ImageError("truncated payload")
    if zlib.crc32(image[HEADER_SIZE:HEADER_SIZE + size]) != pcrc:
        raise ImageError("payload CRC mismatch")
    return Header(load_addr=load_addr, entry=entry, payload_size=size, payload_crc32=pcrc,
                  version=(major, minor, patch), flags=flags)


def _int(text: str) -> int:
    return int(text, 0)


def _version(text: str) -> tuple[int, int, int]:
    parts = text.split(".")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("version must be MAJOR.MINOR.PATCH")
    return int(parts[0]), int(parts[1]), int(parts[2])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--load-addr", type=_int, default=0x4000)
    parser.add_argument("--region-size", type=_int, default=0x1C000)
    parser.add_argument("--version", type=_version, default=(0, 0, 0))
    parser.add_argument("--info", action="store_true", help="verify and print an image")
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path, nargs="?")
    args = parser.parse_args(argv)

    try:
        if args.info:
            header = parse(args.input.read_bytes())
            print(f"load 0x{header.load_addr:08X} entry 0x{header.entry:08X} "
                  f"size {header.payload_size} crc 0x{header.payload_crc32:08X} "
                  f"version {'.'.join(map(str, header.version))}")
            return 0
        if args.output is None:
            parser.error("output file required")
        image = build(args.input.read_bytes(), args.load_addr, args.region_size, args.version)
        args.output.write_bytes(image)
    except (ImageError, OSError) as exc:
        print(f"mkimage: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
