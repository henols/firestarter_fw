"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Fail-closed sync gate over the two copies of the flash-path and PCB record.

**The shared-section contract.** The two copies are
`platform/py32f071/v1.23-FLASH-PATH-DECISION.md`, the authoritative record
vendored from the meta repository, and
`platform/py32f071/FLASH-PATH-AND-PCB.md`, which is a subset. Both now live in
this repository. Five stable keys name the sections both copies must carry. Each key
appears as a suffix on a `## ` heading line in both copies: `S1` three-tier
flash path, `S2` PCB checklist, `S3` flash budget, `S4` USB VID/PID, `S5`
socket-empty instruction. Only the **body** below each heading is compared.
The meta copy numbers its headings and the subset does not, so heading text
itself is never part of the comparison. `firestarter_fw/CLAUDE.md` names the
same five keys, so the human instruction and the machine gate cannot drift
apart.

**CI coverage, stated honestly.** `pytest tests/ -v` in `build.yml` collects
and runs this module, and every leg RUNS there. Until 2026-09-27 the
comparison legs skipped in CI, because the authoritative copy lived in the meta
repository and CI checks out this repository alone. Vendoring that copy into
`platform/py32f071/` removed the skip: there is no longer any environment in
which this gate silently does nothing. Do not reintroduce a cross-repo read
here -- see agent-os `standards/testing/standalone-checkout.md`.

**Single-helper rule.** Every test, positive legs and planted-violation legs
alike, goes through the one module-level `_extract_shared_section` and
`_shared_sections` pair. The planted-violation demonstrations therefore
exercise the same checking code the positive legs do. Two parallel
implementations drift, and a planted violation then proves nothing.

**No conftest.py.** This module resolves its own paths, using the same
`_HERE`-relative idiom every other module in this directory uses. No
`conftest.py`, `pytest.ini`, `pyproject.toml`, `setup.cfg` or `tox.ini` exists
anywhere in this repository. That is a house rule, not an omission.

Both records are in-repo, so a missing one is a hard failure, never a skip --
the same `assert path.exists()` shape every other path in this module uses.
The meta-presence probe that used to mediate this (`tests/meta_presence.py`,
with its `FIRESTARTER_META_ROOT` seam, `requires_meta` marker and
`MissingScanTargetError`) was deleted along with the cross-repo read: the
fail-open it guarded against is now structurally impossible rather than
merely tested for.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
_FW_REPO_ROOT = _HERE.parent
_FW_DOC = _FW_REPO_ROOT / "platform" / "py32f071" / "FLASH-PATH-AND-PCB.md"

# The authoritative record and its seed, VENDORED from the meta repository's
# retired `.planning/` tree on 2026-09-27. Both resolve from Path(__file__)
# inside this repo -- never from a sibling or parent repo, and never from an
# environment variable. Each carries a header citing the source blob it was
# taken from; test_vendored_record_cites_its_source_blob asserts that.
_VENDORED_DOC = _FW_REPO_ROOT / "platform" / "py32f071" / "v1.23-FLASH-PATH-DECISION.md"
_VENDORED_SEED = (
    _FW_REPO_ROOT / "platform" / "py32f071" / "py32f071-no-external-tool-fw-install.md"
)
_DOC_SOURCE_BLOB = "d91283323b131cc3a09af47608c6becff723bfc5"
_SEED_SOURCE_BLOB = "76ed3093eca33fd7b5e2c0ee351cfecbab662365"
_LINKER = _FW_REPO_ROOT / "platform" / "py32f071" / "linker" / "PY32F071xB_FLASH.ld"

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
    """Assert the vendored authoritative record exists (naming the resolved
    absolute path), then return it. It lives inside this repository, so a
    missing file is a hard failure -- there is no meta-repo-absent case left
    to skip on, and no environment in which this resolves outside the repo."""
    assert _VENDORED_DOC.exists(), (
        f"{_VENDORED_DOC} does not exist. This must FAIL the suite, never be "
        "silently skipped."
    )
    return _VENDORED_DOC


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
    """Assert the vendored seed exists (naming the resolved absolute path),
    then return its text. In-repo, so a missing seed fails the suite."""
    assert _VENDORED_SEED.exists(), (
        f"{_VENDORED_SEED} does not exist. This must FAIL the suite, never be "
        "silently skipped."
    )
    return _VENDORED_SEED.read_text()


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
    """The pure RED demonstrations: none needs either real record to
    exist. These are the legs that make this gate a gate rather than a
    comment.

    Four further legs used to live here, proving the meta-presence probe
    could not silently skip: the absent-root subprocess skip, the census
    assertion that an absence claim can never be false, the
    present-root-missing-target raise, and the marker-name-not-overridable
    source scan. All four were removed on 2026-09-27 when the records were
    vendored in-repo. They are not gaps: the condition they guarded --
    a cross-repo read that can find nothing and skip -- no longer exists.
    test_no_test_reads_outside_this_repo below keeps it from coming back."""

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


    def test_no_test_reads_outside_this_repo(self):
        """The structural successor to the four meta-presence legs removed
        on 2026-09-27, and the enforcement of agent-os
        `standards/testing/standalone-checkout.md`: "A test reads only files
        inside its own repo. Never a sibling repo, the meta repo or
        `.planning/`."

        Parses every `tests/*.py` module with `ast` and refuses three
        things: an import of the deleted `meta_presence` helper, a read of
        any `FIRESTARTER_META*` environment key, and a `.planning` path in
        any string literal that is not a docstring. Docstrings and comments
        are deliberately exempt -- several modules discuss the retired tree
        in prose, and describing history is not depending on it.

        This catches the regression the old legs could not: they proved the
        cross-repo read failed LOUDLY, while this proves it is not there."""
        import ast

        # Built from character codes, not literals -- the same idiom
        # _SEED_FORBIDDEN_STATUS uses below -- so this checker's OWN source
        # does not trip it. Spelling either sentinel literally here would
        # make the module self-flagging, and the obvious fix for that
        # (exempting this file) would blind the check to the one module
        # most likely to regress.
        env_prefix = "".join(chr(c) for c in (
            70, 73, 82, 69, 83, 84, 65, 82, 84, 69, 82, 95, 77, 69, 84, 65))
        retired_tree = "".join(chr(c) for c in (
            46, 112, 108, 97, 110, 110, 105, 110, 103))

        violations = []
        for module in sorted(_HERE.glob("*.py")):
            tree = ast.parse(module.read_text(), filename=str(module))

            docstrings = set()
            for node in ast.walk(tree):
                if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef,
                                     ast.AsyncFunctionDef)):
                    doc = ast.get_docstring(node, clean=False)
                    if doc is not None:
                        docstrings.add(doc)

            for node in ast.walk(tree):
                if isinstance(node, ast.ImportFrom) and node.module:
                    if "meta_presence" in node.module:
                        violations.append(
                            f"{module.name}:{node.lineno} imports "
                            f"{node.module} -- the cross-repo presence probe "
                            "was deleted and must not return"
                        )
                elif isinstance(node, ast.Import):
                    for alias in node.names:
                        if "meta_presence" in alias.name:
                            violations.append(
                                f"{module.name}:{node.lineno} imports "
                                f"{alias.name}"
                            )
                elif isinstance(node, ast.Constant) and isinstance(node.value, str):
                    if node.value in docstrings:
                        continue
                    if node.value.startswith(env_prefix):
                        violations.append(
                            f"{module.name}:{node.lineno} names the "
                            f"environment key {node.value!r} -- the meta-root "
                            "seam was deleted with the cross-repo read"
                        )
                    if retired_tree in node.value:
                        violations.append(
                            f"{module.name}:{node.lineno} contains a "
                            f"retired-planning-tree path in a non-docstring "
                            f"string literal: {node.value!r}"
                        )

        assert not violations, (
            "test modules must read only files inside this repository "
            "(agent-os standards/testing/standalone-checkout.md). "
            "Violations:\n  " + "\n  ".join(violations)
        )


