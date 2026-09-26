"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 140 Plan 05 -- TABLE-04 (+ TABLE-01, TABLE-02) (D-09, D-14)

Requirements: TABLE-04, TABLE-01, TABLE-02

Defect class this closes: a table value changing without its citation
following, a cell shipping with no attribution at all, a citation for a cell
that does not exist, and a pulse-width column entering the table under any
name.

Coverage:
  1. test_blob_shas_match_the_recorded_sources -- `git rev-parse
     HEAD:<path>` for both eprom_params.h and eprom_params.cpp equals
     meta.blob_shas in the committed sidecar.
  2. test_struct_field_names_are_exactly_the_frozen_six_in_order -- the live
     ordered field list equals the frozen six, reporting the first
     divergence by index.
  3. test_no_pulse_width_column_exists -- TABLE-02: no field name matches
     (?i)(pulse_(width|delay|us)|fallback_pulse), AND the only field whose
     name contains the substring "pulse" is exactly max_pulses.
  4. test_rows_are_exactly_the_three_27c_protocols -- _row_keys yields
     exactly 0x07, 0x08, 0x0B in that order; _row_values yields exactly
     three rows of six initialisers each.
  5. test_citations_cover_every_cell_exactly_once -- the bijection: the set
     of (row, column) in the sidecar's cells equals the cross-product of the
     live row keys and the live field names, with no duplicates and exactly
     18 entries; missing and extra cells are named separately.
  6. test_every_citation_is_well_formed -- every basis == "datasheet" cell
     carries non-empty family/part/document/revision/section/quote/scope,
     with scope matching the D-09 template and its chip count; every
     basis == "reasoned" cell's reasoned_from starts with the literal
     "no datasheet basis — reasoned from " prefix.
  7. test_recorded_values_match_the_live_table -- each cell's recorded value
     equals the live initialiser at that (row, column), reporting the first
     mismatch by row, column, recorded and live value.
  8. test_inventory_is_non_vacuous -- cells_scanned == 18 and > 0; both
     source files resolve, exist and are non-empty; the live parse returns
     6 field names and 3 rows; every cell object is non-empty.
  9. test_default_targets_resolve_inside_this_repository -- the three
     default paths, recomputed from _REPO_ROOT / _HERE WITHOUT consulting
     the environment, all exist and lie inside this repository.
  10. test_git_is_required_not_optional -- this module's own source contains
      no runtime skip-bypass call and no skip-marker decorator anywhere.

