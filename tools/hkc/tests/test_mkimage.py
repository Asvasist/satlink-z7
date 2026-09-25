"""Tests for tools/hkc/mkimage.py.

@verifies SRS-HKC-004
"""
import pathlib
import struct
import zlib

import pytest

from tools.hkc import mkimage


def test_header_layout_matches_the_c_definition():
    image = mkimage.build(b"\x13\x00\x00\x00" * 4, 0x4000, 0x1C000, (1, 2, 3))
    assert image[:4] == b"SLHK"
    load, entry, size, crc = struct.unpack_from("<IIII", image, 8)
    assert (load, entry, size) == (0x4000, 0x4020, 16)
    assert crc == zlib.crc32(image[32:])
    assert image[24:27] == bytes([1, 2, 3])
    assert struct.unpack_from("<I", image, 28)[0] == zlib.crc32(image[:28])


def test_round_trip():
    image = mkimage.build(bytes(range(100)), 0x4000, 0x1C000, (4, 5, 6))
    header = mkimage.parse(image)
    assert header.version == (4, 5, 6)
    assert header.payload_size == 100


@pytest.mark.parametrize("offset,message", [(0, "magic"), (25, "header CRC"), (40, "payload CRC")])
def test_parse_detects_corruption(offset, message):
    image = bytearray(mkimage.build(bytes(64), 0x4000, 0x1C000, (1, 0, 0)))
    image[offset] ^= 0xFF
    with pytest.raises(mkimage.ImageError, match=message):
        mkimage.parse(bytes(image))


def test_build_rejects_oversize_empty_and_bad_entry():
    with pytest.raises(mkimage.ImageError, match="does not fit"):
        mkimage.build(bytes(0x1C000), 0x4000, 0x1C000, (1, 0, 0))
    with pytest.raises(mkimage.ImageError, match="empty"):
        mkimage.build(b"", 0x4000, 0x1C000, (1, 0, 0))
    with pytest.raises(mkimage.ImageError, match="entry"):
        mkimage.build(bytes(8), 0x4000, 0x1C000, (1, 0, 0), entry=0x4000)


def test_cli(tmp_path: pathlib.Path, capsys):
    raw = tmp_path / "app.bin"
    raw.write_bytes(bytes(range(40)))
    out = tmp_path / "app.slhk"
    assert mkimage.main(["--version", "1.0.0", str(raw), str(out)]) == 0
    assert mkimage.main(["--info", str(out)]) == 0
    assert "version 1.0.0" in capsys.readouterr().out
    out.write_bytes(b"junk")
    assert mkimage.main(["--info", str(out)]) == 1
