"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Fail-closed sync gate over the two copies of the flash-path and PCB record.

**The shared-section contract.** The two copies are the flash-path and PCB
decision record in the meta repository, which is authoritative, and
`platform/py32f071/FLASH-PATH-AND-PCB.md` in this repository, which is a
subset. Five stable keys name the sections both copies must carry. Each key
appears as a suffix on a `## ` heading line in both copies: `S1` three-tier
flash path, `S2` PCB checklist, `S3` flash budget, `S4` USB VID/PID, `S5`
socket-empty instruction. Only the **body** below each heading is compared.
The meta copy numbers its headings and the subset does not, so heading text
itself is never part of the comparison. `firestarter_fw/CLAUDE.md` names the
same five keys, so the human instruction and the machine gate cannot drift
apart.

**CI coverage, stated honestly.** `pytest tests/ -v` in `build.yml` does
collect and run this module. Its cross-repo legs nevertheless **skip** there,
because CI checks out this repository alone and the meta copy is absent, which
trips the `requires_meta` collection-time marker. Enforcement of the sync is
therefore a local-run obligation for anyone editing either copy. Never imply
that CI compared the two records.

**Single-helper rule.** Every test, positive legs and planted-violation legs
alike, goes through the one module-level `_extract_shared_section` and
`_shared_sections` pair. The planted-violation demonstrations therefore
exercise the same checking code the positive legs do. Two parallel
implementations drift, and a planted violation then proves nothing.

**No conftest.py.** This module resolves its own paths, using the same
`_HERE`-relative idiom every other module in this directory uses. No
`conftest.py`, `pytest.ini`, `pyproject.toml`, `setup.cfg` or `tox.ini` exists
anywhere in this repository. That is a house rule, not an omission.

An absent meta repository must produce an auditable skip, never a silent pass.
A missing scan target under a *present* meta repository must raise
`MissingScanTargetError` rather than downgrade to a skip, because that
downgrade is the fail-open this module exists to prevent.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

# This sibling-package import was proved to resolve at plan time under both
# `python -m pytest tests/ -v` and the bare `pytest tests/ -v` console
# script, run from the firmware repo root. It is the FIRST sibling-helper
# import in this repo's test suite -- every other module self-resolves via
# `Path(__file__)`. It works because `tests/__init__.py` exists and
# pytest's prepend import mode inserts the repo root onto `sys.path`. No
# `conftest.py` is added to make this work.
from tests.meta_presence import (
    META_ABSENT_REASON,
    META_MARKER,
    META_PRESENT,
    META_ROOT,
    MissingScanTargetError,
    meta_path,
    requires_meta,
)

_HERE = Path(__file__).resolve().parent
_FW_REPO_ROOT = _HERE.parent
_FW_DOC = _FW_REPO_ROOT / "platform" / "py32f071" / "FLASH-PATH-AND-PCB.md"

_META_DOC_REL = "milestones/v1.23-FLASH-PATH-DECISION.md"
_LINKER = _FW_REPO_ROOT / "platform" / "py32f071" / "linker" / "PY32F071xB_FLASH.ld"
_SEED_REL = "seeds/py32f071-no-external-tool-fw-install.md"

_SHARED_KEYS = ("S1", "S2", "S3", "S4", "S5")

# Built PROGRAMMATICALLY from _SHARED_KEYS, not five hand-typed literal
# regexes -- so a typo in one of the five cannot silently produce a pattern
# that matches nothing. Matches a full line starting with "## " and ending
# with "[SHARED:<key>]" (trailing whitespace allowed).
_SHARED_MARKER_RE = {
    key: re.compile(r"^## .*\[SHARED:" + re.escape(key) + r"\]\s*$", re.MULTILINE)
    for key in _SHARED_KEYS
}


def _extract_shared_section(text, key):
    """Return the body between the unique '## ' heading line carrying the
    `[SHARED:<key>]` marker and the next '## ' heading line (exclusive),
    with the heading line itself excluded and trailing blank lines
    stripped. Adapted from
    tests/test_config_storage_design_vendored.py's analogous
    section-extraction helper: span-scoping matters because a document
    that merely mentions a marker
    somewhere while silently following it must fail, which a file-wide
    substring search alone would not catch.

    Returns `None` when no `## ` heading line carries the marker -- a
    renamed marker is F-14 mode 4, a refusal rather than a silent empty
    match, and is exactly why the non-vacuity assertion below is a
    separate test per parse.

    Raises `AssertionError` containing the phrase 'refusing to guess' when
    two or more `## ` heading lines carry the same marker -- a parser that
    refuses to guess which one is authoritative rather than silently
    picking the first.
    """
    pattern = _SHARED_MARKER_RE[key]
    matches = list(pattern.finditer(text))
    if len(matches) > 1:
        matched_lines = [m.group(0) for m in matches]
        raise AssertionError(
            f"{len(matches)} heading lines carry the [SHARED:{key}] marker "
            f"in this document: {matched_lines!r} -- refusing to guess "
            "which one is authoritative. A document restructure introduced "
            "a second candidate; resolve which one is real before this "
            "parser can proceed."
        )
    if not matches:
        return None

    lines = text.splitlines()
    match_start = matches[0].start()
    start_line_idx = text.count("\n", 0, match_start)
    end = len(lines)
    for j in range(start_line_idx + 1, len(lines)):
        if lines[j].startswith("## "):
            end = j
            break
    body_lines = lines[start_line_idx + 1 : end]
    while body_lines and body_lines[-1].strip() == "":
        body_lines.pop()
    return "\n".join(body_lines)


def _shared_sections(text):
    """Return a dict mapping each key in _SHARED_KEYS to
    `_extract_shared_section(text, key)`. No filtering, no defaults: a
    missing key maps to None so the non-vacuity legs can see it."""
    return {key: _extract_shared_section(text, key) for key in _SHARED_KEYS}


