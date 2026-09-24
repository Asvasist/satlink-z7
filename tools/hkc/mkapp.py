"""Write the SatLink application header into a linked housekeeping-controller image.

The linker script reserves the first 16 bytes of the application region as zeros; objcopy
turns the ELF into a flat binary, and this tool fills that header in (see
libs/boot/include/satlink/boot/app_header.h for the layout): magic, version, header length,
total size and the CRC-16-CCITT of everything after the header. The bootloader recomputes the
CRC before it jumps into an image that is already in memory.

    python3 tools/hkc/mkapp.py build/hkc_app.bin build/hkc_app.img

@implements SRS-HKC-003
"""
import argparse
import pathlib
import struct
import sys

MAGIC = 0x50414C53  # "SLAP", little endian
HEADER_VERSION = 1
HEADER_SIZE = 16
CRC_INIT = 0xFFFF
DEFAULT_MAX_SIZE = 0x1C000  # SATLINK_HKC_LMB_APP_SIZE in the ICD


def crc16_ccitt(data: bytes, crc: int = CRC_INIT) -> int:
    """CRC-16-CCITT, polynomial 0x1021, no reflection, no final XOR (as libs/common)."""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def make_image(raw: bytes, max_size: int = DEFAULT_MAX_SIZE) -> bytes:
    """Return @p raw with its reserved header area filled in."""
    if len(raw) <= HEADER_SIZE:
        raise ValueError("the image has no code after the 16-byte header")
    if len(raw) > max_size:
        raise ValueError(f"the image is {len(raw)} bytes, the application region holds {max_size}")
    if any(raw[:HEADER_SIZE]):
        raise ValueError("the first 16 bytes are not zero: the linker script must reserve them")

    crc = crc16_ccitt(raw[HEADER_SIZE:])
    header = struct.pack("<IHHIHH", MAGIC, HEADER_VERSION, HEADER_SIZE, len(raw), crc, 0)
    return header + raw[HEADER_SIZE:]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("input", type=pathlib.Path, help="flat binary from objcopy -O binary")
    parser.add_argument("output", type=pathlib.Path, help="image with the header filled in")
    parser.add_argument("--max-size", type=lambda s: int(s, 0), default=DEFAULT_MAX_SIZE,
                        help="largest image in bytes (default: the LMB application region)")
    args = parser.parse_args(argv)

    try:
        image = make_image(args.input.read_bytes(), args.max_size)
    except (OSError, ValueError) as error:
        print(f"mkapp: {error}", file=sys.stderr)
        return 1

    args.output.write_bytes(image)
    print(f"mkapp: wrote {args.output} ({len(image)} bytes, "
          f"crc16 0x{crc16_ccitt(image[HEADER_SIZE:]):04X})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