Environment seams: FIRESTARTER_PARAMS_HEADER, FIRESTARTER_PARAMS_SOURCE and
FIRESTARTER_PARAMS_CITATIONS override the default header / source / sidecar
paths respectively. All three bind AT IMPORT (module load time, via
os.environ.get below) -- they must be set in the environment of the pytest
child process before it starts; monkeypatching os.environ inside a test has
no effect on the already-resolved Path objects. They exist ONLY for
planted-violation runs (see this phase's SUMMARY.md, Runs A/B/C/D/E). Tests
1 and 9 deliberately ignore them by construction: test 1 always names
_HEADER_REL / _SOURCE_REL (fixed, repo-relative strings) as the `git
rev-parse HEAD:<path>` argument, never an env-seam-resolved absolute path,
and test 9 recomputes its own targets straight from _REPO_ROOT / _HERE. A
stray FIRESTARTER_PARAMS_* variable left set in CI must never make either
check silently look at the wrong file.

This module never imports check_*.py machinery: it is a standalone pytest
module asserting directly against the committed sidecar JSON and the live
firmware tree via `git` subprocess calls (list-form argv, invoked directly
rather than through a shell) and plain file reads. The parsers below
deliberately duplicate the derivation used to author the sidecar, rather
than importing a shared helper -- the sidecar and the live tree are meant to
be compared by two INDEPENDENT readings, not by one parser trusting its own
prior output.

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo; a recorded house-rule
pattern decision, not an omission). Stdlib and pytest only.
"""

import json
import os
import re
import shutil
import subprocess
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent

_HEADER_REL = "include/eprom_params.h"
_SOURCE_REL = "src/proms/eprom_params.cpp"
_CITATIONS_DEFAULT = _HERE / "golden" / "eprom_params_citations.json"

# Environment seams -- bind AT IMPORT. See module docstring "Environment
# seams" section: these exist only for planted-violation runs, and tests 1
# and 9 ignore them by construction.
_HEADER_PATH = Path(os.environ.get("FIRESTARTER_PARAMS_HEADER", _REPO_ROOT / _HEADER_REL))
_SOURCE_PATH = Path(os.environ.get("FIRESTARTER_PARAMS_SOURCE", _REPO_ROOT / _SOURCE_REL))
_CITATIONS_PATH = Path(os.environ.get("FIRESTARTER_PARAMS_CITATIONS", _CITATIONS_DEFAULT))

_FROZEN_FIELD_NAMES = [
    "overprogram_cap_us",
    "energy_cap_us",
    "max_pulses",
    "overprogram_factor",
    "verify_mode",
    "vpp_path",
]
_FROZEN_ROW_KEYS = ["0x07", "0x08", "0x0B"]

_PULSE_WIDTH_RE = re.compile(r"(?i)(pulse_(width|delay|us)|fallback_pulse)")

_BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT_RE = re.compile(r"//[^\n]*")
_STRING_LITERAL_RE = re.compile(r'"(?:[^"\\]|\\.)*"')

_STRUCT_BODY_RE = re.compile(
    r"typedef\s+struct\s*\{(.*?)\}\s*eprom_params_t\s*;", re.DOTALL
)
_FIELD_DECL_RE = re.compile(
    r"^\s*(?:uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t)"
    r"\s+(\w+)\s*;\s*$",
    re.MULTILINE,
)

_KEYS_ARRAY_RE = re.compile(
    r"EPROM_PARAM_KEYS\s*\[\s*\]\s*PROGMEM\s*=\s*\{(.*?)\}\s*;", re.DOTALL
)
_HEX_LITERAL_RE = re.compile(r"0[xX][0-9a-fA-F]+")

_PARAMS_ARRAY_RE = re.compile(
    r"EPROM_PARAMS\s*\[\s*\]\s*PROGMEM\s*=\s*\{(.*?)\}\s*;", re.DOTALL
)
_ROW_INIT_RE = re.compile(r"\{([^{}]*)\}")
_INT_SUFFIX_RE = re.compile(r"^(\d+)[uUlL]*$")


def _strip_comments_and_strings(text):
    """Strip C-style block/line comments and string literals before any
    regex-based structural parse. None of the regions this module parses
    (the struct body, EPROM_PARAM_KEYS[], the EPROM_PARAMS[] row
    initialisers) contain string literals in this codebase today, but
    stripping them defensively means a future inline string can never be
    mistaken for a field name or a row value. Line numbers are irrelevant
    here -- unlike the golden-trace identity pin, nothing in this module
    reports a source line."""
    text = _BLOCK_COMMENT_RE.sub("", text)
    text = _LINE_COMMENT_RE.sub("", text)
    text = _STRING_LITERAL_RE.sub('""', text)
    return text


def _struct_field_names(header_text):
    """Ordered field names inside `typedef struct { ... } eprom_params_t;`,
    independently re-derived from the header's raw text."""
    text = _strip_comments_and_strings(header_text)
    m = _STRUCT_BODY_RE.search(text)
    assert m is not None, (
        "could not locate 'typedef struct { ... } eprom_params_t;' in "
        "include/eprom_params.h -- fix this locator, never the assertion "
        "that consumes it (D-15 trap 2: a pre-authored gate leg can be "
        "unreachable)."
    )
    return _FIELD_DECL_RE.findall(m.group(1))


def _normalize_hex_key(token):
    """Normalise a C hex literal (0x07, 0X0b, 0x0B, ...) to the sidecar's
    canonical form: lowercase '0x' prefix, uppercase hex digits, e.g.
    '0x0B' -- so a source-side case change can never desync from the
    sidecar's row names by case alone."""
    m = re.match(r"0[xX]([0-9a-fA-F]+)", token.strip())
    assert m is not None, f"not a hex literal: {token!r}"
    digits = m.group(1).upper()
    if len(digits) < 2:
        digits = digits.zfill(2)
    return "0x" + digits