# Built at runtime from the characters of the seed's own historical status
# value, rather than embedded as a literal string in the assertion below.
_SEED_FORBIDDEN_STATUS = "".join(
    chr(c) for c in (100, 111, 114, 109, 97, 110, 116)
)


class TestFlashPathRecordSync:
    """Plan 02's live legs: D-03's parity and content half over the two
    copies of the v1.23 flash-path and PCB requirements record. Both
    copies are in-repo, so every method RUNS -- none is gated on an
    environment that might not be there. Every method re-reads and re-parses
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

    @pytest.mark.parametrize("key", _SHARED_KEYS)
    def test_meta_extract_is_non_vacuous(self, key):
        """Coverage 11 -- parses the authoritative copy only, key `key`;
        no comparison in this test. RED-by-construction while the vendored
        record does not exist: `_meta_doc()` asserts its existence, naming
        the resolved path, before any section is parsed."""
        text = _meta_doc().read_text()
        section = _extract_shared_section(text, key)
        _assert_non_vacuous(section, f"meta copy, key {key}")

    @pytest.mark.parametrize("key", _SHARED_KEYS)
    def test_fw_extract_is_non_vacuous(self, key):
        """Coverage 12 -- the same, for the firmware subset. A separate
        test from Coverage 11 (not merged into one loop), per F-14 mode 2's
        requirement of one non-vacuity assertion per parse in its own
        test."""
        text = _fw_doc_text()
        section = _extract_shared_section(text, key)
        _assert_non_vacuous(section, f"firmware subset copy, key {key}")

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

    def test_vendored_record_cites_its_source_blob(self):
        """The vendoring provenance gate, mirroring
        test_config_storage_design_vendored.py's
        test_design_doc_cites_the_vendored_blob_by_sha.

        Both vendored files must name the meta blob they were taken from,
        so a reader can recover the original and verify nothing was altered
        in transit:

            git -C <meta> cat-file -p <blob>

        A vendored copy that does not say where it came from is
        indistinguishable from one somebody edited."""
        for path, blob, label in (
            (_VENDORED_DOC, _DOC_SOURCE_BLOB, "flash-path record"),
            (_VENDORED_SEED, _SEED_SOURCE_BLOB, "fw-install seed"),
        ):
            assert path.exists(), (
                f"{path} does not exist. This must FAIL the suite, never be "
                "silently skipped."
            )
            text = path.read_text()
            assert blob in text, (
                f"the vendored {label} at {path} does not cite its source "
                f"blob {blob} -- a vendored copy must name its origin so it "
                "can be checked against the meta repository's history."
            )
            assert "VENDORED" in text, (
                f"the vendored {label} at {path} does not carry a VENDORED "
                "marker in its header."
            )
