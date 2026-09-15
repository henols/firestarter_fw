"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 124 Plan 03 — MERGE-06's per-array golden-trace identity pin.

Requirements: MERGE-06

Defect class this closes: the 141-case/17-suite `pio test -e native` count
that MERGE-06's other checks rely on as "the two consuming suites pass" is
invariant under an array being deleted TOGETHER with the `TEST_ASSERT` calls
that consumed it -- both the array and its assertions vanish, so the count
stays 141/17 and the suite still reports green. A count assertion cannot see
that class of change. This module adds the two checks RESEARCH §MERGE-06
records as still missing: whole-file blob identity, and a per-array
name + entry-count inventory, independently re-parsed from the file rather
than trusted from any prior record (including this module's own committed
JSON) -- the point of an inventory pin is that the file and the recorded
expectation are read by two separate mechanisms and compared, so a change to
either alone is visible.

Coverage:
  1. test_blob_sha_matches_the_recorded_inventory -- `git rev-parse
     HEAD:test/native/avr/_shared/sdp_expected.h` equals `meta.blob_sha` in
     the committed inventory JSON.
  2. test_array_names_match_the_recorded_inventory -- the ordered list of
     array names parsed from the live file equals the ordered list of names
     in the JSON.
  3. test_array_entry_counts_match_the_recorded_inventory -- per-array entry
     counts match, positionally; the assertion message names the FIRST
     diverging array and both counts (never a bare "lists differ"), mirroring
     sdp_expected.h's own sdp_first_divergence discipline.
  4. test_inventory_is_non_vacuous -- the JSON records at least 9 arrays and
     every recorded `entries` is >= 1; an empty or truncated inventory must
     FAIL, not silently pass.
  5. test_consuming_suites_still_include_the_fixture -- both
     test/native/avr/test_sdp_harness/test_sdp_harness.cpp and
     test/native/avr/test_eeprom28c_sdp/test_eeprom28c_sdp.cpp still contain
     the literal `_shared/sdp_expected.h` include. This is the link that
     makes the fixture load-bearing: if both consumers stopped including it,
     the blob could stay byte-identical while nothing exercised it.
  6. test_git_is_required_not_optional -- this module's own source contains
     no runtime skip-bypass call and no skip-marker decorator anywhere: the
     fail-closed contract (a missing `git` binary must FAIL the suite, never
     silently skip it) is self-enforcing. (The exact two patterns this test
     scans for are named in its own docstring below, not repeated here, so
     this module-level list stays a plain description rather than a second
     copy of the literal patterns.)