def _row_keys(source_text):
    """Ordered literals inside `EPROM_PARAM_KEYS[] PROGMEM = { ... };`,
    normalised to the sidecar's canonical hex form."""
    text = _strip_comments_and_strings(source_text)
    m = _KEYS_ARRAY_RE.search(text)
    assert m is not None, (
        "could not locate 'EPROM_PARAM_KEYS[] PROGMEM = { ... };' in "
        "src/proms/eprom_params.cpp -- fix this locator, never the "
        "assertion that consumes it (D-15 trap 2)."
    )
    return [_normalize_hex_key(tok) for tok in _HEX_LITERAL_RE.findall(m.group(1))]


def _normalize_init_token(token):
    """Strip UL/U/L integer suffixes and return an int; anything else (an
    enum identifier such as VERIFY_PER_PULSE) is returned unchanged as a
    bare string."""
    m = _INT_SUFFIX_RE.match(token)
    if m:
        return int(m.group(1))
    return token


def _row_values(source_text):
    """For each brace-initialised row inside
    `EPROM_PARAMS[] PROGMEM = { ... };`, the ordered initialiser tokens,
    positionally parallel to _row_keys()."""
    text = _strip_comments_and_strings(source_text)
    m = _PARAMS_ARRAY_RE.search(text)
    assert m is not None, (
        "could not locate 'EPROM_PARAMS[] PROGMEM = { ... };' in "
        "src/proms/eprom_params.cpp -- fix this locator, never the "
        "assertion that consumes it (D-15 trap 2)."
    )
    rows = []
    for row_m in _ROW_INIT_RE.finditer(m.group(1)):
        tokens = [tok.strip() for tok in row_m.group(1).split(",")]
        tokens = [tok for tok in tokens if tok]
        rows.append([_normalize_init_token(tok) for tok in tokens])
    return rows


def _live_table():
    """Build {row_key: {field_name: value}} from a live, independent
    re-parse of the header and source -- never trusting the sidecar's own
    recorded values. Returns (table, field_names, row_keys)."""
    header_text = _HEADER_PATH.read_text()
    source_text = _SOURCE_PATH.read_text()
    field_names = _struct_field_names(header_text)
    row_keys = _row_keys(source_text)
    row_values = _row_values(source_text)
    assert len(row_keys) == len(row_values), (
        f"EPROM_PARAM_KEYS has {len(row_keys)} entries but EPROM_PARAMS has "
        f"{len(row_values)} rows -- the two arrays are supposed to be "
        "positionally parallel."
    )
    table = {}
    for key, values in zip(row_keys, row_values):
        assert len(values) == len(field_names), (
            f"row {key} has {len(values)} initialiser tokens but the "
            f"struct has {len(field_names)} fields: {field_names!r} "
            f"(row tokens: {values!r})"
        )
        table[key] = dict(zip(field_names, values))
    return table, field_names, row_keys


def _load_citations():
    return json.loads(_CITATIONS_PATH.read_text())


def _resolve_git():
    """Resolve the `git` binary, fail-closed. Deliberately never bypassed
    via any decorator or runtime call that would mark this outcome as
    skipped: a missing `git` turning this gate into a silent skip would be
    exactly the defect class the pre-existing golden-trace identity pin's
    own T-124-11 finding named. If `git` (or $GIT) cannot be resolved via
    shutil.which, this raises via a plain assert, which the test runner
    reports as a FAILURE, never a skipped outcome."""
    git_bin = shutil.which(os.environ.get("GIT", "git"))
    assert git_bin is not None, (
        "git not found on PATH (checked $GIT, falling back to 'git'). This "
        "must FAIL the suite, never be silently skipped -- a missing git "
        "would otherwise turn TABLE-04's blob-identity check into a silent "
        "no-op."
    )
    return git_bin


