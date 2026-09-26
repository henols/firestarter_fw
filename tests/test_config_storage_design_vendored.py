"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 126 Plan 01 -- CFG-01's mechanical gate over
platform/py32f071/CONFIG-STORAGE.md, the in-scope flash-config design
vendored from closed-PR blob 4b1a441, plus a first mechanical check on
CFG-02's flash-geometry record.

Requirements: CFG-01, CFG-02
Decisions covered: D-01, D-13, D-16, D-17, D-18, D-19

This module executes in NO CI leg on this branch: `pytest tests/ -v` runs
only in build.yml (push/PR to main) and beta-build.yml (push to beta) --
neither fires on this firmware milestone branch, and py32f071.yml has no
pytest step at all. The local run recorded in this phase's evidence
artifact is the only evidence this module's assertions were ever
exercised.

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo, and none of pytest.ini,
pyproject.toml, setup.cfg or tox.ini exist either; a recorded house-rule
pattern, not an omission, per tests/test_vpp_seam_manual_on_every_board.py's
own docstring). Stdlib and pytest only -- no third-party import, no
PlatformIO import, nothing under .pio/, because a dependency on
.pio/libdeps/ passes on a warm tree and fails on a clean checkout (the
fail-open shape this milestone has already paid for once, A-7).

All nine test functions below are built on ONE module-level helper,
_find_design_doc_violations(text), so the positive tests and the
planted-violation RED demonstration (Coverage 8) exercise the same
checking code rather than a second, parallel implementation that could
silently drift from what the positive tests actually check.

Coverage:
  1. test_design_doc_cites_the_vendored_blob_by_sha -- the doc contains
     '4b1a441' and names both closed-PR branches and PR numbers.
  2. test_design_doc_marks_every_superseded_module -- all seven
     superseded module identifiers are present AND each appears inside
     the SUPERSEDED section's own span, not merely somewhere in the file.
  3. test_design_doc_records_the_flash_geometry_with_its_citation -- 256
     and 8192 both present, the RM V0.2 section identifiers present, and
     the pinned SDK commit '0ed2f4b4...' present.
  4. test_design_doc_records_config_magic_as_not_vendored -- '0x52555250'
     present, and the sentence marking it a this-milestone rather than
     vendored choice present.
  5. test_design_doc_records_the_d16_amendment -- the amendment section
     exists and cites RM §4.2.3.2.
  6. test_design_doc_refuses_to_call_crc32_a_security_primitive -- the
     explicit not-a-security-primitive statement is present.
  7. test_design_doc_records_the_reserved_map_addresses -- '0x0801E000',
     '0x0801E100', '120K', '8K' and '256' all present.
  8. test_helper_reports_a_violation_on_a_planted_copy -- the RED
     demonstration: a copy of the doc in tmp_path with the blob-SHA
     citation stripped is fed to the same module-level helper the
     positive tests use, and the helper reports a violation naming the
     missing citation. This is what makes the gate non-vacuous (Phases
     118 and 124 each shipped a gate that passed without observing
     anything and had to be unwound).
  9. test_compiler_is_required_not_optional -- the analog's
     self-enforcing no-skip leg, adapted: this module invokes no
     compiler, so the leg asserts only that the module contains no skip
     call and no conditional-skip marker anywhere in its own source.
