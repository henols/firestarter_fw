"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 123 Plan 06 — BASE-08's convention-derived meta-test.

Requirements: BASE-08
Decisions covered: D-06, D-08

BASE-08 requires that every checker introduced in v1.23 ships with a
committed planted-violation fixture and a pytest proving the checker's
non-zero exit against it. D-08 makes the filesystem convention the single
source of truth for that requirement -- no registry file lists the
checkers, and no allow-list grandfathers an exception -- with a hardcoded
floor so a zero-match (or shrunken) glob FAILS rather than passing
vacuously.

SCOPE (the load-bearing decision of this module): `CHECKER_GLOB` globs
`check_*.py` in `firestarter/scripts/` ONLY, non-recursively -- a single
directory level, never a recursive descent. It never reaches into
`firestarter_app/tools/`.

This scope is deliberate: the glob covers `firestarter/scripts/` only, and
never reaches `firestarter_app/tools/`. The host repo's own checker family
was retired wholesale in v1.37 Phase 188, so there is nothing left there for
a repo-wide version of this meta-test to measure.

FLOOR = 4 -- the number of `check_*.py` files actually shipped into
`firestarter/scripts/` and still present: `check_cmake_manifest.py`,
`check_orphan_provisional.py`, `check_landing_range.py` and
`check_erase_no_vpp.py`. The size-baseline, build-warnings, release-assets
and no-heap/64-bit checkers that this list once also named were retired on
2026-09-13 as never-run gates, taking the floor from 8 to 4 (as the rule
below requires, in reverse).

path). FIXTURE_FLOOR = 5 -- the number of `planted_*` entries actually
present in `firestarter/tests/fixtures/` at this commit:
`planted_cmake_manifest_excluded_no_reason/`,
`planted_cmake_manifest_missing_source/`,
`planted_erase_no_vpp_ctrl_write.cpp`,
`planted_landing_range_replayed_history/` and
`planted_orphan_provisional_macro/`. The release-assets, size-baseline,
build-warnings and no-heap/64-bit fixtures this list once also named went
with their checkers on 2026-09-13.

**Both floors are counted on the tree, never transcribed from a plan's
prose.** `ls scripts/check_*.py | wc -l` gives **4** and
`ls -d tests/fixtures/planted_* | wc -l` gives **5** at this commit, which
is what `FLOOR` and `FIXTURE_FLOOR` below carry. The assertions are `>=`.

A floor moving DOWN is not the normal case and is not a way to silence a
red gate -- it is correct only when checkers are deliberately retired, as
on 2026-09-13, when four never-run gates were deleted and the floors went
8 -> 4 and 31 -> 5 in that same commit. A later phase
that adds a firmware checker under `firestarter/scripts/` raises both
floors deliberately in the SAME commit that adds the checker; lowering a
floor is never the correct response to a red gate here -- it means a
checker, test, or fixture went missing.

Tests (one named function each):

  1. test_glob_is_non_vacuous -- at least FLOOR checkers are found via
     CHECKER_GLOB, asserted with the observed count and the floor in the
     failure message.
  2. test_every_checker_has_paired_test_module -- for each
     `scripts/check_<X>.py`, `tests/test_check_<X>.py` must exist. Every
     missing pair is collected and reported in ONE assertion message
     rather than failing on the first found.
  3. test_every_checker_has_planted_fixture -- for each `check_<X>.py`, at
     least one entry in `tests/fixtures/` must match `planted_<X>*`,
     counting directories as well as files (two of this phase's fixtures,
     `planted_cmake_manifest_missing_source` and
     `planted_orphan_provisional_macro`, are trees, not plain files).
  4. test_fixture_directory_is_non_vacuous -- the total `planted_*` count
     in `tests/fixtures/` is `>= FIXTURE_FLOOR`.
  5. test_paired_test_module_names_its_checker -- each paired test
     module's source text must contain its checker's exact filename. A
     pairing that exists by name alone but tests something else entirely
     would satisfy tests 2-4 while proving nothing -- the hollow shape
     this whole requirement targets.
  6. test_paired_test_module_asserts_nonzero_exit -- each paired test
     module's source text must contain a `returncode != 0` assertion
     somewhere. This is deliberately a coarse textual check, not a
     semantic proof -- the semantic proof that the checker actually fails
     on its planted violation lives in each module's own tests. This test
     only guards the convention that SOME non-zero-exit assertion exists
     in the file at all.
  7. test_scope_is_firmware_only -- the resolved glob directory's path
     ends in `firestarter/scripts`, and no path in the glob's result set
     contains the substring `firestarter_app`.

