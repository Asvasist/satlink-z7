"""Tests for tools/hkc/mkapp.py.

@verifies SRS-HKC-003
"""
import struct

import pytest

from tools.hkc import mkapp

# The same image is checked byte for byte in tests/unit/boot/test_app_header.c: the tool and the
# C validator must agree.
GOLDEN_PAYLOAD = b"SatLink hkc app"
GOLDEN_IMAGE = bytes.fromhex(
    "534c4150" "0100" "1000" "1f000000" "4c6c" "0000" + GOLDEN_PAYLOAD.hex()
)


def test_crc16_ccitt_check_value():
    assert mkapp.crc16_ccitt(b"123456789") == 0x29B1


def test_crc16_ccitt_of_nothing_is_the_initial_value():
    assert mkapp.crc16_ccitt(b"") == 0xFFFF


def test_golden_image_matches_what_the_c_test_expects():
    assert mkapp.make_image(bytes(16) + GOLDEN_PAYLOAD) == GOLDEN_IMAGE


def test_header_fields():
    raw = bytes(16) + bytes(range(100))
    image = mkapp.make_image(raw)
    magic, version, header_len, size, crc, reserved = struct.unpack("<IHHIHH", image[:16])

    assert magic == 0x50414C53
    assert version == 1
    assert header_len == 16
    assert size == len(raw) == len(image)
    assert crc == mkapp.crc16_ccitt(raw[16:])
    assert reserved == 0
    assert image[16:] == raw[16:]


def test_header_area_must_be_zero():
    with pytest.raises(ValueError, match="not zero"):
        mkapp.make_image(b"\x00" * 15 + b"\x01" + b"code")


def test_an_image_without_code_is_rejected():
    with pytest.raises(ValueError, match="no code"):
        mkapp.make_image(bytes(16))


def test_the_application_region_size_is_enforced():
    mkapp.make_image(bytes(16) + b"x" * (mkapp.DEFAULT_MAX_SIZE - 16))
    with pytest.raises(ValueError, match="holds"):
        mkapp.make_image(bytes(16) + b"x" * (mkapp.DEFAULT_MAX_SIZE - 15))


def test_command_line_round_trip(tmp_path, capsys):
    source = tmp_path / "app.bin"
    target = tmp_path / "app.img"
    source.write_bytes(bytes(16) + GOLDEN_PAYLOAD)

    assert mkapp.main([str(source), str(target)]) == 0

    assert target.read_bytes() == GOLDEN_IMAGE
    assert "crc16 0x6C4C" in capsys.readouterr().out


def test_command_line_reports_errors_without_a_traceback(tmp_path, capsys):
    source = tmp_path / "app.bin"
    source.write_bytes(b"short")

    assert mkapp.main([str(source), str(tmp_path / "out.img")]) == 1
    assert "mkapp:" in capsys.readouterr().err
    assert mkapp.main([str(tmp_path / "missing.bin"), str(tmp_path / "out.img")]) == 1
