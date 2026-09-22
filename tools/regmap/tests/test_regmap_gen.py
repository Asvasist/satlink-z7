"""Tests for tools/regmap/regmap_gen.py (run with: python3 -m pytest tools).

@verifies SRS-ICD-001
@verifies SRS-ICD-002
@verifies SRS-ICD-003
@verifies SRS-ICD-004
@verifies SRS-SYS-002
"""
from __future__ import annotations

import pathlib
import sys
import tempfile
import textwrap
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import regmap_gen  # noqa: E402  pylint: disable=wrong-import-position

VALID_BLOCK = """\
block: demo
description: Demo block.
registers:
  - name: CTRL
    offset: 0x00
    access: RW
    reset: 0x1
    description: Control.
    fields:
      - {name: EN, bits: "0", description: Enable.}
      - {name: MODE, bits: "3:1", description: Mode.}
  - name: STATUS
    offset: 0x04
    access: W1C
    reset: 0x0
    description: Status.
    fields:
      - {name: BUSY, bits: "0", access: RO, description: Busy.}
      - {name: DONE, bits: "1", description: Done.}
"""

VALID_MAP = """\
hardware_baseline: hw-test
spaces:
  ps:
    description: Test space.
    irq_kind: GIC interrupt ID
    regions:
      - {name: ddr, base: 0x0, size: 0x10000000, owner: shared, status: fixed, description: DDR.}
      - {name: demo, base: 0x43C00000, size: 0x10000, owner: linux, status: provisional,
         regmap: demo, irqs: {done: 61}, description: Demo block.}
ddr_partitions:
  - {name: linux, base: 0x0, size: 0x08000000, owner: linux, status: provisional, description: L.}
"""


class IcdFixture(unittest.TestCase):
    """Writes a temporary icd/ directory for each test."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name)
        (self.root / "icd" / "regmap").mkdir(parents=True)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def write(self, block: str = VALID_BLOCK, amap: str = VALID_MAP, name: str = "demo") -> None:
        (self.root / "icd" / "regmap" / f"{name}.yaml").write_text(textwrap.dedent(block))
        (self.root / "icd" / "address_map.yaml").write_text(textwrap.dedent(amap))

    def generate(self) -> dict[pathlib.Path, str]:
        return regmap_gen.generate(self.root / "icd", self.root / "inc", self.root / "docs")

    def assert_rejected(self, fragment: str) -> None:
        with self.assertRaises(regmap_gen.RegmapError) as ctx:
            self.generate()
        self.assertIn(fragment, str(ctx.exception))


class TestValidInput(IcdFixture):
    def test_c_header_contains_offsets_masks_and_resets(self) -> None:
        self.write()
        c_header = self.generate()[self.root / "inc" / "demo.h"]
        self.assertIn("#define DEMO_CTRL_OFFSET (0x00U)", c_header)
        self.assertIn("#define DEMO_CTRL_MODE_MASK (0x0000000EU)", c_header)
        self.assertIn("#define DEMO_CTRL_RESET (0x00000001U)", c_header)
        self.assertIn("#define DEMO_SPAN (0x08U)", c_header)

    def test_cpp_header_uses_field_level_access(self) -> None:
        self.write()
        cpp = self.generate()[self.root / "inc" / "demo.hpp"]
        self.assertIn("inline constexpr Field kBusy{0x04U, 0U, 1U, Access::kReadOnly};", cpp)
        self.assertIn("inline constexpr Field kDone{0x04U, 1U, 1U, Access::kWrite1Clear};", cpp)

    def test_address_map_header_contains_base_and_irq(self) -> None:
        self.write()
        amap = self.generate()[self.root / "inc" / "address_map.h"]
        self.assertIn("#define SATLINK_PS_DEMO_BASE (0x43C00000U)", amap)
        self.assertIn("#define SATLINK_PS_DEMO_IRQ_DONE (61U)", amap)
        self.assertIn("#define SATLINK_DDR_LINUX_SIZE (0x08000000U)", amap)

    def test_markdown_lists_fields_with_reset_values(self) -> None:
        self.write()
        doc = self.generate()[self.root / "docs" / "demo.md"]
        self.assertIn("| 3:1 | MODE | RW | `0x0` | Mode. |", doc)
        self.assertIn("| 0 | EN | RW | `0x1` | Enable. |", doc)


class TestRegisterValidation(IcdFixture):
    def test_misaligned_offset_is_rejected(self) -> None:
        self.write(VALID_BLOCK.replace("offset: 0x04", "offset: 0x06"))
        self.assert_rejected("not 32-bit aligned")

    def test_duplicate_offset_is_rejected(self) -> None:
        self.write(VALID_BLOCK.replace("offset: 0x04", "offset: 0x00"))
        self.assert_rejected("share offset")

    def test_overlapping_fields_are_rejected(self) -> None:
        self.write(VALID_BLOCK.replace('bits: "3:1"', 'bits: "3:0"'))
        self.assert_rejected("overlaps")

    def test_field_outside_32_bits_is_rejected(self) -> None:
        self.write(VALID_BLOCK.replace('bits: "3:1"', 'bits: "32:1"'))
        self.assert_rejected("outside 31..0")

    def test_reset_bits_outside_fields_are_rejected(self) -> None:
        self.write(VALID_BLOCK.replace("reset: 0x1", "reset: 0x100"))
        self.assert_rejected("outside all fields")

    def test_unknown_access_type_is_rejected(self) -> None:
        self.write(VALID_BLOCK.replace("access: RW", "access: RX"))
        self.assert_rejected("access 'RX'")

    def test_block_name_must_match_file_name(self) -> None:
        self.write(name="other", amap=VALID_MAP.replace("regmap: demo", "regmap: other"))
        self.assert_rejected("must match the file name")


class TestAddressMapValidation(IcdFixture):
    def test_overlapping_regions_are_rejected(self) -> None:
        self.write(amap=VALID_MAP.replace("base: 0x43C00000", "base: 0x0F000000"))
        self.assert_rejected("overlap")

    def test_duplicate_irq_is_rejected(self) -> None:
        amap = VALID_MAP.replace(
            "status: fixed, description: DDR.}",
            "status: fixed, irqs: {ecc: 61}, description: DDR.}")
        self.write(amap=amap)
        self.assert_rejected("IRQ 61")

    def test_unknown_regmap_reference_is_rejected(self) -> None:
        self.write(amap=VALID_MAP.replace("regmap: demo", "regmap: missing"))
        self.assert_rejected("unknown regmap")

    def test_ddr_partition_outside_ddr_is_rejected(self) -> None:
        self.write(amap=VALID_MAP.replace("size: 0x08000000", "size: 0x20000000"))
        self.assert_rejected("outside ps/ddr")

    def test_region_without_owner_is_rejected(self) -> None:
        self.write(amap=VALID_MAP.replace("owner: linux, ", ""))
        self.assert_rejected("owner None")

    def test_unaligned_base_is_rejected(self) -> None:
        self.write(amap=VALID_MAP.replace("base: 0x43C00000", "base: 0x43C00010"))
        self.assert_rejected("4 KiB aligned")


class TestRepository(unittest.TestCase):
    def test_committed_outputs_are_up_to_date(self) -> None:
        self.assertEqual(0, regmap_gen.main(["--check"]))


if __name__ == "__main__":
    unittest.main()
