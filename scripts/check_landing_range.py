#!/usr/bin/env python3
"""scripts/check_landing_range.py -- MERGE-01 Criterion-1 landing-shape gate
(Phase 124 Plan 01, D-05/D-06).

Criterion 1 requires that `git log` on the integration branch show no commit
where the portability-macro files (`include/rurp_platform_compat.h`,
`include/avr/pgmspace.h`) are present without the full py32 stack
(`platform/py32f071/`) alongside them. A **true merge** of
`agent/py32f071-toolchain` replays five commits at which the portability
files exist and `platform/py32f071/` does not -- RESEARCH measured exactly
five: `52d6c1f`, `adb133a`, `b253092`, `c0c6695`, `532997c`. A **squash**
produces zero such commits, because the whole port lands atomically in one
commit. This defect class matters because a reader trusting the *final tree*
alone cannot see it -- the offending state is only reachable by walking
history, and Phase 124's own operator tie-breaker (123-CONTEXT `<specifics>`)
says Criterion 1 must be discharged by a script, not by a human eyeballing
`git log`.

Confirmed present/absent today. On the pre-landing `v1.23-py32f071-integration`
tree, `git rev-list <fork>..HEAD` is 14 commits, none of which carries either
portability marker -- the port has not landed yet, so this gate is expected
to PASS at `scanned=14, carrying=0, violations=0` until the landing commit(s)
are added.

Rejected alternative reading, recorded deliberately. A `--first-parent`
traversal would report ZERO violations for a true merge, because the five
replayed commits are reachable only through the merge's non-first-parent
side -- `--first-parent` walks past the merge commit itself and never visits
what it merged in. That is the WEAKER reading D-06 rejects: this checker
walks the FULL `git rev-list <fork>..HEAD` set (every commit reachable from
HEAD but not from the fork, via any parent), not `--first-parent`, so a true
merge cannot hide its five violating commits behind the merge topology.

Scan scope. Every commit in `git rev-list <fork>..HEAD` (the full set). For
each commit, for each marker in PORTABILITY_MARKERS, `git cat-file -e
<commit>:<marker>` tests whether that path exists in that commit's tree; if
so, `git rev-parse -q --verify <commit>:platform/py32f071` tests whether the
py32 stack directory also exists in that same tree. A marker present without
the stack is a violation.

Never-vacuous guard. If `scanned == 0` (the fork resolves to HEAD, or HEAD is
otherwise unreachable-forward of the fork), that is a FAIL, not a pass -- a
gate that scanned nothing must never look like a gate that scanned
everything and found it clean. This is enforced by the never-vacuous check
below, independent of the ancestor check in step 3.

Output: `PASS: {scanned} commit(s) scanned in <fork>..HEAD, {carrying}
carrying a portability marker, 0 violations` on a clean range -- the PASS
line NAMES what it found, so a run that scanned nothing cannot visually
resemble a clean run. `FAIL: N violation(s) in <fork>..HEAD:` followed by up
to 20 violation lines and a `... and M more` tail on a dirty range.

Non-claim: a green run proves the history SHAPE only -- every commit in the
scanned range that carries a portability marker also carries the py32 stack
in the same tree. It proves nothing about whether the port builds, links, or
is correct; that is MERGE-02's and Phase 124's other gates' job entirely.

Exit codes:
  0 -- scanned >= 1 commit and zero violations found (gate passes)
  1 -- scanned == 0 commits (never-vacuous guard), OR scanned >= 1 commit and
       at least one violation was found
  2 -- the configured fork ref does not resolve to a commit, OR the fork is
       not an ancestor of HEAD, OR a `git` invocation itself failed
       unexpectedly -- a tool/configuration error, never silently reported
       as a pass

Anti-hollow contract: this checker's mandatory paired pytest is
`tests/test_check_landing_range.py`, exercising two synthetic git
repositories built from scratch in `tmp_path` (a squashed landing and a
replayed/true-merge landing) via the `FIRESTARTER_RANGE_ROOT` /
`FIRESTARTER_RANGE_FORK` env seams and a real subprocess -- never an
in-process import -- so a passing suite proves this script itself
discriminates a squash from a replay, not merely that the test asserts it
should.

Usage:
    python3 scripts/check_landing_range.py
    FIRESTARTER_RANGE_ROOT=/path/to/repo FIRESTARTER_RANGE_FORK=<sha> \\
        python3 scripts/check_landing_range.py
"""
import os
import subprocess
import sys
from pathlib import Path

# Resolve the repo root from this file's location so the gate behaves
# identically regardless of the caller's working directory (mirrors
# check_orphan_provisional.py:163 / check_cmake_manifest.py:110).
REPO_ROOT = Path(__file__).resolve().parent.parent

# The two files whose co-occurrence with platform/py32f071/ this gate
# enforces. Verified this session as exactly the two files
# origin/agent/portability-macros ADDS over the merge base a1953c2 (it also
# modifies include/rurp_serial_utils.h and include/rurp_shield.h, which are
# not markers because they pre-exist).
#
# NOTE: include/rurp_platform.h is py32-BRANCH content, not
# portability-branch content (research correction R-1), and is therefore
# deliberately NOT a marker here.
PORTABILITY_MARKERS = ("include/rurp_platform_compat.h", "include/avr/pgmspace.h")

# The directory whose presence in the same tree as a marker exempts that
# commit from being a violation.
PY32_KEY = "platform/py32f071"