def _git(*args):
    """Run `git <args>` as a real subprocess (list-form argv, invoked
    directly rather than through a shell) against _REPO_ROOT, and assert a
    clean exit. Returns stdout, stripped."""
    git_bin = _resolve_git()
    result = subprocess.run(
        [git_bin, *args],
        cwd=str(_REPO_ROOT),
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, (
        f"git {' '.join(args)} failed (exit {result.returncode}).\n"
        f"stderr:\n{result.stderr}"
    )
    return result.stdout.strip()


def test_blob_shas_match_the_recorded_sources():
    """TABLE-04: `git rev-parse HEAD:<path>` for both sources must match the
    sidecar's meta.blob_shas. Always uses the FIXED repo-relative paths
    (_HEADER_REL, _SOURCE_REL) as the git argument, never an env-seam path
    -- a citation pinned against a blob SHA is meaningless if the SHA
    itself came from a redirected file."""
    citations = _load_citations()
    recorded = citations["meta"]["blob_shas"]
    for rel_path in (_HEADER_REL, _SOURCE_REL):
        observed = _git("rev-parse", f"HEAD:{rel_path}")
        assert observed == recorded[rel_path], (
            f"{rel_path} blob SHA changed -- recorded={recorded[rel_path]!r} "
            f"observed={observed!r}. If this file legitimately changed, "
            "re-derive tests/golden/eprom_params_citations.json's "
            "blob_shas (never hand-edit the SHA) and state in the commit "
            "message which cell's citation changed and why."
        )


def test_struct_field_names_are_exactly_the_frozen_six_in_order():
    """TABLE-01: the live struct's ordered field-name list equals the
    frozen six, in order. Reports the first divergence by index rather
    than a bare 'lists differ'."""
    live = _struct_field_names(_HEADER_PATH.read_text())
    n = min(len(live), len(_FROZEN_FIELD_NAMES))
    for i in range(n):
        assert live[i] == _FROZEN_FIELD_NAMES[i], (
            f"first divergence at field index {i} -- "
            f"expected={_FROZEN_FIELD_NAMES[i]!r} live={live[i]!r} "
            f"(expected={_FROZEN_FIELD_NAMES!r}, live={live!r})"
        )
    assert len(live) == len(_FROZEN_FIELD_NAMES), (
        f"field count diverged after {n} matching fields -- "
        f"expected_count={len(_FROZEN_FIELD_NAMES)} live_count={len(live)} "
        f"(expected={_FROZEN_FIELD_NAMES!r}, live={live!r})"
    )


def test_no_pulse_width_column_exists():
    """TABLE-02. Two independent assertions, because a naive 'no field name
    contains pulse' substring test is fooled by the legitimate field
    max_pulses -- that is the trap this test exists to catch:
      (a) no field name matches (?i)(pulse_(width|delay|us)|fallback_pulse)
      (b) the ONLY field name containing the substring 'pulse' is exactly
          max_pulses
    """
    live = _struct_field_names(_HEADER_PATH.read_text())
    for name in live:
        assert not _PULSE_WIDTH_RE.search(name), (
            f"field {name!r} matches the pulse-width pattern "
            f"{_PULSE_WIDTH_RE.pattern!r} -- TABLE-02 forbids any "
            "pulse-width column in this table; pulse width stays "
            "handle->pulse_delay."
        )
    pulse_containing = [name for name in live if "pulse" in name.lower()]
    assert pulse_containing == ["max_pulses"], (
        "expected the only 'pulse'-containing field name to be exactly "
        "'max_pulses' (a naive substring-only test is fooled by this "
        f"legitimate field -- that is the trap this assertion exists to "
        f"avoid), found {pulse_containing!r} in {live!r}"
    )


def test_rows_are_exactly_the_three_27c_protocols():
    """TABLE-01: _row_keys yields exactly 0x07, 0x08, 0x0B in that order,
    and _row_values yields exactly three rows of six initialisers each."""
    source_text = _SOURCE_PATH.read_text()
    keys = _row_keys(source_text)
    assert keys == _FROZEN_ROW_KEYS, (
        f"row key list diverged -- expected={_FROZEN_ROW_KEYS!r} "
        f"live={keys!r}"
    )
    values = _row_values(source_text)
    assert len(values) == 3, (
        f"expected exactly 3 initialiser rows, found {len(values)}: "
        f"{values!r}"
    )
    for i, row in enumerate(values):
        key = keys[i] if i < len(keys) else "?"
        assert len(row) == 6, (
            f"row index {i} (key {key}) has {len(row)} initialiser "
            f"tokens, expected 6: {row!r}"
        )


def test_citations_cover_every_cell_exactly_once():
    """TABLE-04's bijection: the set of (row, column) in the sidecar's
    cells equals the cross-product of the live row keys and the live field
    names; no duplicates; and len(cells) == 18. Missing cells (a value with
    no citation) and extra cells (a citation for a value that does not
    exist) are named separately in the failure message."""
    _table, field_names, row_keys = _live_table()
    expected = {(row, col) for row in row_keys for col in field_names}

    citations = _load_citations()
    cells = citations["cells"]
    seen = [(c["row"], c["column"]) for c in cells]
    seen_set = set(seen)

    duplicates = sorted({item for item in seen if seen.count(item) > 1})
    assert not duplicates, f"duplicate (row, column) citations: {duplicates!r}"

    missing = sorted(expected - seen_set)
    extra = sorted(seen_set - expected)
    assert not missing and not extra, (
        "citation coverage is not a bijection.\n"
        f"missing cells (a live value with no citation): {missing!r}\n"
        f"extra cells (a citation for a value that does not exist): {extra!r}"
    )
    assert len(cells) == 18, f"expected exactly 18 cells, found {len(cells)}"


def test_every_citation_is_well_formed():
    """TABLE-04: for basis == 'datasheet', family/part/document/revision/
    section/quote/scope all non-empty, and scope matches the D-09 template
    with the row's real chip count. For basis == 'reasoned', reasoned_from
    starts with meta.reasoned_prefix, and that prefix is itself asserted to
    be the literal 'no datasheet basis — reasoned from '. Any other basis
    value fails."""
    citations = _load_citations()
    meta = citations["meta"]
    prefix = meta["reasoned_prefix"]
    assert prefix == "no datasheet basis — reasoned from ", (
        "meta.reasoned_prefix must be the exact literal "
        f"'no datasheet basis — reasoned from ' (em dash, trailing "
        f"space), found {prefix!r}"
    )
    counts = meta["row_chip_counts"]
    scope_re = re.compile(
        r"^representative of this row; not asserted true of all (\d+) "
        r"chips carrying this protocol_id\.$"
    )
    for cell in citations["cells"]:
        row, col, basis = cell["row"], cell["column"], cell.get("basis")
        if basis == "datasheet":
            for key in ("family", "part", "document", "revision", "section", "quote", "scope"):
                assert cell.get(key), (
                    f"cell ({row}, {col}): datasheet citation missing or "
                    f"empty required field {key!r}"
                )
            m = scope_re.match(cell["scope"])
            assert m, (
                f"cell ({row}, {col}): scope {cell['scope']!r} does not "
                f"match the D-09 template {scope_re.pattern!r}"
            )
            expected_count = counts[row]
            assert int(m.group(1)) == expected_count, (
                f"cell ({row}, {col}): scope names {m.group(1)} chips, but "
                f"meta.row_chip_counts[{row!r}] = {expected_count}"
            )
        elif basis == "reasoned":
            reasoned_from = cell.get("reasoned_from", "")
            assert reasoned_from.startswith(prefix), (
                f"cell ({row}, {col}): reasoned_from "
                f"{reasoned_from[:60]!r}... does not start with the "
                f"required prefix {prefix!r}"
            )
        else:
            raise AssertionError(
                f"cell ({row}, {col}): unrecognised basis {basis!r} -- "
                "must be exactly 'datasheet' or 'reasoned'"
            )


def test_recorded_values_match_the_live_table():
    """TABLE-04's drift check: for each cell, the recorded value equals the
    live initialiser at that row and column position -- integers compared
    as integers, enum identifiers compared as strings. Reports the first
    mismatch naming row, column, recorded and live value. This is what
    stops a value drifting away from its citation."""
    table, _field_names, _row_keys_live = _live_table()
    citations = _load_citations()
    for cell in citations["cells"]:
        row, col, recorded = cell["row"], cell["column"], cell["value"]
        assert row in table, (
            f"cell ({row}, {col}): row {row!r} does not exist in the live "
            f"table (live rows: {sorted(table)!r})"
        )
        assert col in table[row], (
            f"cell ({row}, {col}): column {col!r} does not exist in the "
            f"live row (live columns: {sorted(table[row])!r})"
        )
        live_value = table[row][col]
        assert recorded == live_value, (
            "value drifted from its citation -- "
            f"row={row} column={col} recorded={recorded!r} live={live_value!r}"
        )


def test_inventory_is_non_vacuous():
    """D-15: cells_scanned == 18 and > 0; both source files resolve, exist
    and are non-empty; the live parse returned 6 field names and 3 rows;
    every cell object is non-empty. A zero-scan must never read as a
    pass."""
    for path in (_HEADER_PATH, _SOURCE_PATH, _CITATIONS_PATH):
        assert path.exists(), f"{path} does not exist"
        assert path.stat().st_size > 0, f"{path} exists but is empty"

    _table, field_names, row_keys = _live_table()
    assert len(field_names) == 6, (
        "non-vacuous guard: expected exactly 6 live field names, got "
        f"{len(field_names)}: {field_names!r}"
    )
    assert len(row_keys) == 3, (
        "non-vacuous guard: expected exactly 3 live rows, got "
        f"{len(row_keys)}: {row_keys!r}"
    )

    citations = _load_citations()
    cells = citations["cells"]
    cells_scanned = len(cells)
    assert cells_scanned == 18 and cells_scanned > 0, (
        f"non-vacuous guard: cells_scanned={cells_scanned}, expected "
        "exactly 18 and > 0 -- a zero-scan or truncated sidecar must FAIL, "
        "never silently pass."
    )
    for cell in cells:
        assert cell, f"empty cell object found among {cells_scanned} scanned"
        assert cell.get("row") and cell.get("column") and cell.get("basis"), (
            f"cell {cell!r} is missing row/column/basis"
        )


def test_default_targets_resolve_inside_this_repository():
    """Recomputes all three default paths from _REPO_ROOT / _HERE WITHOUT
    consulting the environment (never _HEADER_PATH / _SOURCE_PATH /
    _CITATIONS_PATH, which are env-seam-aware) -- a stray
    FIRESTARTER_PARAMS_* env var left set in CI must never make this test
    look at the wrong file."""
    default_header = _REPO_ROOT / _HEADER_REL
    default_source = _REPO_ROOT / _SOURCE_REL
    default_citations = _HERE / "golden" / "eprom_params_citations.json"
    repo_root_resolved = str(_REPO_ROOT.resolve())
    for path in (default_header, default_source, default_citations):
        assert path.exists(), f"default target {path} does not exist"
        assert path.stat().st_size > 0, f"default target {path} is empty"
        resolved = path.resolve()
        assert str(resolved).startswith(repo_root_resolved + os.sep), (
            f"default target {resolved} lies outside this repository "
            f"({repo_root_resolved})"
        )


def test_git_is_required_not_optional():
    """Self-checking: this module's own source contains no runtime
    skip-bypass call and no skip-marker decorator, i.e. the fail-closed
    contract for a missing `git` (see _resolve_git) is self-enforcing
    rather than merely documented. Mirrors the pre-existing golden-trace
    identity pin's own self-scan verbatim."""
    this_source = Path(__file__).read_text()
    for line in this_source.splitlines():
        stripped = line.strip()
        # Real usage of either construct is always a statement/decorator
        # start-of-line -- never embedded mid-string -- so startswith()
        # correctly identifies actual code while never self-matching this
        # very check's own prose (docstring text, f-string messages, and
        # this assertion's own condition text never START a line with
        # either literal).
        assert not stripped.startswith("pytest.skip"), (
            f"found a skip-bypass call at: {line!r} -- git absence must "
            "FAIL this suite, never take this bypass."
        )
        assert not stripped.startswith("@pytest.mark.skipif"), (
            f"found a skip-marker decorator at: {line!r} -- git absence "
            "must FAIL this suite, never skip it."
        )
