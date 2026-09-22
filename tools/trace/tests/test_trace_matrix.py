"""Tests for tools/trace/trace_matrix.py.

@verifies SRS-DOC-002
"""
from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import trace_matrix  # noqa: E402  pylint: disable=wrong-import-position

# Built at runtime so this file does not itself contain tags for the scanner to find.
IMPL = "@" + "implements"
VERIF = "@" + "verifies"

SDOC = """\
[DOCUMENT]
TITLE: Test

[REQUIREMENT]
UID: SRS-ABC-001
STATUS: Accepted
TITLE: First
STATEMENT: >>>
First.
<<<

[REQUIREMENT]
UID: SRS-ABC-002
STATUS: Draft
TITLE: Second
STATEMENT: >>>
Second.
<<<
"""


class TraceFixture(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name)
        (self.root / "docs" / "requirements").mkdir(parents=True)
        (self.root / "docs" / "requirements" / "srs.sdoc").write_text(SDOC)
        (self.root / "src").mkdir()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def build(self) -> tuple[str, list[str]]:
        return trace_matrix.build(self.root, self.root / "docs" / "requirements")


class TestTraceMatrix(TraceFixture):
    def test_requirements_are_parsed_with_status(self) -> None:
        reqs = trace_matrix.parse_requirements(self.root / "docs" / "requirements")
        self.assertEqual({"SRS-ABC-001", "SRS-ABC-002"}, set(reqs))
        self.assertEqual("Draft", reqs["SRS-ABC-002"].status)

    def test_tags_are_linked_to_requirements(self) -> None:
        (self.root / "src" / "a.c").write_text(f"/* {IMPL} SRS-ABC-001 */\n")
        (self.root / "src" / "test_a.c").write_text(f"/* {VERIF} SRS-ABC-001 */\n")
        markdown, errors = self.build()
        self.assertEqual([], errors)
        self.assertIn("| SRS-ABC-001 | First | Accepted | `src/a.c` | `src/test_a.c` |", markdown)
        self.assertIn("Accepted and verified by an automated test: 1 / 1", markdown)

    def test_unknown_uid_is_reported(self) -> None:
        (self.root / "src" / "b.py").write_text(f"# {VERIF} SRS-ABC-999\n")
        _, errors = self.build()
        self.assertEqual(1, len(errors))
        self.assertIn("SRS-ABC-999", errors[0])

    def test_markdown_files_are_not_scanned(self) -> None:
        (self.root / "src" / "notes.md").write_text(f"Example: {IMPL} SRS-ABC-999\n")
        _, errors = self.build()
        self.assertEqual([], errors)

    def test_duplicate_uid_is_rejected(self) -> None:
        (self.root / "docs" / "requirements" / "dup.sdoc").write_text(SDOC)
        with self.assertRaises(ValueError):
            self.build()

    def test_check_mode_detects_stale_matrix(self) -> None:
        (self.root / "src" / "a.c").write_text(f"/* {IMPL} SRS-ABC-001 */\n")
        self.assertEqual(0, trace_matrix.main(["--root", str(self.root)]))
        self.assertEqual(0, trace_matrix.main(["--root", str(self.root), "--check"]))
        (self.root / "src" / "c.c").write_text(f"/* {IMPL} SRS-ABC-002 */\n")
        self.assertEqual(1, trace_matrix.main(["--root", str(self.root), "--check"]))


if __name__ == "__main__":
    unittest.main()
