"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 124 Plan 01 — the BASE-08 anti-hollow pairing for
scripts/check_landing_range.py.

Requirements: MERGE-01
Decisions covered: D-06

This is the MANDATORY anti-hollow pairing for the MERGE-01 landing-range
gate: a checker with no negative-fixture test is exactly this project's
v1.12 hollow-GATE-03 failure mode -- a declared-empty detector that could
never fail because nothing concrete was asserted against it. Every test
below invokes check_landing_range.py as a REAL SUBPROCESS (list-form
subprocess.run, never shell=True) against a synthetic git repository built
FROM SCRATCH in `tmp_path` via `git init` plus a small number of scripted
commits -- never an in-process import, and never an in-process env-var
patch, because FIRESTARTER_RANGE_ROOT and FIRESTARTER_RANGE_FORK bind at
import time and an in-process patch would be silently ineffective
(123-RESEARCH.md Correction C-15). This module never imports
check_landing_range.

No in-tree fixture is a real git repository (124-PATTERNS.md "No Analog
Found"), so the two shapes this checker must discriminate -- a squashed
landing (0 violations) vs. a replayed/true-merge landing (N>0 violations) --
are built here, in `tmp_path`, on every test run. The committed
`tests/fixtures/planted_landing_range_replayed_history/README.md` is a
recipe stub describing these same two shapes in prose, satisfying
`test_checker_convention.py::test_every_checker_has_planted_fixture`.

Coverage:
  1. test_squashed_landing_passes -- one commit adding both marker headers
     AND platform/py32f071/ together -> exit 0, "PASS:", "0 violations".
  2. test_replayed_landing_fires_exactly_one_violation -- two commits, the
     first adding only the marker headers, the second adding
     platform/py32f071/ -> non-zero exit, "FAIL: 1 ", the first commit's
     short SHA named in stdout.
  3. test_replayed_landing_never_names_the_completing_commit -- the named
     negative control: the SECOND (completing) commit's SHA must NEVER
     appear in the violation bucket.
  4. test_zero_commits_scanned_is_a_failure_not_a_pass -- range_fork set to
     the repo's own HEAD -> non-zero exit, stdout starts "FAIL:".
  5. test_non_ancestor_fork_exits_exactly_2 -- a second, unrelated repo's
     HEAD passed as range_fork -> exit exactly 2, "ERROR:" on stderr.
  6. test_real_tree_with_no_seam_override_passes -- both seams absent (the
     env.pop branch) -> exit 0, "PASS:" on the real
     v1.23-py32f071-integration tree; unlike the two Phase-123 `UNARMED:`
     tests this checker's sibling gates carry, this test does NOT expire at
     the landing -- a squashed landing keeps this at 0 violations too.
  7. test_checker_module_is_invoked_as_a_subprocess_not_imported -- this
     test module names check_landing_range.py but contains no
     `import check_landing_range`.

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo; a recorded house-rule
pattern decision per test_update_version.py's own comment, not an
omission). Stdlib and pytest only.
"""

import os
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_CHECKER = _REPO_ROOT / "scripts" / "check_landing_range.py"

_MARKER_1 = "include/rurp_platform_compat.h"
_MARKER_2 = "include/avr/pgmspace.h"
_PY32_KEY_FILE = "platform/py32f071/CMakeLists.txt"


def _run_git(cwd, args):
    result = subprocess.run(
        ["git", *args],
        cwd=str(cwd),
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, (
        f"fixture setup: `git {' '.join(args)}` failed in {cwd}.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    return result.stdout.strip()


def _init_repo(repo_dir):
    """git init a throwaway repo and commit a fork-point README. The README
    content and commit message embed `repo_dir`'s own (tmp_path-unique)
    name so that two independently-initialized repos can never produce a
    content-identical (and therefore hash-identical) fork commit -- without
    this, two repos built back-to-back with identical author config and
    identical README text can land on the SAME commit SHA if git's
    one-second commit-timestamp resolution also happens to coincide,
    silently defeating the non-ancestor test below."""
    repo_dir.mkdir(parents=True, exist_ok=True)
    _run_git(repo_dir, ["init", "-q"])
    _run_git(repo_dir, ["config", "user.email", "fixture@example.invalid"])
    _run_git(repo_dir, ["config", "user.name", "Fixture Bot"])
    (repo_dir / "README.md").write_text(
        f"fixture repo for check_landing_range.py ({repo_dir.name})\n"
    )
    _run_git(repo_dir, ["add", "README.md"])
    _run_git(repo_dir, ["commit", "-q", "-m", f"fork point ({repo_dir.name})"])
    fork_sha = _run_git(repo_dir, ["rev-parse", "HEAD"])
    return fork_sha


def _write(repo_dir, rel_path, content):
    p = repo_dir / rel_path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content)


def _commit_all(repo_dir, message):
    _run_git(repo_dir, ["add", "-A"])
    _run_git(repo_dir, ["commit", "-q", "-m", message])
    return _run_git(repo_dir, ["rev-parse", "HEAD"])


def _make_repo(tmp_path, shape):
    """Build a synthetic repository under tmp_path exercising one of two
    landing shapes. Returns (repo_path, fork_sha).

    shape == "squashed": ONE commit creating both marker headers AND
        platform/py32f071/CMakeLists.txt together.
    shape == "replayed": TWO commits -- the first creates only the two
        marker headers, the second adds platform/py32f071/CMakeLists.txt.
    """
    repo_dir = tmp_path / f"repo_{shape}"
    fork_sha = _init_repo(repo_dir)

    if shape == "squashed":
        _write(repo_dir, _MARKER_1, "// fixture marker 1\n")
        _write(repo_dir, _MARKER_2, "// fixture marker 2\n")
        _write(repo_dir, _PY32_KEY_FILE, "# fixture cmake stub\n")
        _commit_all(repo_dir, "squashed landing: markers + py32 stack together")
    elif shape == "replayed":
        _write(repo_dir, _MARKER_1, "// fixture marker 1\n")
        _write(repo_dir, _MARKER_2, "// fixture marker 2\n")
        _commit_all(repo_dir, "replayed landing: markers alone (the violation)")
        _write(repo_dir, _PY32_KEY_FILE, "# fixture cmake stub\n")
        _commit_all(repo_dir, "replayed landing: py32 stack completes it")
    else:
        raise ValueError(f"fixture setup: unknown shape {shape!r}")

    return repo_dir, fork_sha


def _run_checker(range_root=None, range_fork=None):
    """Invoke check_landing_range.py as a real subprocess (list argv, never
    shell=True). `range_root`/`range_fork`, when not None, set
    FIRESTARTER_RANGE_ROOT/FIRESTARTER_RANGE_FORK in the CHILD's
    environment -- when None, the env var is left absent entirely, reaching
    the "variable genuinely absent -> defaults" path (which, for
    FIRESTARTER_RANGE_ROOT, is this repo's own root; for
    FIRESTARTER_RANGE_FORK, the recorded 123-01 fork_point_firmware SHA)."""
    env = {**os.environ}
    if range_root is not None:
        env["FIRESTARTER_RANGE_ROOT"] = str(range_root)
    else:
        env.pop("FIRESTARTER_RANGE_ROOT", None)
    if range_fork is not None:
        env["FIRESTARTER_RANGE_FORK"] = str(range_fork)
    else:
        env.pop("FIRESTARTER_RANGE_FORK", None)
    return subprocess.run(
        [sys.executable, str(_CHECKER)],
        cwd=str(_REPO_ROOT),
        capture_output=True,
        text=True,
        env=env,
    )


def test_squashed_landing_passes(tmp_path):
    """Coverage 1 -- a single commit adding both marker headers and the
    py32 stack together must pass with 0 violations."""
    repo_dir, fork_sha = _make_repo(tmp_path, "squashed")
    result = _run_checker(range_root=repo_dir, range_fork=fork_sha)
    assert result.returncode == 0, (
        f"expected exit 0 on a squashed landing.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" in result.stdout, (
        f"expected 'PASS:' in output. Got:\n{result.stdout}"
    )
    assert "0 violations" in result.stdout, (
        f"expected '0 violations' in output. Got:\n{result.stdout}"
    )


def test_replayed_landing_fires_exactly_one_violation(tmp_path):
    """Coverage 2 -- a replayed (true-merge-shaped) landing must fire
    exactly one violation naming the first (marker-only) commit."""
    repo_dir, fork_sha = _make_repo(tmp_path, "replayed")
    result = _run_checker(range_root=repo_dir, range_fork=fork_sha)
    assert result.returncode != 0, (
        f"expected non-zero exit on a replayed landing.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "FAIL: 1 " in result.stdout, (
        f"expected exactly 1 violation reported. Got:\n{result.stdout}"
    )
    first_commit = _run_git(
        repo_dir, ["log", "--reverse", "--format=%H", f"{fork_sha}..HEAD"]
    ).splitlines()[0]
    assert first_commit[:7] in result.stdout, (
        f"expected the first (marker-only) commit {first_commit[:7]} named "
        f"in the FAIL output. Got:\n{result.stdout}"
    )


def test_replayed_landing_never_names_the_completing_commit(tmp_path):
    """Coverage 3 -- the named negative control: the SECOND (completing)
    commit's SHA must never appear in the violation bucket. A fixture where
    the gate could not tell the two commits apart would satisfy Coverage 2
    while proving nothing."""
    repo_dir, fork_sha = _make_repo(tmp_path, "replayed")
    result = _run_checker(range_root=repo_dir, range_fork=fork_sha)
    commits = _run_git(
        repo_dir, ["log", "--reverse", "--format=%H", f"{fork_sha}..HEAD"]
    ).splitlines()
    assert len(commits) == 2, f"fixture setup: expected 2 commits, got {commits}"
    second_commit = commits[1]
    assert second_commit[:7] not in result.stdout, (
        f"the completing commit {second_commit[:7]} must NEVER appear in "
        f"the violation output.\nstdout:\n{result.stdout}"
    )


def test_zero_commits_scanned_is_a_failure_not_a_pass(tmp_path):
    """Coverage 4 -- pointing range_fork at the repo's own HEAD (an empty
    range) must fail, never pass, per the never-vacuous guard."""
    repo_dir, fork_sha = _make_repo(tmp_path, "squashed")
    head = _run_git(repo_dir, ["rev-parse", "HEAD"])
    result = _run_checker(range_root=repo_dir, range_fork=head)
    assert result.returncode != 0, (
        f"expected non-zero exit when scanning an empty range.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert result.stdout.startswith("FAIL:"), (
        f"expected stdout to start with 'FAIL:'. Got:\n{result.stdout}"
    )


def test_non_ancestor_fork_exits_exactly_2(tmp_path):
    """Coverage 5 -- a fork SHA taken from a second, wholly unrelated
    repository is not an ancestor of the first repo's HEAD, and must exit
    exactly 2 with an ERROR: on stderr, never a silent pass or a plain
    fail."""
    repo_dir, _fork_sha = _make_repo(tmp_path, "squashed")
    other_dir = tmp_path / "unrelated_repo"
    other_fork = _init_repo(other_dir)
    result = _run_checker(range_root=repo_dir, range_fork=other_fork)
    assert result.returncode == 2, (
        f"expected exit code 2 for a non-ancestor fork.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "ERROR:" in result.stderr, (
        f"expected 'ERROR:' on stderr. Got stderr:\n{result.stderr}\n"
        f"stdout:\n{result.stdout}"
    )


def test_real_tree_with_no_seam_override_passes():
    """Coverage 6 -- both seams absent (the env.pop branch): the real
    v1.23-py32f071-integration tree, scanned against the recorded 123-01
    fork_point_firmware default, must exit 0 with a PASS: line. Unlike the
    two Phase-123 `UNARMED:` tests (which assert an absence that expires
    the moment Phase 124 lands the port), this test asserts a POSITIVE scan
    result that stays true THROUGH the landing: a squashed landing keeps
    this range at 0 violations too, so this test does not expire."""
    result = _run_checker(range_root=None, range_fork=None)
    assert result.returncode == 0, (
        f"expected exit 0 on the real tree with no seam override.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" in result.stdout, (
        f"expected 'PASS:' in output. Got:\n{result.stdout}"
    )


def test_checker_module_is_invoked_as_a_subprocess_not_imported():
    """Coverage 7 -- satisfies test_checker_convention.py's test 5 (paired
    module must name its checker) while proving this module never imports
    check_landing_range -- every exit-code assertion above goes through a
    real subprocess."""
    this_source = Path(__file__).read_text()
    assert "check_landing_range.py" in this_source, (
        "expected this test module's own source to name "
        "check_landing_range.py"
    )
    forbidden = "import check_landing_range"
    lines_with_forbidden = [
        line
        for line in this_source.splitlines()
        if line.strip().startswith(forbidden) or line.strip() == forbidden
    ]
    assert not lines_with_forbidden, (
        "this test module must never import check_landing_range directly "
        f"-- every assertion must go through a real subprocess. Found: "
        f"{lines_with_forbidden}"
    )
