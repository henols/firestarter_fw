"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Convention meta-test for the checker scripts.

Every `scripts/check_<X>.py` must have a paired `tests/test_check_<X>.py` and
at least one `planted_<X>*` fixture in `tests/fixtures/`. The filesystem
convention is the only source of truth: no registry file lists the checkers,
and no allow-list grandfathers an exception. Hardcoded floors make a
zero-match or shrunken glob fail instead of passing vacuously.

SCOPE: `CHECKER_GLOB` globs `check_*.py` in `scripts/` only, non-recursively.

FLOOR = 4: `check_cmake_manifest.py`, `check_orphan_provisional.py` and
`check_erase_no_vpp.py`.

FIXTURE_FLOOR = 5: `planted_cmake_manifest_excluded_no_reason/`,
`planted_cmake_manifest_missing_source/`, `planted_erase_no_vpp_ctrl_write.cpp`
and `planted_orphan_provisional_macro/`.

Both floors are counted on the tree. The assertions are `>=`. Add a checker in
the same commit that raises both floors. Lowering a floor is correct only when
a checker is deliberately deleted. It is never the response to a red gate.

Tests:

  1. test_glob_is_non_vacuous -- at least FLOOR checkers are found.
  2. test_every_checker_has_paired_test_module -- every checker has a test
     module. All missing pairs are reported in one message.
  3. test_every_checker_has_planted_fixture -- every checker has at least one
     `planted_<X>*` entry (file or directory) in `tests/fixtures/`.
  4. test_fixture_directory_is_non_vacuous -- the `planted_*` count is
     `>= FIXTURE_FLOOR`.
  5. test_paired_test_module_names_its_checker -- each paired test module
     names its checker's filename.
  6. test_paired_test_module_asserts_nonzero_exit -- each paired test module
     contains a `returncode != 0` assertion. This is a coarse textual check.
  7. test_scope_is_firmware_only -- the glob directory is the repo `scripts/`
     directory, and no checker path contains `firestarter_app`.

Path resolution is self-contained here. The tests directory has no conftest.py.
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
    """check_erase_no_vpp.py -> 'erase_no_vpp' for fixture matching."""
    stem = checker_path.stem  # e.g. "check_erase_no_vpp"
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
    """Test 7 -- the glob directory must be this repo's `scripts/`
    directory, and no path in the glob's result set may contain
    'firestarter_app'. A future edit widening CHECKER_GLOB to reach the host
    repo fails here."""
    resolved = _SCRIPTS_DIR.resolve()
    assert resolved == (_REPO_ROOT / "scripts").resolve()
    assert (_REPO_ROOT / "platformio.ini").is_file(), (
        f"expected the firmware repo root at {_REPO_ROOT}"
    )

    checkers = _discovered_checkers()
    for checker in checkers:
        assert "firestarter_app" not in str(checker.resolve()), (
            f"CHECKER_GLOB reached into firestarter_app, which is out of "
            f"scope for this meta-test: {checker}"
        )


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