FIRESTARTER_RANGE_ROOT = os.environ.get("FIRESTARTER_RANGE_ROOT", str(REPO_ROOT))

FIRESTARTER_RANGE_FORK = os.environ.get(
    "FIRESTARTER_RANGE_FORK", "5c9160a34b665878b05403ab014b959926feb6bf"
)

_ROOT = Path(FIRESTARTER_RANGE_ROOT)


class ScanError(Exception):
    """A fork ref that does not resolve, a non-ancestor fork, or an
    unexpected `git` failure.

    Caught only at the entry point and converted to exit 2 -- never exit 0
    and never exit 1. A misconfigured fork point must never look like a
    clean pass, because a wrong or moved fork point could make the whole
    gate trivially and silently vacuous.
    """


def _run_git(args):
    """Run a git command as list-argv subprocess.run (never a shell string),
    cwd set to the range root. Returns the completed process; does not raise on
    non-zero exit -- callers inspect returncode themselves, because a
    non-zero exit from some of these calls (e.g. --verify on a bad ref) is
    an expected, meaningful outcome rather than a tool failure.
    """
    return subprocess.run(
        ["git", *args],
        cwd=str(_ROOT),
        capture_output=True,
        text=True,
    )


def resolve_fork(fork):
    """Resolve `fork` to a commit, raising ScanError if it does not
    resolve to a real commit object."""
    result = _run_git(["rev-parse", "--verify", f"{fork}^{{commit}}"])
    if result.returncode != 0:
        raise ScanError(
            f"FIRESTARTER_RANGE_FORK={fork!r} does not resolve to a commit "
            f"in {_ROOT} (git rev-parse --verify failed): {result.stderr.strip()}"
        )
    return result.stdout.strip()


def assert_ancestor(fork, ref="HEAD"):
    """Raise ScanError unless `fork` is an ancestor of `ref`. This is the
    guard that stops the whole gate passing vacuously against a wrong or
    moved fork point -- a non-ancestor fork could make git rev-list report
    an empty or nonsensical range that would otherwise look clean."""
    result = _run_git(["merge-base", "--is-ancestor", fork, ref])
    if result.returncode != 0:
        raise ScanError(
            f"fork {fork!r} is not an ancestor of {ref!r} in {_ROOT} -- "
            "refusing to scan a range defined against a wrong or moved "
            "fork point (git merge-base --is-ancestor exit "
            f"{result.returncode}): {result.stderr.strip()}"
        )


def list_range_commits(fork, ref="HEAD"):
    """Return the FULL set of commits in `fork`..`ref` (plain git rev-list,
    walking every parent) -- see the module docstring's "Rejected
    alternative reading" section for why a first-parent-only traversal is
    the weaker reading D-06 rejects."""
    result = _run_git(["rev-list", f"{fork}..{ref}"])
    if result.returncode != 0:
        raise ScanError(
            f"git rev-list {fork}..{ref} failed in {_ROOT}: "
            f"{result.stderr.strip()}"
        )
    commits = [line for line in result.stdout.splitlines() if line.strip()]
    return commits


def path_exists_in_commit(commit, path):
    """True iff `path` exists in `commit`'s tree (git cat-file -e)."""
    result = _run_git(["cat-file", "-e", f"{commit}:{path}"])
    return result.returncode == 0


def stack_present_in_commit(commit, key=PY32_KEY):
    """True iff `key` (a directory or object) exists in `commit`'s tree
    (git rev-parse -q --verify)."""
    result = _run_git(["rev-parse", "-q", "--verify", f"{commit}:{key}"])
    return result.returncode == 0


def scan_range(fork, ref="HEAD"):
    """Walk every commit in fork..ref and return (scanned, carrying,
    violations) where violations is a list of human-readable strings.

    One violation is recorded per VIOLATING COMMIT, not per marker: a
    commit carrying both PORTABILITY_MARKERS without the py32 stack is one
    landing-shape violation, not two, so the stack check runs once per
    commit against the FIRST marker found present (deterministic, per
    PORTABILITY_MARKERS' declared order), not once per present marker.
    """
    commits = list_range_commits(fork, ref)
    scanned = len(commits)
    carrying = 0
    violations = []
    for commit in commits:
        present_markers = [m for m in PORTABILITY_MARKERS if path_exists_in_commit(commit, m)]
        if not present_markers:
            continue
        carrying += 1
        if not stack_present_in_commit(commit):
            violations.append(f"{commit} carries {present_markers[0]} without {PY32_KEY}/")
    return scanned, carrying, violations


def main():
    fork = resolve_fork(FIRESTARTER_RANGE_FORK)
    assert_ancestor(fork)
    scanned, carrying, violations = scan_range(fork)

    if scanned == 0:
        print(
            f"FAIL: 0 commits scanned in {fork}..HEAD -- a gate that scans "
            "nothing must never look like a gate that scanned everything "
            "and found it clean (never-vacuous guard). Check "
            "FIRESTARTER_RANGE_FORK and FIRESTARTER_RANGE_ROOT."
        )
        return 1

    if violations:
        print(f"FAIL: {len(violations)} violation(s) in {fork}..HEAD:")
        for v in violations[:20]:
            print(f"  {v}")
        if len(violations) > 20:
            print(f"  ... and {len(violations) - 20} more")
        return 1

    print(
        f"PASS: {scanned} commit(s) scanned in {fork}..HEAD, {carrying} "
        f"carrying a portability marker, 0 violations"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ScanError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(2)