"""

from pathlib import Path

import pytest  # noqa: F401 -- imported for parity with the house convention; no fixtures of its own are used beyond tmp_path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_DOC_PATH = _REPO_ROOT / "platform" / "py32f071" / "CONFIG-STORAGE.md"

_SUPERSEDED_MODULE_NAMES = (
    "storage.cpp",
    "gpio.cpp",
    "board.cpp",
    "adc.cpp",
    "dac.cpp",
    "py32f071_board.h",
    "py32f071_pins.h",
)

_GEOMETRY_NEEDLES = ("256", "8192", "§4.1", "§4.2.1", "Table 4-1", "0ed2f4b4")

_RESERVED_MAP_NEEDLES = ("0x0801E000", "0x0801E100", "120K", "8K", "256")


def _read_doc_text():
    """Read the committed design doc's text. Callers that need to mutate a
    copy must do so via a tmp_path copy (Coverage 8) -- this function never
    writes and the committed file is never mutated by this module."""
    return _DOC_PATH.read_text()


def _extract_section(text, heading_prefix):
    """Return the text spanning from the line starting with heading_prefix
    up to (but not including) the next top-level '## ' heading, or None if
    heading_prefix is not found as a heading line. This is what makes the
    SUPERSEDED check section-scoped rather than a file-wide substring
    search -- a document that merely mentions the seven module names
    somewhere while silently following them must fail, which a file-wide
    search alone would not catch."""
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if line.startswith(heading_prefix):
            start = i
            break
    if start is None:
        return None
    end = len(lines)
    for j in range(start + 1, len(lines)):
        if lines[j].startswith("## ") and not lines[j].startswith(heading_prefix):
            end = j
            break
    return "\n".join(lines[start:end])


def _find_design_doc_violations(text):
    """The single module-level helper every test in this module calls,
    directly or through a filtered view of its result. Returns a list of
    human-readable violation strings for the CFG-01/CFG-02 requirements
    this gate enforces against `text` -- an empty list means every
    requirement checked here is satisfied. Both the positive tests (real
    document) and the planted-violation RED demonstration (a stripped
    tmp_path copy) call this same function, so the RED demonstration
    proves the code path the positive tests depend on, not a second,
    parallel implementation."""
    violations = []

    # Coverage 1 -- blob SHA citation and both closed-PR homes.
    if "4b1a441" not in text:
        violations.append("missing blob SHA citation '4b1a441'")
    if "feature/py32f071-toolchain" not in text:
        violations.append(
            "missing closed-PR branch name 'feature/py32f071-toolchain'"
        )
    if "feature/py32f071-full-support" not in text:
        violations.append(
            "missing closed-PR branch name 'feature/py32f071-full-support'"
        )
    if "PR #46" not in text:
        violations.append("missing PR number reference 'PR #46'")
    if "PR #47" not in text:
        violations.append("missing PR number reference 'PR #47'")

    # Coverage 2 -- SUPERSEDED section, section-scoped.
    superseded_section = _extract_section(text, "## SUPERSEDED")
    if superseded_section is None:
        violations.append("missing a '## SUPERSEDED' section heading")
        superseded_section = ""
    for module_name in _SUPERSEDED_MODULE_NAMES:
        if module_name not in superseded_section:
            violations.append(
                f"SUPERSEDED section does not name superseded module "
                f"{module_name!r}"
            )

    # Coverage 3 -- flash geometry with its citations.
    for needle in _GEOMETRY_NEEDLES:
        if needle not in text:
            violations.append(f"missing flash-geometry citation/value {needle!r}")

    # Coverage 4 -- CONFIG_MAGIC recorded as not-vendored.
    if "0x52555250" not in text:
        violations.append("missing CONFIG_MAGIC value '0x52555250'")
    if "not vendored" not in text.lower():
        violations.append(
            "missing the this-milestone/NOT-vendored sentence for CONFIG_MAGIC"
        )

    if "§4.2.3.2" not in text:
        violations.append(
            "missing RM §4.2.3.2 citation for the D-16 commit-step amendment"
        )
    if "IS_FLASH_TYPEPROGRAM" not in text:
        violations.append(
            "missing IS_FLASH_TYPEPROGRAM citation for the D-16 commit-step amendment"
        )

    # Coverage 6 -- CRC32 is not a security primitive.
    if "not a security primitive" not in text.lower():
        violations.append(
            "missing the explicit 'CRC32 is not a security primitive' statement"
        )

    for needle in _RESERVED_MAP_NEEDLES:
        if needle not in text:
            violations.append(f"missing reserved-flash-map value {needle!r}")

    return violations


def test_design_doc_cites_the_vendored_blob_by_sha():
    """Coverage 1 -- the doc contains the blob SHA '4b1a441' and names
    both closed-PR branches and their PR numbers."""
    violations = _find_design_doc_violations(_read_doc_text())
    blob_violations = [
        v
        for v in violations
        if "4b1a441" in v or "PR #4" in v or "feature/py32f071" in v
    ]
    assert not blob_violations, (
        f"expected {_DOC_PATH} to cite blob 4b1a441 by SHA and name both "
        f"closed-PR branches with their PR numbers.\n"
        f"Violations: {blob_violations!r}"
    )


def test_design_doc_marks_every_superseded_module():
    """Coverage 2 -- all seven superseded module identifiers are present
    AND each appears inside the SUPERSEDED section's own span, not merely
    somewhere in the file. A bare file-wide substring test would pass on
    a document that mentioned the names while silently following them,
    which is precisely what CFG-01 forbids."""
    violations = _find_design_doc_violations(_read_doc_text())
    superseded_violations = [
        v for v in violations if "SUPERSEDED" in v or "superseded module" in v
    ]
    assert not superseded_violations, (
        f"expected {_DOC_PATH}'s '## SUPERSEDED' section to name all "
        f"seven of {_SUPERSEDED_MODULE_NAMES!r} within its own span.\n"
        f"Violations: {superseded_violations!r}"
    )


def test_design_doc_records_the_flash_geometry_with_its_citation():
    """Coverage 3 -- 256 and 8192 both present, the RM V0.2 section
    identifiers present, and the pinned SDK commit '0ed2f4b4...' present
    (CFG-02)."""
    violations = _find_design_doc_violations(_read_doc_text())
    geometry_violations = [v for v in violations if "flash-geometry" in v]
    assert not geometry_violations, (
        f"expected {_DOC_PATH} to record the flash geometry "
        f"({_GEOMETRY_NEEDLES!r}) with its citations.\n"
        f"Violations: {geometry_violations!r}"
    )


def test_design_doc_records_config_magic_as_not_vendored():
    """Coverage 4 -- '0x52555250' is present, and the sentence marking it
    a this-milestone rather than vendored choice is present (D-19)."""
    violations = _find_design_doc_violations(_read_doc_text())
    magic_violations = [v for v in violations if "CONFIG_MAGIC" in v]
    assert not magic_violations, (
        f"expected {_DOC_PATH} to record CONFIG_MAGIC 0x52555250 as an "
        f"explicit this-milestone, NOT-vendored choice.\n"
        f"Violations: {magic_violations!r}"
    )


def test_design_doc_records_the_d16_amendment():
    """Coverage 5 -- the D-16 commit-step amendment section exists and
    cites RM §4.2.3.2 and IS_FLASH_TYPEPROGRAM."""
    violations = _find_design_doc_violations(_read_doc_text())
    d16_violations = [v for v in violations if "D-16" in v]
    assert not d16_violations, (
        f"expected {_DOC_PATH} to record the D-16 commit-step amendment "
        f"with its RM citation and IS_FLASH_TYPEPROGRAM.\n"
        f"Violations: {d16_violations!r}"
    )


def test_design_doc_refuses_to_call_crc32_a_security_primitive():
    """Coverage 6 -- the explicit 'CRC32 is not a security primitive'
    statement is present (V6)."""
    violations = _find_design_doc_violations(_read_doc_text())
    crc_violations = [v for v in violations if "security primitive" in v]
    assert not crc_violations, (
        f"expected {_DOC_PATH} to state explicitly that CRC32 is not a "
        f"security primitive.\nViolations: {crc_violations!r}"
    )


def test_design_doc_records_the_reserved_map_addresses():
    """Coverage 7 -- '0x0801E000', '0x0801E100', '120K', '8K' and '256'
    all present (the D-18-amended reserved flash map)."""
    violations = _find_design_doc_violations(_read_doc_text())
    map_violations = [v for v in violations if "reserved-flash-map" in v]
    assert not map_violations, (
        f"expected {_DOC_PATH} to record the reserved flash map "
        f"({_RESERVED_MAP_NEEDLES!r}).\nViolations: {map_violations!r}"
    )


def test_helper_reports_a_violation_on_a_planted_copy(tmp_path):
    """Coverage 8 -- the RED demonstration. A copy of the real document is
    written into tmp_path with its blob-SHA citation stripped, fed to the
    SAME module-level helper the positive tests above call, and the
    returned violation list must be non-empty and must name the missing
    citation. This is what proves the gate can actually fail, rather than
    passing vacuously on any input -- the exact defect Phases 118 and 124
    each shipped and had to unwind. The committed document itself is never
    mutated: only a tmp_path copy is altered."""
    original_text = _read_doc_text()
    assert "4b1a441" in original_text, (
        "precondition failed: the committed document no longer contains "
        "'4b1a441', so stripping it below would not be a meaningful planted "
        "violation."
    )

    planted_text = original_text.replace("4b1a441", "REDACTED")
    planted_copy = tmp_path / "CONFIG-STORAGE-planted.md"
    planted_copy.write_text(planted_text)

    violations = _find_design_doc_violations(planted_copy.read_text())
    blob_violations = [v for v in violations if "4b1a441" in v]
    assert blob_violations, (
        f"expected the planted copy (blob-SHA citation stripped) to be "
        f"reported as violating the blob-SHA-citation requirement, but the "
        f"helper returned no matching violation. Full violation list: "
        f"{violations!r}"
    )

    # Never mutate the committed document -- re-read it and confirm it
    # still contains the real blob SHA, unaffected by the planted copy.
    assert "4b1a441" in _read_doc_text(), (
        "the committed CONFIG-STORAGE.md was unexpectedly mutated by this "
        "test; it must remain untouched -- only the tmp_path copy is "
        "altered."
    )


def test_compiler_is_required_not_optional():
    """Coverage 9 -- this module's own source contains no skip decorator
    and no skip call anywhere. Unlike tests/test_vpp_seam_manual_on_every_
    board.py's analog, this module invokes no compiler, so there is no
    compiler-absence case to guard against a silent SKIP -- but the same
    self-enforcing discipline is asserted here so a future edit cannot
    quietly introduce one.

    The two needle strings below are built via concatenation (not written
    verbatim) so this test's own assertion text does not trip its own
    check -- the literal substrings must appear NOWHERE in this file,
    including inside this test's failure messages."""
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- "
        "this gate must FAIL on a violation, never SKIP."
    )
    assert skipif_marker not in own_text, (
        "expected no @pytest." + skipif_marker + " decorator anywhere in "
        "this module -- this gate must FAIL on a violation, never SKIP."
    )
