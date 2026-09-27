"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Content gate over the "## Configuration storage" and "## Flash geometry"
sections of `platform/py32f071/DESIGN.md`.

The record was first a separate file, `CONFIG-STORAGE.md`, that vendored part
of an older design from a closed pull request. This module used to check that
file's provenance: the source blob SHA, the two closed-PR branch names and a
table of superseded module names. Those checks pinned the history of the old
file, not a fact about the firmware, so they were removed with the file. The
design facts stay checked:

1. The flash geometry: 256-byte page, 8192-byte sector, the reference-manual
   sections and the pinned SDK commit.
2. `CONFIG_MAGIC`: the value in the record equals the value in
   `src/config_storage_dualslot.h`, and the record calls it a firmware-local
   choice.
3. The commit-step amendment: the record cites RM §4.2.3.2 and
   `IS_FLASH_TYPEPROGRAM`.
4. The explicit statement that CRC32 is not a security primitive.
5. The validation order `magic`, `length`, `crc32`.

Every test calls the one helper `_find_design_doc_violations(text)`. The
planted-copy tests feed it violating input.
"""

import re
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_DOC_PATH = _REPO_ROOT / "platform" / "py32f071" / "DESIGN.md"
_HEADER_PATH = _REPO_ROOT / "platform" / "py32f071" / "src" / "config_storage_dualslot.h"

_GEOMETRY_NEEDLES = ("256", "8192", "§4.1", "§4.2.1", "Table 4-1", "0ed2f4b4")
_MAGIC_DEFINE_RE = re.compile(r"#define\s+CONFIG_MAGIC\s+\(\(uint32_t\)(0x[0-9A-Fa-f]+)\)")
_VALIDATION_ORDER_RE = re.compile(r"`magic`,\s*then\s*`length`,\s*then\s*`crc32`")


def _header_magic():
    m = _MAGIC_DEFINE_RE.search(_HEADER_PATH.read_text())
    assert m, f"no CONFIG_MAGIC #define found in {_HEADER_PATH}"
    return m.group(1)


def _extract_section(text, heading):
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.rstrip() == heading:
            end = next(
                (j for j in range(i + 1, len(lines)) if lines[j].startswith("## ")),
                len(lines),
            )
            return "\n".join(lines[i + 1 : end])
    return None


def _find_design_doc_violations(text, magic=None):
    """Return a list of violation strings for `text`. An empty list means
    every check passes. `magic` defaults to the value in the firmware
    header."""
    magic = magic if magic is not None else _header_magic()
    v = []
    geometry = _extract_section(text, "## Flash geometry")
    storage = _extract_section(text, "## Configuration storage")
    if geometry is None:
        v.append("missing the '## Flash geometry' section")
        geometry = ""
    if storage is None:
        v.append("missing the '## Configuration storage' section")
        storage = ""

    for needle in _GEOMETRY_NEEDLES:
        if needle not in geometry:
            v.append(f"flash-geometry: missing {needle!r}")

    if magic not in storage:
        v.append(f"CONFIG_MAGIC: the record does not state the header value {magic}")
    if "firmware-local choice" not in storage:
        v.append("CONFIG_MAGIC: the record does not call the value a firmware-local choice")

    if "§4.2.3.2" not in storage:
        v.append("commit-step: missing the RM §4.2.3.2 citation")
    if "IS_FLASH_TYPEPROGRAM" not in storage:
        v.append("commit-step: missing IS_FLASH_TYPEPROGRAM")

    if "not a security primitive" not in storage.lower():
        v.append("security primitive: missing 'CRC32 is not a security primitive'")

    if not _VALIDATION_ORDER_RE.search(storage):
        v.append("validation order: missing '`magic`, then `length`, then `crc32`'")
    return v


def _read_doc_text():
    assert _DOC_PATH.exists(), f"{_DOC_PATH} does not exist. This must FAIL, never skip."
    return _DOC_PATH.read_text()


def test_header_magic_is_the_rurp_value():
    """The boundary for the magic check: the header value is what the record
    documents, 'RURP' in ASCII."""
    assert _header_magic() == "0x52555250"


@pytest.mark.parametrize(
    "prefix",
    ["flash-geometry", "CONFIG_MAGIC", "commit-step", "security primitive", "validation order"],
)
def test_design_doc_passes_each_check(prefix):
    violations = [v for v in _find_design_doc_violations(_read_doc_text()) if v.startswith(prefix)]
    assert not violations, violations


def test_design_doc_has_no_violations():
    assert _find_design_doc_violations(_read_doc_text()) == []


@pytest.mark.parametrize(
    "old, new, expect",
    [
        ("0x52555250", "0x52555251", "CONFIG_MAGIC: the record does not state"),
        ("firmware-local choice", "vendored choice", "firmware-local choice"),
        ("Table 4-1", "Table 4-9", "flash-geometry: missing 'Table 4-1'"),
        ("IS_FLASH_TYPEPROGRAM", "IS_FLASH_PROGRAM_TYPE", "IS_FLASH_TYPEPROGRAM"),
        ("CRC32 is not a security primitive", "CRC32 is a check", "security primitive"),
        ("`magic`, then `length`, then `crc32`", "`crc32`, then `magic`", "validation order"),
        ("## Configuration storage", "## Config", "Configuration storage"),
    ],
)
def test_helper_reports_a_violation_on_a_planted_copy(tmp_path, old, new, expect):
    """Each planted copy breaks one check. The committed document is never
    changed: only a tmp_path copy is."""
    original = _read_doc_text()
    assert old in original, f"precondition: {old!r} is not in the committed record"
    planted = tmp_path / "DESIGN-planted.md"
    planted.write_text(original.replace(old, new))
    violations = _find_design_doc_violations(planted.read_text())
    assert violations, "the planted copy produced no violation"
    if expect is not None:
        assert any(expect in v for v in violations), violations
    assert _read_doc_text() == original


def test_magic_mismatch_with_header_is_reported():
    """A record that states a different value from the header fails, even
    when the record itself is unchanged."""
    violations = _find_design_doc_violations(_read_doc_text(), magic="0xDEADBEEF")
    assert any("0xDEADBEEF" in v for v in violations), violations


def test_no_skip_in_this_module():
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    assert skip_call not in own_text
    assert skipif_marker not in own_text
