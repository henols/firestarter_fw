"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 125 Plan 03 -- ROADMAP Criterion 1's exit-code-producing proof that no
commit from PR #45 (feature/common-vpp-calibration, closed) is an ancestor of
this firmware integration branch, and that the two new VPP seam files are not
copies of PR #45's blobs.

Requirements: VPP-01
Decisions covered: the Criterion-1 discretion default (a scripted check
producing an exit code, never a prose assertion), plus D-09/D-10 (the API
surface decisions that make content divergence from PR #45 substantial).

Defect class this replaces. ROADMAP Criterion 1's own named mechanism
(`git log --all --grep`/SHA lookup) is wrong in two independent ways, both
measured rather than assumed:

  1. `--grep` searches commit MESSAGES, not commit objects. `git log --all
     --grep=<sha> --oneline` returns zero rows today, and would still return
     zero rows after a cherry-pick whose message was rewritten -- which is
     precisely the evasion Criterion 1 exists to catch. A gate built on it
     passes vacuously forever.
  2. Any reachability question asked against the WHOLE object graph
     (`--all`) gets the wrong answer, because
     origin/feature/common-vpp-calibration is a fetched local
     remote-tracking ref: research measured all ten of PR #45's commits
     reachable that way, and none of them reachable from HEAD. Every
     reachability question in this module therefore names HEAD as its
     scope, never `--all`.

This module asserts directly against the live tree via `git` subprocess
calls, invoked in list form without a shell -- it imports no check_*.py
machinery.

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo, and none of pytest.ini,
pyproject.toml, setup.cfg or tox.ini exist either; a recorded house-rule
pattern decision, not an omission). Stdlib and pytest only.

Coverage:
  1. test_pr45_commit_list_is_never_vacuous -- before any ancestry question
     is asked anywhere in this module: the module-level commit tuple has
     exactly ten entries, the ten entries are distinct, and each entry's
     commit object resolves locally (`git cat-file -e <sha>^{commit}` exits
     0). A non-zero exit here is a TOOL-ERROR class, never read as "not an
     ancestor" -- research recorded this as a real fresh-clone hazard: the
     objects resolve here only because
     origin/feature/common-vpp-calibration is fetched.
  2. test_no_pr45_commit_is_an_ancestor_of_head -- for each of the ten,
     `git merge-base --is-ancestor <sha> HEAD` is classified into exactly
     three buckets: exit 0 means the commit IS an ancestor (collected as a
     violation), exit 1 means clean, any other exit is a loud tool-error
     failure quoting the captured stderr. The number of commits actually
     examined is asserted to equal ten, so a loop that silently skipped
     entries cannot pass; the collected violation list is asserted empty,
     naming every offending commit on failure.
  3. test_seam_files_diverge_from_pr45_blobs -- the two new seam files'
     live worktree blob hashes (`git hash-object <path>`) are asserted NOT
     EQUAL to PR #45's corresponding recorded blob SHAs. Ancestry (coverage
     2) catches a cherry-pick; this leg catches a copy-paste that left no
     commit behind. Also records RESEARCH C-18's finding: PR #45's
     768580f adds the calibration fields to rurp_configuration_t BEFORE the
     PR's own CONFIG_VERSION bump (05f4a77), so cherry-picking 768580f alone
     would change the struct layout while the version string stayed
     literally "VER06" -- a silent schema change with no migration signal,
     strictly worse than the visible bump the record already warns about.
     That is the failure mode a well-meaning "just take the types" shortcut
     would produce, and it is why the answer is take nothing.
  4. test_git_is_required_not_optional -- combines two protections: (a) the
     fail-closed `git` resolver is exercised directly and asserted non-None;
     (b) this module's own source is scanned to assert neither pytest skip
     construct appears anywhere in it, keeping the fail-closed contract from
     being edited away. A skip that reports success at exit 0 is the
     absence-proxy failure class this milestone's Phase 123 removed.