Self-contained path resolution below -- NOT in conftest.py
(`firestarter/tests/` has no conftest.py anywhere in the repo; a recorded
house-rule pattern, per test_update_version.py's own comment, not an
omission). Stdlib and pytest only.
"""

from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_SCRIPTS_DIR = _REPO_ROOT / "scripts"
_TESTS_DIR = _HERE
_FIXTURES_DIR = _HERE / "fixtures"

# Non-recursive glob, scoped to firestarter/scripts/ only -- never a
# recursive descent, never firestarter_app/tools/. See module docstring
# "SCOPE" section.
CHECKER_GLOB = "check_*.py"

# Hardcoded floors -- see module docstring for what each counts and why a
# future checker addition must raise these in the same commit.
FLOOR = 4
FIXTURE_FLOOR = 5

def _discovered_checkers():
    """Single-level, non-recursive glob of check_*.py in
    firestarter/scripts/ only."""
    return sorted(_SCRIPTS_DIR.glob(CHECKER_GLOB))


def _expected_test_module_for(checker_path):
    """check_<X>.py -> test_check_<X>.py, per the project convention."""
    return _TESTS_DIR / f"test_{checker_path.name}"


def _stem_after_check_prefix(checker_path):
    """check_landing_range.py -> 'landing_range' for fixture matching."""
    stem = checker_path.stem  # e.g. "check_landing_range"
    prefix = "check_"
    if stem.startswith(prefix):
        return stem[len(prefix) :]
    return stem


def _planted_fixtures_for(checker_path):
    """Every tests/fixtures/ entry (file or directory) matching
    planted_<stem>* for this checker's stem."""
    suffix = _stem_after_check_prefix(checker_path)
    return sorted(_FIXTURES_DIR.glob(f"planted_{suffix}*"))


def test_glob_is_non_vacuous():
    """Test 1 -- the glob must find at least FLOOR checkers. A zero-match
    or shrunken glob (renamed directory, moved files, typo'd pattern) must
    FAIL here rather than let every downstream test pass vacuously over an
    empty set."""
    checkers = _discovered_checkers()
    assert len(checkers) >= FLOOR, (
        f"expected at least FLOOR={FLOOR} checkers discovered via "
        f"CHECKER_GLOB={CHECKER_GLOB!r} under {_SCRIPTS_DIR}, got "
        f"{len(checkers)}: {[c.name for c in checkers]}"
    )


def test_every_checker_has_paired_test_module():
    """Test 2 -- every scripts/check_<X>.py must have a
    tests/test_check_<X>.py. Every missing pair is collected and reported
    in one assertion message, not failed on the first."""
    checkers = _discovered_checkers()
    assert checkers, "fixture setup: no checkers discovered at all"

    missing = []
    for checker in checkers:
        expected = _expected_test_module_for(checker)
        if not expected.is_file():
            missing.append(f"{checker.name} -> expected {expected.name} (not found)")

    assert not missing, (
        "the following checkers have no paired test module by the "
        "check_<X>.py -> test_check_<X>.py convention:\n" + "\n".join(missing)
    )