This module never imports check_*.py machinery: it is a standalone pytest
module asserting directly against the committed inventory JSON and the live
firmware tree via `git` subprocess calls (list-form argv, invoked directly
rather than through a shell) and plain file reads. `_parse_arrays()`
deliberately duplicates the derivation Plan 124-03's Task 1 used to author
the JSON, rather than importing a shared helper -- the inventory and the
file are meant to be compared by two INDEPENDENT readings, not by one
parser trusting its own prior output.

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo; a recorded house-rule pattern
decision per test_update_version.py's own comment, not an omission).
Stdlib and pytest only.
"""

import json
import os
import re
import shutil
import subprocess
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_FIXTURE_PATH = "test/native/avr/_shared/sdp_expected.h"
_INVENTORY_JSON = _HERE / "golden" / "sdp_expected_inventory.json"
_CONSUMERS = (
    _REPO_ROOT / "test" / "native" / "avr" / "test_sdp_harness" / "test_sdp_harness.cpp",
    _REPO_ROOT / "test" / "native" / "avr" / "test_eeprom28c_sdp" / "test_eeprom28c_sdp.cpp",
)

_ARRAY_DECL_RE = re.compile(
    r"static const sdp_strobe_t\s+(\w+)\[\]\s*=\s*\{(.*?)\};",
    re.DOTALL,
)
_ENTRY_RE = re.compile(r"\{[^{}]*\}")


def _resolve_git():
    """Resolve the `git` binary, fail-closed.

    Deliberately never bypassed via any decorator or runtime call that would
    mark this outcome as skipped, anywhere in this module: a missing `git`
    turning this pin into a silent skip is exactly the class of defect
    T-124-11 names, and the class Phase 123 removed for the compiler-absence
    case (BASE-02/BASE-03). If `git` (or $GIT) cannot be resolved via
    shutil.which, this raises via a plain assert, which the test runner
    reports as a FAILURE, never a skipped outcome.
    """
    git_bin = shutil.which(os.environ.get("GIT", "git"))
    assert git_bin is not None, (
        "git not found on PATH (checked $GIT, falling back to 'git'). This "
        "must FAIL the suite, never be silently skipped -- a missing git "
        "would otherwise turn MERGE-06's blob-identity pin into a silent "
        "no-op (T-124-11)."
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


def _parse_arrays(text):
    """Re-derive the ordered (name, entries) pairs from sdp_expected.h's raw
    text, independently of the committed inventory JSON. Strips C-style
    comments first so commented-out entries can never inflate a count."""
    arrays = []
    for m in _ARRAY_DECL_RE.finditer(text):
        name = m.group(1)
        body = m.group(2)
        body_nc = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
        body_nc = re.sub(r"//[^\n]*", "", body_nc)
        entries = _ENTRY_RE.findall(body_nc)
        arrays.append((name, len(entries)))
    return arrays


def _load_inventory():
    return json.loads(_INVENTORY_JSON.read_text())


def _live_fixture_text():
    return (_REPO_ROOT / _FIXTURE_PATH).read_text()


def test_blob_sha_matches_the_recorded_inventory():
    inventory = _load_inventory()
    recorded_sha = inventory["meta"]["blob_sha"]
    observed_sha = _git("rev-parse", f"HEAD:{_FIXTURE_PATH}")
    assert observed_sha == recorded_sha, (
        f"{_FIXTURE_PATH} blob SHA changed -- recorded={recorded_sha} "
        f"observed={observed_sha}. If this file legitimately changed, "
        "re-derive tests/golden/sdp_expected_inventory.json from the new "
        "file (never hand-edit the SHA) and state in the commit message "
        "which array changed and why."
    )


def test_array_names_match_the_recorded_inventory():
    inventory = _load_inventory()
    recorded_names = [a["name"] for a in inventory["arrays"]]
    live_names = [name for name, _entries in _parse_arrays(_live_fixture_text())]
    assert live_names == recorded_names, (
        f"array name list diverged.\nrecorded={recorded_names}\nlive={live_names}"
    )


def test_array_entry_counts_match_the_recorded_inventory():
    inventory = _load_inventory()
    recorded = [(a["name"], a["entries"]) for a in inventory["arrays"]]
    live = _parse_arrays(_live_fixture_text())

    n = min(len(recorded), len(live))
    for i in range(n):
        rec_name, rec_entries = recorded[i]
        live_name, live_entries = live[i]
        if rec_name != live_name or rec_entries != live_entries:
            raise AssertionError(
                f"first divergence at index {i} -- "
                f"recorded={{'name': {rec_name!r}, 'entries': {rec_entries}}}, "
                f"live={{'name': {live_name!r}, 'entries': {live_entries}}}"
            )
    assert len(recorded) == len(live), (
        f"array count diverged after {n} matching entries -- "
        f"recorded_count={len(recorded)} live_count={len(live)}"
    )


def test_inventory_is_non_vacuous():
    inventory = _load_inventory()
    arrays = inventory["arrays"]
    assert len(arrays) >= 9, (
        f"non-vacuous guard: expected >= 9 recorded arrays, got {len(arrays)} "
        "-- an empty or truncated inventory must FAIL, not silently pass."
    )
    for a in arrays:
        assert a["entries"] >= 1, (
            f"non-vacuous guard: array {a['name']!r} records entries="
            f"{a['entries']}, expected >= 1."
        )


def test_consuming_suites_still_include_the_fixture():
    for consumer in _CONSUMERS:
        text = consumer.read_text()
        assert "_shared/sdp_expected.h" in text, (
            f"{consumer} no longer includes _shared/sdp_expected.h -- if "
            "both consumers stopped including this fixture, the blob could "
            "stay byte-identical while nothing exercised it (T-124-12)."
        )


def test_git_is_required_not_optional():
    """Self-checking: this module's own source contains no runtime
    skip-bypass call and no skip-marker decorator, i.e. the fail-closed
    contract for a missing `git` (see _resolve_git) is self-enforcing
    rather than merely documented. The two exact patterns scanned for are
    named once, below, as the literal `startswith()` arguments -- not
    repeated a second time here, since a second copy in this docstring
    would only be prose about the same two strings."""
    this_source = Path(__file__).read_text()
    for line in this_source.splitlines():
        stripped = line.strip()
        assert not stripped.startswith("pytest.skip"), (
            f"found a skip-bypass call at: {line!r} -- git absence must "
            "FAIL this suite, never take this bypass (T-124-11)."
        )
        assert not stripped.startswith("@pytest.mark.skipif"), (
            f"found a skip-marker decorator at: {line!r} -- git "
            "absence must FAIL this suite, never skip it (T-124-11)."
        )