This module's own source is scanned by coverage 4's needle search, which is
built by string concatenation rather than written verbatim precisely so this
scan does not trip on its own assertion text -- see that test's docstring for
what it searches for, not repeated here as a second literal copy.
"""

import shutil
import subprocess
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent

PR45_SHAS = (
    "04fd9b3",
    "fc0b2c7",
    "86f351a",
    "768580f",
    "05f4a77",
    "b964ee6",
    "9134f2a",
    "d285b83",
    "71278d0",
    "a47228d",
)

PR45_BLOBS = {
    "include/rurp_vpp.h": "c982173813b38ec745b59d6e02817f2504d6c6b4",
    "src/rurp_vpp.cpp": "fcbe009dffcd46139802f8779865a1d7aa331880",
}


def _resolve_git():
    """Resolve the `git` binary, fail-closed.

    Deliberately never bypassed via any decorator or runtime call that would
    mark this outcome as skipped, anywhere in this module: doing so would
    recreate the exact BASE-02/BASE-03 absence-proxy failure class this
    milestone's Phase 123 removed. If `git` cannot be resolved via
    shutil.which, this raises via a plain assert, which the test runner
    reports as a FAILURE, never a skipped outcome.
    """
    git_bin = shutil.which("git")
    assert git_bin is not None, (
        "`git` binary not found on PATH. This must FAIL the suite, never be "
        "silently skipped -- every reachability assertion in this module "
        "depends on it."
    )
    return git_bin


def _git(*args):
    """Run `git` against this repository, with an explicit -C repository
    root argument, taking the remaining arguments as a list. List argv
    only, never a shell, never a composed command string. Returns the
    CompletedProcess with both streams captured as text."""
    git_bin = _resolve_git()
    argv = [git_bin, "-C", str(_REPO_ROOT), *args]
    return subprocess.run(argv, capture_output=True, text=True)


def test_pr45_commit_list_is_never_vacuous():
    """Coverage 1 -- before any ancestry question is asked anywhere in this
    module: the commit tuple has exactly ten entries, the ten entries are
    distinct, and each entry's commit object resolves locally. A non-zero
    exit from the object-existence query here is the TOOL-ERROR class: the
    failure message names origin/feature/common-vpp-calibration as the ref
    to fetch and states explicitly that an absent object must NOT read as
    "not an ancestor". RESEARCH recorded this as a real fresh-clone hazard
    -- the objects resolve here only because that ref is fetched."""
    assert len(PR45_SHAS) == 10, (
        f"never-vacuous: expected exactly 10 PR #45 commit SHAs, got "
        f"{len(PR45_SHAS)}: {PR45_SHAS!r}"
    )
    assert len(set(PR45_SHAS)) == 10, (
        f"never-vacuous: expected all 10 PR #45 commit SHAs to be distinct, "
        f"got duplicates in {PR45_SHAS!r}"
    )
    for sha in PR45_SHAS:
        result = _git("cat-file", "-e", f"{sha}^{{commit}}")
        assert result.returncode == 0, (
            f"{sha} did not resolve as a local commit object (exit "
            f"{result.returncode}). This is a TOOL-ERROR, never a silent "
            f"'not an ancestor' pass -- fetch "
            f"origin/feature/common-vpp-calibration first, then re-run.\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def test_no_pr45_commit_is_an_ancestor_of_head():
    """Coverage 2 -- for each of the ten PR #45 commits, classify
    `git merge-base --is-ancestor <sha> HEAD`'s exit code into exactly
    three buckets: 0 means the commit IS an ancestor of HEAD (collected as
    a violation), 1 means clean, anything else is a loud tool-error failure
    quoting the captured stderr. Never a negated single call -- RESEARCH
    measured a fabricated reference producing exit 128 with a fatal
    message, which a naive negation reads as clean. The examined count is
    asserted to equal ten, so a loop that silently skipped entries cannot
    pass. A green run here means: ten commits examined, each object
    present, each not reachable from HEAD. Every reachability query in this
    module names HEAD as its scope -- never `--all`, which would find all
    ten commits reachable via the fetched
    origin/feature/common-vpp-calibration ref regardless of HEAD."""
    checked = 0
    ancestors = []
    for sha in PR45_SHAS:
        result = _git("merge-base", "--is-ancestor", sha, "HEAD")
        if result.returncode == 0:
            ancestors.append(sha)
        elif result.returncode == 1:
            pass
        else:
            raise AssertionError(
                f"git merge-base --is-ancestor {sha} HEAD failed "
                f"unexpectedly with exit {result.returncode} (expected 0 "
                f"or 1) -- treat this as a tool error, never as clean.\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )
        checked += 1
    assert checked == 10, (
        f"never-vacuous: expected exactly 10 commits examined, got "
        f"{checked} -- a loop that silently skipped entries must not pass."
    )
    assert not ancestors, (
        f"PR #45 commit(s) reachable from HEAD, which must never happen: "
        f"{ancestors}. VPP-01 requires nothing be cherry-picked from PR #45."
    )


def test_seam_files_diverge_from_pr45_blobs():
    """Coverage 3 -- the two new seam files' live worktree blob hashes are
    asserted NOT EQUAL to PR #45's corresponding recorded blob SHAs. The
    mapping is asserted to have exactly two entries and both paths are
    asserted to resolve before any comparison, so an emptied mapping or a
    typo'd path cannot pass vacuously. Ancestry (coverage 2) catches a
    cherry-pick; content divergence catches a copy-paste that left no
    commit behind.

    RESEARCH C-18's finding, cited for VPP-01: PR #45's 768580f
    ("Persist common VPP calibration in board configuration") adds the
    calibration fields to rurp_configuration_t in include/rurp_types.h
    BEFORE the PR's own CONFIG_VERSION bump (05f4a77) -- so cherry-picking
    768580f alone would change the struct layout while CONFIG_VERSION
    stayed literally "VER06", a silent schema change with no migration
    signal. That is strictly worse than the visible version bump the
    record already warns about, and it is the failure mode a well-meaning
    "just take the types" shortcut would produce. The answer is: take
    nothing."""
    assert len(PR45_BLOBS) == 2, (
        f"never-vacuous: expected exactly 2 entries in the blob-divergence "
        f"mapping, got {len(PR45_BLOBS)}: {PR45_BLOBS!r}"
    )
    for relpath, pr45_blob in PR45_BLOBS.items():
        live_path = _REPO_ROOT / relpath
        assert live_path.exists(), (
            f"expected {live_path} to exist in the live worktree -- an "
            f"absent path cannot be compared and must not pass vacuously."
        )
        result = _git("hash-object", str(live_path))
        assert result.returncode == 0, (
            f"git hash-object failed for {live_path}.\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        live_blob = result.stdout.strip()
        assert live_blob != pr45_blob, (
            f"{relpath}'s live blob hash ({live_blob}) equals PR #45's "
            f"recorded blob ({pr45_blob}) -- this file must be hand-"
            f"authored, never a copy of PR #45's content."
        )


def test_git_is_required_not_optional():
    """Coverage 4 -- combines two protections. First, resolving `git` is
    fail-closed: the resolver is called directly here and asserted non-
    None, rather than only ever being defined. Second, this module's own
    source is scanned to assert neither pytest skip construct appears
    anywhere in it, keeping the fail-closed contract from being edited
    away by a future change. A skip that reports success at exit 0 is the
    absence-proxy failure class this milestone's Phase 123 removed, and it
    is why every gate in this milestone fails rather than skips when its
    tool is missing.

    The two needle strings below are built via concatenation (not written
    verbatim) so this test's own assertion text does not trip its own
    check -- the literal substrings must appear NOWHERE in this file,
    including inside this test's failure messages."""
    git_bin = _resolve_git()
    assert git_bin is not None, "expected _resolve_git() to return a non-None path"

    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- the "
        "git-absence case must FAIL, never SKIP."
    )
    assert skipif_marker not in own_text, (
        "expected no @pytest." + skipif_marker + " decorator anywhere in "
        "this module -- the git-absence case must FAIL, never SKIP."
    )