def test_every_checker_has_planted_fixture():
    """Test 3 -- every checker must have at least one planted_<stem>*
    fixture in tests/fixtures/, counting directories as well as files."""
    checkers = _discovered_checkers()
    assert checkers, "fixture setup: no checkers discovered at all"

    missing = []
    for checker in checkers:
        fixtures = _planted_fixtures_for(checker)
        if not fixtures:
            suffix = _stem_after_check_prefix(checker)
            missing.append(
                f"{checker.name} -> expected at least one "
                f"planted_{suffix}* entry under {_FIXTURES_DIR} (none found)"
            )

    assert not missing, (
        "the following checkers have no planted fixture:\n" + "\n".join(missing)
    )


def test_fixture_directory_is_non_vacuous():
    """Test 4 -- the total planted_* count across tests/fixtures/ (files
    and directories) must be >= FIXTURE_FLOOR."""
    planted = sorted(_FIXTURES_DIR.glob("planted_*"))
    assert len(planted) >= FIXTURE_FLOOR, (
        f"expected at least FIXTURE_FLOOR={FIXTURE_FLOOR} planted_* "
        f"entries under {_FIXTURES_DIR}, got {len(planted)}: "
        f"{[p.name for p in planted]}"
    )


def test_paired_test_module_names_its_checker():
    """Test 5 -- each paired test module's source text must name its
    checker's exact filename. A pairing that exists by filename convention
    alone but never actually invokes (or even mentions) its checker would
    satisfy tests 2-4 while proving nothing -- exactly the hollow shape
    this whole requirement exists to catch."""
    checkers = _discovered_checkers()
    assert checkers, "fixture setup: no checkers discovered at all"

    violations = []
    for checker in checkers:
        test_module = _expected_test_module_for(checker)
        if not test_module.is_file():
            # Reported by test_every_checker_has_paired_test_module;
            # skip here to avoid a redundant, confusing failure message.
            continue
        text = test_module.read_text()
        if checker.name not in text:
            violations.append(
                f"{test_module.name} does not mention {checker.name} anywhere in its source"
            )

    assert not violations, (
        "the following paired test modules never name their checker's "
        "filename:\n" + "\n".join(violations)
    )


def test_paired_test_module_asserts_nonzero_exit():
    """Test 6 -- each paired test module's source text must contain a
    `returncode != 0` assertion somewhere. Deliberately coarse: this is a
    convention guard proving SOME non-zero-exit assertion exists in the
    file, not a semantic proof that the checker fails correctly on its
    planted violation -- the semantic proof is each module's own
    planted-fixture test(s)."""
    checkers = _discovered_checkers()
    assert checkers, "fixture setup: no checkers discovered at all"

    violations = []
    for checker in checkers:
        test_module = _expected_test_module_for(checker)
        if not test_module.is_file():
            continue
        text = test_module.read_text()
        if "returncode != 0" not in text:
            violations.append(
                f"{test_module.name} contains no 'returncode != 0' assertion"
            )

    assert not violations, (
        "the following paired test modules never assert a non-zero exit "
        "code:\n" + "\n".join(violations)
    )


def test_scope_is_firmware_only():
    """Test 7 -- the resolved glob directory must end in
    firestarter/scripts, and no path in the glob's result set may contain
    'firestarter_app'. This pins the SCOPE decision structurally: a future
    edit widening CHECKER_GLOB to reach the host repo would fail here
    even before it changed FLOOR-related behavior."""
    resolved = _SCRIPTS_DIR.resolve()
    parts = resolved.parts[-2:]
    assert parts == ("firestarter", "scripts"), (
        f"expected the resolved scripts directory to end in "
        f"('firestarter', 'scripts'), got {parts} (full path: {resolved})"
    )

    checkers = _discovered_checkers()
    for checker in checkers:
        assert "firestarter_app" not in str(checker.resolve()), (
            f"CHECKER_GLOB reached into firestarter_app, which is out of "
            f"scope for this meta-test: {checker}"
        )


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