def _assert_non_vacuous(value, source):
    """Non-vacuity guard (research finding A-7), run BEFORE any value is
    compared: a parse that found nothing (or captured only whitespace) must
    be an AssertionError, never a silent pass -- an empty value would make
    every downstream comparison vacuously true. The exact phrase
    'vacuously true' is load-bearing: the RED tests match on it."""
    assert value is not None and value.strip(), (
        f"parsed value {value!r} from {source} -- a parse that found "
        "nothing (or captured only whitespace) would make every "
        "downstream comparison vacuously true (research finding A-7)."
    )


def _git_hash_object(path: Path) -> str:
    """Resolve `git` fail-closed and hash-object `path`."""
    git_bin = shutil.which("git")
    assert git_bin is not None, (
        "`git` binary not found on PATH. This must FAIL the suite, never "
        "be silently skipped."
    )
    result = subprocess.run(
        [git_bin, "hash-object", str(path)],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def _git_porcelain(path: Path) -> str:
    """Resolve `git` fail-closed and return `git status --porcelain` for
    `path`. Empty output means a clean tree."""
    git_bin = shutil.which("git")
    assert git_bin is not None, (
        "`git` binary not found on PATH. This must FAIL the suite, never "
        "be silently skipped."
    )
    result = subprocess.run(
        [git_bin, "-C", str(path), "status", "--porcelain"],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout


def _run_gate_in_subprocess(env_overrides, node_id=None):
    """Run `[sys.executable, "-m", "pytest", <this module's path>, "-q",
    "-rs"]` (optionally scoped to `node_id`) with `cwd=_FW_REPO_ROOT` and
    `os.environ` merged with `env_overrides`, capturing text output, and
    return the `CompletedProcess`.

    A subprocess is mandatory here because `meta_presence`'s names bind at
    import and `pytest.mark.skipif` binds at collection, so
    `monkeypatch.setenv` has no effect on either.

    Guards against infinite recursion by refusing to run (raising
    `AssertionError`) when the environment variable
    `FIRESTARTER_129_GATE_CHILD` is already set in the CURRENT process, and
    sets that variable in the child's environment. Callers that themselves
    run inside a nested child (i.e. that see the variable already set) must
    check for it and skip BEFORE calling this function, rather than relying
    on this guard alone -- see
    `test_absent_meta_root_skip_is_auditable_not_silent`.
    """
    assert os.environ.get("FIRESTARTER_129_GATE_CHILD") is None, (
        "refusing to spawn a nested gate subprocess -- "
        "FIRESTARTER_129_GATE_CHILD is already set in this process; this "
        "would recurse indefinitely if it were allowed to proceed."
    )
    module_path = str(_HERE / "test_flash_path_record_sync.py")
    target = f"{module_path}::{node_id}" if node_id else module_path
    env = dict(os.environ)
    env.update(env_overrides)
    env["FIRESTARTER_129_GATE_CHILD"] = "1"
    return subprocess.run(
        [sys.executable, "-m", "pytest", target, "-q", "-rs"],
        cwd=str(_FW_REPO_ROOT),
        env=env,
        capture_output=True,
        text=True,
    )


def _synthetic_record(bodies):
    """Build a minimal markdown document string with a title line and, for
    each key in _SHARED_KEYS, a '## ' heading line ending in that key's
    marker followed by that key's body from the `bodies` mapping. Used by
    every synthetic fixture in this module so no fixture hand-rolls
    document text."""
    lines = ["# Synthetic Flash-Path Record (test fixture)", ""]
    for key in _SHARED_KEYS:
        lines.append(f"## {key} Heading [SHARED:{key}]")
        lines.append(bodies[key])
        lines.append("")
    return "\n".join(lines)


_README = _FW_REPO_ROOT / "platform" / "py32f071" / "README.md"

# Three exact literals. Every character matters -- these are what plans
# 129-03/05/06 must reproduce verbatim in the two records. U+2014 EM DASH is
# used wherever an em dash appears, never a double hyphen. Each assertion
# against these constants is a plain substring test, so the records may wrap
# the sentence in `**` bold markers without breaking it.

_L1_NON_RETIREMENT = (
    "Landing the factory USB DFU path in v1.23 does not retire the "
    "self-flash bootloader seed."
)

_L2_SHIP_GATE = (
    "Ship gate: no PY32F071 board ships, and no release advertises a USB "
    "identity, until a PID allocated under VID 0x1209 exists."
)

_L3_SOCKET_EMPTY = (
    "Before any PY32F071 firmware install — DFU, SWD or otherwise — "
    "the PROM socket must be empty."
)

_S1_NEEDLES = (
    "self-flash bootloader",
    "CDC",
    "COBS",
    "factory USB DFU",
    "SWD",
    "intended primary",
    "maintainer/manufacturing recovery",
    "last resort",
)

_S2_NEEDLES = (
    "PF8",
    "nBOOT1",
    "PA13",
    "PA14",
    "nRST",
    "PB0",
    "PB7",
    "LQFP64",
    "CSP64",
    "QFN64",
    "LQFP48",
    "QFN48",
    "QFN56",
    "QFN32",
    "HSE",
    "PA4",
    "ADC",
    "PA11",
    "PA12",
    "1.5 kΩ",
)

# CONTEXT S"Specifics" -- "the record should state its own edges".
_S2_UNDECIDED_NEEDLES = ("socket", "ZIF", "connector", "power budget")

_S3_NEEDLES = (
    "0x08000000",
    "0x0801DFFF",
    "0x0801E000",
    "0x0801E100",
    "0x08020000",
    "120K",
    "8K",
    "256",
    "8192",
    "Sector 15",
    "__config_page_size",
    "__config_slot_a_start",
    "__config_slot_b_start",
    "__config_region_end",
    "24 KiB",
    "3 sectors",
    "14.6 KiB",
    "27,372",
    "192 B",
    "__VTOR_PRESENT",
    "SCB->VTOR",
)

# The bootloader reservation figure, in either of its two written forms:
# "24" + optional whitespace + "KiB", or "3" + whitespace + optional "whole"
# + whitespace + "sectors". Case-insensitive.
_S3_FIGURE_RE = re.compile(r"24\s*KiB|3\s+(?:whole\s+)?sectors", re.IGNORECASE)

_S3_COST_TOKENS = ("ORIGIN", "migration", "re-flash")

_S4_NEEDLES = (
    "0x1209",
    "1209:0001",
    "pid.codes",
    "0x36B7",
    "0xFFFF",
    "Puya Semiconductor",
    "usbd_cdc_if.c",
    "pycdc.inf",
    "0ed2f4b4d3391eccfd4491006a30295fd78e32c2",
    "0x0448",
    "py32_dfu.py",
    "0xFE/0x01",
)

_S5_NEEDLES = (
    "provisional",
    "RURP_PY32F071_PINMAP_PROVISIONAL",
    "direction",
    "BOOT0",
    "three board revisions",
)

_LINKER_NEEDLES = (
    "FLASH-PATH-AND-PCB.md",
    "v1.23-FLASH-PATH-DECISION.md",
    "__VTOR_PRESENT",
    "SCB->VTOR",
    "BOOTLOADER (rx) : ORIGIN = 0x08000000, LENGTH = 0",
)

_LINKER_FORBIDDEN_RE = re.compile(r"no\s+VTOR", re.IGNORECASE)


def _meta_doc() -> Path:
    """Resolve the meta repo's authoritative flash-path record through
    meta_presence.meta_path() -- never a string concatenation onto
    META_ROOT -- so a missing target under a present meta repo raises
    MissingScanTargetError instead of silently skipping."""
    return meta_path(".planning", _META_DOC_REL)


def _fw_doc_text() -> str:
    """Assert the firmware subset record exists (naming the resolved
    absolute path), then return its text. A missing subset must fail the
    suite, never be skipped."""
    assert _FW_DOC.exists(), (
        f"{_FW_DOC} does not exist. This must FAIL the suite, never be "
        "silently skipped."
    )
    return _FW_DOC.read_text()


def _readme_text() -> str:
    """Assert platform/py32f071/README.md exists (naming the resolved
    absolute path), then return its text. A missing README must fail the
    suite, never be skipped."""
    assert _README.exists(), (
        f"{_README} does not exist. This must FAIL the suite, never be "
        "silently skipped."
    )
    return _README.read_text()


def _linker_text() -> str:
    """Assert the PY32F071 linker script exists (naming the resolved
    absolute path), then return its text. A missing linker script must fail
    the suite, never be skipped."""
    assert _LINKER.exists(), (
        f"{_LINKER} does not exist. This must FAIL the suite, never be "
        "silently skipped."
    )
    return _LINKER.read_text()


def _seed_text() -> str:
    """Read the py32f071 no-external-tool-fw-install seed through
    meta_presence.meta_path(), so a missing seed under a present meta repo
    raises MissingScanTargetError instead of silently skipping."""
    return meta_path(".planning", _SEED_REL).read_text()


def _copy_text(copy_id: str) -> str:
    """Dispatch to the text of the named copy: 'meta' -> the authoritative
    record, 'fw' -> the firmware subset, 'readme' -> platform/py32f071's
    README. Any other id is a programmer error, not a test outcome."""
    if copy_id == "meta":
        return _meta_doc().read_text()
    if copy_id == "fw":
        return _fw_doc_text()
    if copy_id == "readme":
        return _readme_text()
    raise AssertionError(
        f"unknown copy_id {copy_id!r} -- expected 'meta', 'fw' or 'readme'"
    )


def _frontmatter(text):
    """Return an ordered dict of the top-level `key: value` pairs in the
    YAML block delimited by the first two lines equal to '---', requiring
    the opening delimiter on line 1. Raises AssertionError containing
    'refusing to guess' when the opening '---' is not line 1 or the closing
    '---' is absent -- D-17's seed format is a fixed four-field schema and
    this parser must not silently guess its shape."""
    lines = text.splitlines()
    if not lines or lines[0].strip() != "---":
        raise AssertionError(
            "expected the opening '---' frontmatter delimiter on line 1 -- "
            "refusing to guess where the frontmatter starts."
        )
    end_idx = None
    for i in range(1, len(lines)):
        if lines[i].strip() == "---":
            end_idx = i
            break
    if end_idx is None:
        raise AssertionError(
            "no closing '---' frontmatter delimiter found -- refusing to "
            "guess where the frontmatter ends."
        )
    result = {}
    for line in lines[1:end_idx]:
        if not line.strip() or ":" not in line:
            continue
        key, _, value = line.partition(":")
        result[key.strip()] = value.strip()
    return result


# A checklist row header: "- [ ] **R<digit> -- <title>". The Why and
# Breaks-if-omitted lines must begin after EXACTLY two leading spaces.
_ROW_HEADER_RE = re.compile(r"^- \[ \] \*\*R(\d+) — (.+)$")
_ROW_WHY_RE = re.compile(r"^  - \*Why:\*(.{20,})")
_ROW_BREAKS_RE = re.compile(r"^  - \*Breaks if omitted:\*(.{20,})")


def _checklist_rows(body):
    """Return a list of (row_id, title, why_line, breaks_line) tuples
    parsed from `body`'s '- [ ] **R<digit> -- <title>' row headers, each
    required to be followed by its two-space-indented '- *Why:*' and
    '- *Breaks if omitted:*' lines (each carrying at least twenty
    characters of text). Raises AssertionError naming the offending row id
    when the shape is violated.

    D-16: checkbox + one line of rationale + one line of what breaks. The
    shape exists so Phase 130's CLOSE-02 honesty ledger can cite specific
    rows."""
    lines = body.splitlines()
    rows = []
    for i, line in enumerate(lines):
        m = _ROW_HEADER_RE.match(line)
        if not m:
            continue
        row_id = f"R{m.group(1)}"
        title = m.group(2)
        following = [ln for ln in lines[i + 1 :] if ln.strip() != ""]
        if len(following) < 2:
            raise AssertionError(
                f"row {row_id} ({title!r}) is not followed by both its "
                "Why and Breaks-if-omitted lines."
            )
        why_line, breaks_line = following[0], following[1]
        if not _ROW_WHY_RE.match(why_line):
            raise AssertionError(
                f"row {row_id} ({title!r})'s Why line does not match the "
                f"required '  - *Why:*<20+ chars>' shape. Got: {why_line!r}"
            )
        if not _ROW_BREAKS_RE.match(breaks_line):
            raise AssertionError(
                f"row {row_id} ({title!r})'s Breaks-if-omitted line does "
                "not match the required '  - *Breaks if omitted:*<20+ "
                f"chars>' shape. Got: {breaks_line!r}"
            )
        rows.append((row_id, title, why_line, breaks_line))
    return rows


class TestFlashPathRecordSyncFailsClosed:
    """The pure RED demonstrations: none carries `@requires_meta`, none
    needs either real record to exist. These are the legs that make this
    gate a gate rather than a comment."""

    def test_absent_meta_root_skip_is_auditable_not_silent(self, tmp_path):
        """Coverage 1 -- F-14 mode 3. Point a subprocess's
        `FIRESTARTER_META_ROOT` at an empty tmp_path directory (no `.git`)
        and assert the run exits 0, reports a skip, names the resolved
        absent marker path, and never reports a failure.

        This test targets ONLY ITSELF as the subprocess's node id, so no
        other test in this module (some of which assume the real,
        present meta root) runs inside the child. The child re-enters this
        same function; the `FIRESTARTER_129_GATE_CHILD` check below fires
        on that second entry and turns it into the very skip this test is
        proving is auditable -- its reason is the child's own
        `META_ABSENT_REASON`, which names the resolved (absent) marker
        path, because that name is bound fresh in the child's own process
        from the overridden environment, not inherited via monkeypatch.
        """
        if os.environ.get("FIRESTARTER_129_GATE_CHILD"):
            pytest.skip(META_ABSENT_REASON)
            return

        empty_root = tmp_path / "empty-meta-root"
        empty_root.mkdir()
        result = _run_gate_in_subprocess(
            {"FIRESTARTER_META_ROOT": str(empty_root)},
            node_id=(
                "TestFlashPathRecordSyncFailsClosed::"
                "test_absent_meta_root_skip_is_auditable_not_silent"
            ),
        )
        output = result.stdout + result.stderr
        assert result.returncode == 0, (
            f"expected exit 0 from the absent-meta-root subprocess run, "
            f"got {result.returncode}. Output:\n{output}"
        )
        assert "skipped" in output, (
            f"expected the subprocess output to report a skip -- an "
            f"absent meta root must be auditable, never silent. "
            f"Output:\n{output}"
        )
        expected_marker = str(empty_root / ".git")
        assert expected_marker in output, (
            f"expected the subprocess output to name the resolved absent "
            f"marker path {expected_marker!r} so the skip claim is "
            f"auditable. Output:\n{output}"
        )
        assert "failed" not in output, (
            f"expected no failure in the absent-meta-root subprocess run. "
            f"Output:\n{output}"
        )

    def test_absent_meta_claim_can_never_be_false(self):
        """Coverage 2 -- the census assertion, in-process against this
        process's own already-imported, real bindings: if
        `META_MARKER.exists()` then `META_PRESENT` must be `True`, and
        `str(META_MARKER)` must be a substring of `META_ABSENT_REASON`. A
        skip claiming the meta checkout is absent while its marker exists
        is the A-7 shape and must be impossible by construction."""
        if META_MARKER.exists():
            assert META_PRESENT is True, (
                f"{META_MARKER} exists but META_PRESENT is False -- an "
                "absent claim while the marker exists would be a false "
                "skip (research finding A-7's exact shape)."
            )
        assert str(META_MARKER) in META_ABSENT_REASON, (
            f"expected the resolved marker path {str(META_MARKER)!r} to be "
            f"a substring of META_ABSENT_REASON {META_ABSENT_REASON!r} so "
            "any skip claiming absence is auditable against what was "
            "actually resolved."
        )

    def test_present_root_with_missing_target_raises_not_skips(self):
        """Coverage 3 -- F-14 mode 3's hard half. Under the real present
        meta root, a missing scan target raises `MissingScanTargetError`
        rather than ever being downgraded to a skip. The filename is
        deliberately one that will never exist so this leg is stable
        across every later wave that adds real files under `.planning/`.

        The meta root's presence is this leg's PREMISE, not its claim.
        Where the premise holds -- the devcontainer's submodule-under-meta
        checkout -- the leg runs and fails closed exactly as before. Where
        it does not, as in a standalone CI checkout of this repo where no
        meta root is fetched at all, the leg skips with the same auditable
        `META_ABSENT_REASON` every other absent-root leg in this module
        uses, rather than hard-asserting an environment fact into a
        failure. Phase 129 wrote this as a bare `assert META_PRESENT`; that
        was invisible until Phase 130 first ran this module in CI, where it
        turned an unmet premise into a red beta-release build. Scoping it
        is not a weakening: the gate's own subject -- that a missing scan
        target raises rather than skips -- is still asserted wherever it
        can be, and `test_absent_meta_claim_can_never_be_false` above
        still makes a FALSE absence claim impossible by construction."""
        if not META_PRESENT:
            pytest.skip(META_ABSENT_REASON)
        with pytest.raises(MissingScanTargetError) as exc_info:
            meta_path(".planning", "__definitely_not_a_real_file__.md")
        message = str(exc_info.value)
        expected_path = META_ROOT / ".planning" / "__definitely_not_a_real_file__.md"
        assert str(expected_path) in message, (
            f"expected the MissingScanTargetError message to name the "
            f"resolved absolute path {str(expected_path)!r}. Got: "
            f"{message!r}"
        )
        assert "update" in message, (
            f"expected the MissingScanTargetError message to instruct the "
            f"reader to update the path rather than deleting the gate. "
            f"Got: {message!r}"
        )

    def test_marker_name_is_not_overridable(self):
        """Coverage 4. Reads `tests/meta_presence.py`'s own source and
        asserts `os.environ` appears exactly once, that
        `FIRESTARTER_META_ROOT` appears, and that no other
        `FIRESTARTER_META_` identifier appears -- the seam overrides the
        root only, never the marker name."""
        source = (_FW_REPO_ROOT / "tests" / "meta_presence.py").read_text()
        assert source.count("os.environ") == 1, (
            "expected exactly one os.environ read in meta_presence.py -- "
            "the seam overrides the root only."
        )
        assert "FIRESTARTER_META_ROOT" in source
        assert "FIRESTARTER_META_MARKER" not in source, (
            "the marker name must never be overridable -- one more knob "
            "that can be set wrong in a real run."
        )

    def test_empty_extraction_is_not_a_vacuous_pass(self):
        """Coverage 5 -- F-14 mode 2. A synthetic document with no markers
        at all maps every key to None, and the non-vacuity guard raises on
        both a None parse and a whitespace-only one, naming
        'vacuously true' both times."""
        text = "# Title\n\nSome unrelated prose with no shared markers.\n"
        sections = _shared_sections(text)
        for key in _SHARED_KEYS:
            assert sections[key] is None, (
                f"expected key {key!r} to extract to None from a "
                f"marker-free document, got {sections[key]!r}"
            )
        with pytest.raises(AssertionError, match="vacuously true"):
            _assert_non_vacuous(None, "synthetic marker-free document")
        with pytest.raises(AssertionError, match="vacuously true"):
            _assert_non_vacuous("   \n", "synthetic whitespace-only body")

    def test_renamed_marker_yields_a_refusal_not_a_guess(self):
        """Coverage 6 -- F-14 mode 4. Mutating S3's marker to
        `[SHARED:S3x]` makes S3 extract to None while the other four keys
        still extract non-empty bodies, and the non-vacuity guard raises
        on the S3 result."""
        original = _synthetic_record(
            {key: f"Body text for {key}." for key in _SHARED_KEYS}
        )
        mutated = original.replace("[SHARED:S3]", "[SHARED:S3x]")
        assert mutated != original, (
            "planted mutation did not actually change the text -- the "
            "replacement target '[SHARED:S3]' was not found."
        )
        sections = _shared_sections(mutated)
        assert sections["S3"] is None, (
            f"expected the renamed S3 marker to extract to None, got "
            f"{sections['S3']!r}"
        )
        for key in ("S1", "S2", "S4", "S5"):
            assert sections[key], (
                f"expected key {key!r} to still extract a non-empty body "
                f"after only S3 was renamed, got {sections[key]!r}"
            )
        with pytest.raises(AssertionError, match="vacuously true"):
            _assert_non_vacuous(sections["S3"], "mutated synthetic document, key S3")

    def test_duplicate_marker_refuses_to_guess(self):
        """Coverage 7. Two '## ' heading lines both ending in
        `[SHARED:S2]` make the extractor refuse to guess rather than
        silently picking one."""
        text = (
            "# Title\n\n"
            "## First S2 Heading [SHARED:S2]\n"
            "First body.\n\n"
            "## Second S2 Heading [SHARED:S2]\n"
            "Second body.\n"
        )
        with pytest.raises(AssertionError, match="refusing to guess"):
            _extract_shared_section(text, "S2")

    def test_planted_divergence_in_synthetic_copies_is_detected(self):
        """Coverage 8 -- F-14 mode 1. Two synthetic documents built from
        identical bodies extract equal; mutating one copy's S3 body makes
        the two compare unequal, located in S3 specifically, not merely
        somewhere."""
        bodies = {key: f"Shared body text for {key}." for key in _SHARED_KEYS}
        doc_a = _synthetic_record(bodies)
        doc_b = _synthetic_record(bodies)
        sections_a = _shared_sections(doc_a)
        sections_b = _shared_sections(doc_b)
        assert sections_a == sections_b, (
            "expected two synthetic documents built from identical bodies "
            "to extract identically."
        )

        mutated_bodies = dict(bodies)
        mutated_bodies["S3"] = bodies["S3"] + " MUTATED."
        assert mutated_bodies["S3"] != bodies["S3"], (
            "planted mutation did not actually change the S3 body."
        )
        doc_c = _synthetic_record(mutated_bodies)
        sections_c = _shared_sections(doc_c)

        assert sections_a != sections_c, (
            "expected the mutated copy to compare unequal to the original."
        )
        diverging_keys = [k for k in _SHARED_KEYS if sections_a[k] != sections_c[k]]
        assert diverging_keys == ["S3"], (
            f"expected the divergence to be located in S3 specifically, "
            f"got {diverging_keys!r}"
        )

    def test_dirty_tree_is_detected(self, tmp_path):
        """Coverage 9 -- F-14 mode 5. A throwaway git repo under tmp_path
        with an untracked file reports non-empty porcelain; calling
        `_git_porcelain` against the real firmware repo must not raise
        (its cleanliness is not asserted here -- a mid-plan working tree
        is legitimately dirty)."""
        git_bin = shutil.which("git")
        assert git_bin is not None
        repo = tmp_path / "throwaway-repo"
        repo.mkdir()
        subprocess.run(
            [git_bin, "init"],
            cwd=str(repo),
            capture_output=True,
            text=True,
            check=True,
        )
        subprocess.run(
            [
                git_bin,
                "-c",
                "user.email=test@example.com",
                "-c",
                "user.name=Test",
                "commit",
                "--allow-empty",
                "-m",
                "empty",
            ],
            cwd=str(repo),
            capture_output=True,
            text=True,
            check=True,
        )
        (repo / "untracked.txt").write_text("untracked content\n")

        porcelain = _git_porcelain(repo)
        assert porcelain, (
            "expected a non-empty porcelain status for a repo with an "
            "untracked file."
        )

        real_porcelain = _git_porcelain(_FW_REPO_ROOT)
        assert isinstance(real_porcelain, str), (
            "calling _git_porcelain against the real firmware repo must "
            "not raise."
        )

    def test_git_binary_is_required_not_optional(self):
        """Coverage 10. `git` must be present and required -- reads this
        module's own source to assert the first `shutil.which("git")`
        call is followed closely by an `assert`, so a future edit cannot
        silently convert it into a skip."""
        assert shutil.which("git") is not None, (
            "`git` binary not found on PATH. This must FAIL the suite, "
            "never be silently skipped."
        )
        own_source = Path(__file__).read_text()
        which_git = 'shutil.which("git")'
        idx = own_source.find(which_git)
        assert idx != -1, (
            f"expected at least one {which_git} call in this module."
        )
        window = own_source[idx : idx + 200]
        assert "assert" in window, (
            f"expected an `assert` to follow {which_git} closely so a "
            "missing binary fails the suite rather than silently "
            "degrading."
        )


# Built at runtime from the characters of the seed's own historical status
# value, rather than embedded as a literal string in the assertion below.
_SEED_FORBIDDEN_STATUS = "".join(
    chr(c) for c in (100, 111, 114, 109, 97, 110, 116)
)


class TestFlashPathRecordSync:
    """Plan 02's live legs: D-03's parity and content half over the two
    copies of the v1.23 flash-path and PCB requirements record. Every
    method carries `@requires_meta`. Every method re-reads and re-parses
    (no caching across tests) and calls the `_assert_non_vacuous` guard
    before comparing anything.

    This class gates PCB-01...PCB-05 mechanically but does **NOT** close
    any of them, and no requirement is marked complete in
    `REQUIREMENTS.md` from this plan (the Phase 116 4x premature-tick
    guard, in its most copyable form, PATTERNS S4).

    All 31 legs collected here are RED on arrival -- see the module
    docstring's 'Expected-RED ledger on arrival' paragraph for the exact
    failure shape and the discharging plan for each group.
    """

    @requires_meta
    @pytest.mark.parametrize("key", _SHARED_KEYS)
    def test_meta_extract_is_non_vacuous(self, key):
        """Coverage 11 -- parses the meta copy only, key `key`; no
        comparison in this test. RED-by-construction while the meta
        record does not exist: `_meta_doc()` raises
        `MissingScanTargetError` before any section is parsed."""
        text = _meta_doc().read_text()
        section = _extract_shared_section(text, key)
        _assert_non_vacuous(section, f"meta copy, key {key}")

    @requires_meta
    @pytest.mark.parametrize("key", _SHARED_KEYS)
    def test_fw_extract_is_non_vacuous(self, key):
        """Coverage 12 -- the same, for the firmware subset. A separate
        test from Coverage 11 (not merged into one loop), per F-14 mode 2's
        requirement of one non-vacuity assertion per parse in its own
        test."""
        text = _fw_doc_text()
        section = _extract_shared_section(text, key)
        _assert_non_vacuous(section, f"firmware subset copy, key {key}")

    @requires_meta
    @pytest.mark.parametrize("key", _SHARED_KEYS)
    def test_shared_sections_match(self, key):
        """Coverage 13 -- assert non-vacuity of BOTH parses first, then
        assert the two bodies are equal. On failure the message names the
        key, the first differing line number, and both differing lines."""
        meta_text = _meta_doc().read_text()
        fw_text = _fw_doc_text()
        meta_section = _extract_shared_section(meta_text, key)
        fw_section = _extract_shared_section(fw_text, key)
        _assert_non_vacuous(meta_section, f"meta copy, key {key}")
        _assert_non_vacuous(fw_section, f"firmware subset copy, key {key}")
        if meta_section == fw_section:
            return
        meta_lines = meta_section.splitlines()
        fw_lines = fw_section.splitlines()
        first_diff = None
        for i, (a, b) in enumerate(zip(meta_lines, fw_lines)):
            if a != b:
                first_diff = i
                break
        if first_diff is None:
            first_diff = min(len(meta_lines), len(fw_lines))
        meta_line_text = (
            meta_lines[first_diff] if first_diff < len(meta_lines) else "<missing>"
        )
        fw_line_text = (
            fw_lines[first_diff] if first_diff < len(fw_lines) else "<missing>"
        )
        raise AssertionError(
            f"key {key}: meta and firmware subset bodies differ at line "
            f"{first_diff}: meta={meta_line_text!r} fw={fw_line_text!r}"
        )

    @requires_meta
    @pytest.mark.parametrize("copy_id", ("meta", "fw"))
    def test_three_tiers_and_non_retirement(self, copy_id):
        """Coverage 14 -- PCB-01. Extract S1; assert non-vacuity; assert
        every _S1_NEEDLES entry is present; assert _L1_NON_RETIREMENT is a
        substring."""
        text = _copy_text(copy_id)
        section = _extract_shared_section(text, "S1")
        _assert_non_vacuous(section, f"{copy_id} copy, key S1")
        missing = [n for n in _S1_NEEDLES if n not in section]
        assert not missing, f"{copy_id} copy S1 missing needles: {missing!r}"
        assert _L1_NON_RETIREMENT in section, (
            f"{copy_id} copy S1 does not contain the exact non-retirement "
            f"sentence: {_L1_NON_RETIREMENT!r}"
        )

    @requires_meta
    @pytest.mark.parametrize("copy_id", ("meta", "fw"))
    def test_pcb_checklist_rows_are_wellformed(self, copy_id):
        """Coverage 15 -- PCB-02 / D-14 / D-16 / F-10. Extract S2; assert
        non-vacuity; call _checklist_rows and assert exactly seven rows
        R1...R7 in ascending order; assert every _S2_NEEDLES entry is
        present; assert a '### Deliberately undecided' subsection exists
        and carries all four _S2_UNDECIDED_NEEDLES within its own span."""
        text = _copy_text(copy_id)
        section = _extract_shared_section(text, "S2")
        _assert_non_vacuous(section, f"{copy_id} copy, key S2")
        rows = _checklist_rows(section)
        row_ids = [r[0] for r in rows]
        expected_ids = [f"R{n}" for n in range(1, 8)]
        assert row_ids == expected_ids, (
            f"{copy_id} copy S2: expected rows {expected_ids!r} in order, "
            f"got {row_ids!r}"
        )
        missing = [n for n in _S2_NEEDLES if n not in section]
        assert not missing, f"{copy_id} copy S2 missing needles: {missing!r}"
        undecided_marker = "### Deliberately undecided"
        assert undecided_marker in section, (
            f"{copy_id} copy S2 has no {undecided_marker!r} subsection"
        )
        undecided_span = section[section.index(undecided_marker) :]
        missing_undecided = [
            n for n in _S2_UNDECIDED_NEEDLES if n not in undecided_span
        ]
        assert not missing_undecided, (
            f"{copy_id} copy S2's {undecided_marker!r} subsection is "
            f"missing needles: {missing_undecided!r}"
        )

    @requires_meta
    @pytest.mark.parametrize("copy_id", ("meta", "fw"))
    def test_flash_budget_cites_reserved_map(self, copy_id):
        """Coverage 16 -- PCB-03 / F-1 / F-3 / C-1 / C-4. Extract S3;
        assert non-vacuity; assert every _S3_NEEDLES entry is present,
        reporting the full list of missing needles rather than the
        first."""
        text = _copy_text(copy_id)
        section = _extract_shared_section(text, "S3")
        _assert_non_vacuous(section, f"{copy_id} copy, key S3")
        missing = [n for n in _S3_NEEDLES if n not in section]
        assert not missing, f"{copy_id} copy S3 missing needles: {missing!r}"

    @requires_meta
    @pytest.mark.parametrize("copy_id", ("meta", "fw"))
    def test_bootloader_figure_carries_its_cost(self, copy_id):
        """Coverage 17 -- D-10's proximity gate. Split S3 into lines; for
        every line matching _S3_FIGURE_RE, require at least one
        _S3_COST_TOKENS entry within the window of two lines either side.
        Assert at least one match exists first, so the gate can never pass
        because the figure is absent (the vacuous shape A-7 measured)."""
        text = _copy_text(copy_id)
        section = _extract_shared_section(text, "S3")
        _assert_non_vacuous(section, f"{copy_id} copy, key S3")
        lines = section.splitlines()
        figure_line_idxs = [
            i for i, line in enumerate(lines) if _S3_FIGURE_RE.search(line)
        ]
        assert figure_line_idxs, (
            f"{copy_id} copy S3 contains no line matching the bootloader "
            "figure regex -- the proximity gate cannot pass vacuously "
            "because the figure is absent (research finding A-7's shape)."
        )
        for idx in figure_line_idxs:
            window = lines[max(0, idx - 2) : idx + 3]
            if not any(
                any(tok in w for tok in _S3_COST_TOKENS) for w in window
            ):
                raise AssertionError(
                    f"{copy_id} copy S3 line {idx} ({lines[idx]!r}) carries "
                    "the bootloader figure with no cost token "
                    f"({_S3_COST_TOKENS!r}) within two lines either side."
                )

    @requires_meta
    @pytest.mark.parametrize("copy_id", ("meta", "fw"))
    def test_vid_pid_decision_and_ship_gate(self, copy_id):
        """Coverage 18 -- PCB-04 / C-2 / F-6 / F-7. Extract S4; assert
        non-vacuity; assert every _S4_NEEDLES entry is present; assert
        _L2_SHIP_GATE is a substring."""
        text = _copy_text(copy_id)
        section = _extract_shared_section(text, "S4")
        _assert_non_vacuous(section, f"{copy_id} copy, key S4")
        missing = [n for n in _S4_NEEDLES if n not in section]
        assert not missing, f"{copy_id} copy S4 missing needles: {missing!r}"
        assert _L2_SHIP_GATE in section, (
            f"{copy_id} copy S4 does not contain the exact ship-gate "
            f"sentence: {_L2_SHIP_GATE!r}"
        )

    @requires_meta
    @pytest.mark.parametrize("copy_id", ("meta", "fw", "readme"))
    def test_socket_empty_instruction_present(self, copy_id):
        """Coverage 19 -- PCB-05. For meta/fw, extract S5, assert
        non-vacuity, assert _L3_SOCKET_EMPTY is a substring and every
        _S5_NEEDLES entry is present. For readme, assert _L3_SOCKET_EMPTY
        is a substring of the whole README text and that the README also
        contains 'FLASH-PATH-AND-PCB.md' -- the README carries the
        instruction and a pointer, not a fourth copy of the reasoning."""
        if copy_id == "readme":
            text = _readme_text()
            assert _L3_SOCKET_EMPTY in text, (
                "README.md does not contain the exact socket-empty "
                f"sentence: {_L3_SOCKET_EMPTY!r}"
            )
            assert "FLASH-PATH-AND-PCB.md" in text, (
                "README.md does not point to FLASH-PATH-AND-PCB.md -- the "
                "README carries the instruction and a pointer, not a "
                "fourth copy of the reasoning."
            )
            return
        text = _copy_text(copy_id)
        section = _extract_shared_section(text, "S5")
        _assert_non_vacuous(section, f"{copy_id} copy, key S5")
        assert _L3_SOCKET_EMPTY in section, (
            f"{copy_id} copy S5 does not contain the exact socket-empty "
            f"sentence: {_L3_SOCKET_EMPTY!r}"
        )
        missing = [n for n in _S5_NEEDLES if n not in section]
        assert not missing, f"{copy_id} copy S5 missing needles: {missing!r}"

    @requires_meta
    def test_linker_comment_cross_references_record(self):
        """Coverage 20 -- D-11 / C-1. Read _linker_text(); assert every
        _LINKER_NEEDLES entry is present; assert _LINKER_FORBIDDEN_RE finds
        no match; assert the BOOTLOADER comment block itself (from the
        MEMORY opening brace to the BOOTLOADER (rx) line) is non-empty via
        _assert_non_vacuous -- a file-wide substring search that silently
        matched nothing would be the vacuous shape."""
        text = _linker_text()
        missing = [n for n in _LINKER_NEEDLES if n not in text]
        assert not missing, f"linker script missing needles: {missing!r}"
        forbidden_match = _LINKER_FORBIDDEN_RE.search(text)
        assert forbidden_match is None, (
            f"linker script still contains the false "
            f"{forbidden_match.group(0)!r} clause -- RESEARCH C-1: the "
            "part declares __VTOR_PRESENT 1 and the compiled SystemInit "
            "writes SCB->VTOR at every boot."
        )
        lines = text.splitlines()
        memory_idx = None
        brace_idx = None
        bootloader_idx = None
        for i, line in enumerate(lines):
            stripped = line.strip()
            # Exact-match "MEMORY" so a comment merely mentioning the word
            # (e.g. prose referencing "the MEMORY block") cannot satisfy this
            # -- only the real, structural `MEMORY` keyword line qualifies.
            if memory_idx is None and stripped == "MEMORY":
                memory_idx = i
                continue
            # The brace is the first bare "{" line found AFTER the MEMORY
            # keyword line -- GNU ld's own two-line "MEMORY\n{" convention,
            # not a same-line "MEMORY {" this file has never used.
            if memory_idx is not None and brace_idx is None and stripped == "{":
                brace_idx = i
            if stripped.startswith("BOOTLOADER (rx)"):
                bootloader_idx = i
                break
        assert brace_idx is not None and bootloader_idx is not None, (
            "could not locate the MEMORY block opening brace or the "
            "BOOTLOADER (rx) line in the linker script"
        )
        block = "\n".join(lines[brace_idx : bootloader_idx + 1])
        _assert_non_vacuous(block, "linker script MEMORY-to-BOOTLOADER span")

    @requires_meta
    def test_seed_status_is_no_longer_dormant(self):
        """Coverage 21 -- D-17 / D-18. Read _seed_text(); call
        _frontmatter; assert its key set is exactly title,
        trigger_condition, planted_date, status in that order; assert the
        status value, lowercased and stripped, is not
        _SEED_FORBIDDEN_STATUS; assert the body contains the relative
        markdown link target '../milestones/v1.23-FLASH-PATH-DECISION.md';
        assert the body contains 'FUT-N05'."""
        text = _seed_text()
        fm = _frontmatter(text)
        expected_keys = ["title", "trigger_condition", "planted_date", "status"]
        assert list(fm.keys()) == expected_keys, (
            f"seed frontmatter key order changed: {list(fm.keys())!r} -- "
            "the seed format is a fixed four-field schema (D-17 must work "
            "within it, not extend it)."
        )
        status = fm["status"].strip().lower()
        assert status != _SEED_FORBIDDEN_STATUS, (
            f"seed status is still {status!r} -- D-17 requires it be "
            "updated to reflect that the trigger fired."
        )
        assert "../milestones/v1.23-FLASH-PATH-DECISION.md" in text, (
            "seed body does not link the new record via the relative "
            "markdown link target '../milestones/v1.23-FLASH-PATH-DECISION.md'"
        )
        assert "FUT-N05" in text, "seed body does not name FUT-N05"

    @requires_meta
    def test_planted_mutation_of_the_real_subset_is_detected(
        self, tmp_path, monkeypatch
    ):
        """Coverage 22 -- F-14 mode 1 against the real artifact. The full
        PATTERNS S3b ceremony: capture _FW_DOC into a local BEFORE any
        monkeypatch; hash it with _git_hash_object; read its real text;
        produce a mutated copy by replacing the first occurrence of
        '24 KiB' inside its S3 body with '8 KiB'; assert the mutated text
        differs from the real text; write the mutated text under
        tmp_path; monkeypatch the module's own _FW_DOC constant; assert
        _extract_shared_section of S3 from the planted copy differs from
        the meta copy's S3; then assert _git_hash_object of the captured
        real path is unchanged, and _git_porcelain(_FW_REPO_ROOT) is
        empty."""
        real_path = _FW_DOC  # captured BEFORE any monkeypatch
        before_blob = _git_hash_object(real_path)
        real_text = real_path.read_text()

        replacement_target = "24 KiB"
        mutated_text = real_text.replace(replacement_target, "8 KiB", 1)
        assert mutated_text != real_text, (
            "planted mutation did not actually differ from the real text "
            f"-- the replacement target {replacement_target!r} was not "
            "found (the record's wording may have changed)."
        )

        meta_text = _meta_doc().read_text()
        meta_s3 = _extract_shared_section(meta_text, "S3")

        planted_path = tmp_path / "planted-FLASH-PATH-AND-PCB.md"
        planted_path.write_text(mutated_text)
        monkeypatch.setattr(sys.modules[__name__], "_FW_DOC", planted_path)

        planted_s3 = _extract_shared_section(_FW_DOC.read_text(), "S3")
        assert planted_s3 != meta_s3, (
            "expected the planted mutation to break parity, but the "
            "planted S3 body still equals the meta copy's S3 body."
        )

        after_blob = _git_hash_object(real_path)
        assert after_blob == before_blob, (
            "the planted mutation touched the REAL FLASH-PATH-AND-PCB.md "
            "-- it must only ever be written under tmp_path"
        )
        assert _git_porcelain(_FW_REPO_ROOT) == "", (
            "the firmware repo's working tree is no longer clean after "
            "the planted-copy test"
        )
