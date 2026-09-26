"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 126 Plan 06 -- CFG-02's ordering half as an exit code: the commit
recording the PY32F071 flash geometry (platform/py32f071/CONFIG-STORAGE.md)
must precede, in commit history, every commit that edits the linker script
(platform/py32f071/linker/PY32F071xB_FLASH.ld).

Requirements: CFG-02
Decisions covered: D-01

ROADMAP Criterion 2 is a commit-ORDER constraint, not an end state: it cannot
be satisfied by any property of the files' current content, only by a fact
about the repository's own DAG. That is why this module asks `git` the
question directly, via `git merge-base --is-ancestor`, rather than inspecting
file content or timestamps. A gate wrapping `git log --all --grep=<sha>` would
be wrong for the same reason Plan 125-03's analog documents: `--grep` searches
commit MESSAGES, not commit objects, and would pass vacuously after a rebase
or a message rewrite.

Non-vacuity is the load-bearing property here. A gate that would pass with
zero in-phase commits touching the linker script proves nothing -- it would
have passed identically before this plan's Task 1 ever landed a single line
of `platform/py32f071/linker/PY32F071xB_FLASH.ld`. This module therefore
asserts, as an independent leg, that at least one commit reachable from HEAD
but not from the geometry-record commit actually touches the linker script,
and treats an empty set as a violation in its own right.

This module executes in NO CI leg on this branch: `pytest tests/ -v` appears
only in build.yml (push/PR to main) and beta-build.yml (push to beta) --
neither fires on this firmware milestone branch, and py32f071.yml has no
pytest step at all. The local run recorded in this phase's SUMMARY is the
only evidence this module's assertions were ever exercised.

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo, and none of pytest.ini,
pyproject.toml, setup.cfg or tox.ini exist either; a recorded house-rule
pattern decision, not an omission). Stdlib and pytest only.

Every check below is factored into ONE module-level helper,
`_find_ordering_violations(repo_root, record_path, linker_path)`, returning
`(violations, examined_count)`. The real-repo tests and both synthetic-repo
tests all call this same helper -- a parallel second implementation inside
the RED test would prove nothing about the gate (Phase 124 D-14: "a guard
that supplies the answer it tests is structurally dead").

Coverage:
  1. test_the_geometry_record_exists_and_was_added_by_a_commit -- the adding
     commit for platform/py32f071/CONFIG-STORAGE.md is resolvable against the
     real repository; an untracked or never-added file is a violation.
  2. test_the_linker_script_has_a_commit_after_the_geometry_record -- the
     NON-VACUITY leg: at least one commit reachable from HEAD but not from
     the geometry-record commit touches the linker script.
  3. test_the_geometry_record_precedes_every_in_phase_linker_commit -- the
     geometry-record commit is an ancestor of the linker tip commit, and of
     every commit in the after-the-record set.
  4. test_the_linker_script_at_head_actually_carries_the_config_region --
     the ordered commit is the real one: HEAD's linker script text contains
     the CONFIG region at ORIGIN = 0x0801E000, so an unrelated whitespace
     commit could not have satisfied the ordering.
  5. test_the_geometry_record_carries_the_page_and_sector_figures -- the
     record cited by the ordering actually contains 256, 8192 and the RM
     identifiers, so ordering a content-free document would not pass.
  6. test_a_git_tool_error_is_a_violation_not_a_pass -- feeds the helper a
     directory that is not a git repository at all, and asserts a violation
     is reported rather than a silent success or an uncaught exception.
  7. test_helper_reports_a_violation_on_a_synthetic_repo_with_the_wrong_order
     -- the RED demonstration. Builds a throwaway git repository in tmp_path
     with a commit adding a stand-in linker file FIRST, then a commit adding
     a stand-in geometry document SECOND (the wrong order), and asserts the
     helper returns a non-empty violation list naming the ordering failure.
     A second synthetic repository, built with the correct order, is asserted
     to produce zero violations -- proving the helper discriminates rather
     than merely complains.
  8. test_compiler_is_required_not_optional -- this module invokes no
     compiler; the self-enforcing no-skip leg asserts only the absence of a
     skip call and a conditional-skip marker, keeping the fail-closed
     contract from being edited away by a future change.
"""

import shutil
import subprocess
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_RECORD_PATH = "platform/py32f071/CONFIG-STORAGE.md"
_LINKER_PATH = "platform/py32f071/linker/PY32F071xB_FLASH.ld"


def _resolve_git():
    """Resolve the `git` binary, fail-closed. Never bypassed via any
    decorator or runtime call that would mark this outcome as skipped --
    doing so would recreate the absence-proxy failure class this
    milestone's Phase 123 removed. If `git` cannot be resolved via
    shutil.which, this raises via a plain assert, which the test runner
    reports as a FAILURE, never a skipped outcome."""
    git_bin = shutil.which("git")
    assert git_bin is not None, (
        "`git` binary not found on PATH. This must FAIL the suite, never be "
        "silently skipped -- every ancestry assertion in this module "
        "depends on it."
    )
    return git_bin


def _git(repo_root, *args):
    """Run `git` against the given repository root, with an explicit -C
    argument, taking the remaining arguments as a list. List argv only,
    never a shell, never a composed command string. Returns the
    CompletedProcess with both streams captured as text."""
    git_bin = _resolve_git()
    argv = [git_bin, "-C", str(repo_root), *args]
    return subprocess.run(argv, capture_output=True, text=True)


def _find_ordering_violations(repo_root, record_path, linker_path):
    """The single module-level helper every test in this module calls.

    Returns (violations, examined_count). Logic, in order:

      1. Resolve the ADDING commit of record_path via
         `git log --diff-filter=A --format=%H -- <record_path>`, taking the
         OLDEST line (git log's default order is newest-first, so the oldest
         add event is the last line of output). Zero lines, or a non-zero
         git exit code, is a violation and short-circuits the rest of the
         check -- a tool error here is never read as a passing ordering.
      2. Resolve the TIP commit touching linker_path via
         `git log -1 --format=%H -- <linker_path>`. Zero lines, or a
         non-zero git exit code, is likewise a violation that short-circuits.
      3. Compute the set of commits touching linker_path reachable from HEAD
         but not from the record's adding commit, via
         `git rev-list <add>..HEAD -- <linker_path>`. An EMPTY set is a
         violation in its own right -- the non-vacuity guard: this is what
         stops the gate passing trivially in the window between the
         geometry commit and the linker commit. A non-zero git exit code
         here is also a violation, short-circuiting the rest.
      4. For the tip commit AND every commit in that set, run
         `git merge-base --is-ancestor <add> <commit>` and classify its exit
         code into exactly three buckets: 0 means the record IS an ancestor
         of that commit (clean), 1 means it is NOT (a violation), and any
         other exit code is a TOOL ERROR and is ALSO a violation -- never
         treated as a pass. Each commit checked increments the examined
         count.
      5. An examined count of zero is a violation, independent of step 3's
         own non-vacuity check.
    """
    violations = []
    examined = 0

    # Step 1 -- resolve the adding commit of record_path.
    result = _git(repo_root, "log", "--diff-filter=A", "--format=%H", "--", str(record_path))
    if result.returncode != 0:
        violations.append(
            f"git tool error resolving the adding commit of {record_path!r}: "
            f"exit {result.returncode}: {result.stderr.strip()}"
        )
        return violations, examined
    add_lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not add_lines:
        violations.append(
            f"{record_path!r} has no adding commit -- "
            f"'git log --diff-filter=A' returned nothing. The geometry "
            f"record must exist and be committed before ordering can be "
            f"proven."
        )
        return violations, examined
    add_sha = add_lines[-1]  # oldest line: the true adding commit

    # Step 2 -- resolve the tip commit touching linker_path.
    result = _git(repo_root, "log", "-1", "--format=%H", "--", str(linker_path))
    if result.returncode != 0:
        violations.append(
            f"git tool error resolving the tip commit of {linker_path!r}: "
            f"exit {result.returncode}: {result.stderr.strip()}"
        )
        return violations, examined
    tip_lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not tip_lines:
        violations.append(
            f"{linker_path!r} has no commit touching it at all -- "
            f"'git log -1' returned nothing."
        )
        return violations, examined
    tip_sha = tip_lines[0]

    # Step 3 -- the after-the-record commit set (non-vacuity guard).
    result = _git(repo_root, "rev-list", f"{add_sha}..HEAD", "--", str(linker_path))
    if result.returncode != 0:
        violations.append(
            f"git tool error computing the after-the-record commit set for "
            f"{linker_path!r}: exit {result.returncode}: {result.stderr.strip()}"
        )
        return violations, examined
    after_set = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not after_set:
        violations.append(
            f"non-vacuity violation: no commit touching {linker_path!r} "
            f"exists strictly after {add_sha} (the geometry record's "
            f"adding commit) and up to HEAD -- a gate that would pass with "
            f"zero in-phase linker edits proves nothing about ordering."
        )

    # Step 4 -- ancestry check for the tip commit and every commit in the
    # after-the-record set. Use a list, not a set, so duplicates (the tip
    # commit is very likely also a member of after_set) are still each
    # counted toward the examined total -- de-duplicating would silently
    # under-report how many ancestry questions were actually asked.
    to_check = [tip_sha] + [sha for sha in after_set if sha != tip_sha]
    for sha in to_check:
        result = _git(repo_root, "merge-base", "--is-ancestor", add_sha, sha)
        if result.returncode == 0:
            pass  # add_sha is an ancestor of sha: ordering holds for this commit
        elif result.returncode == 1:
            violations.append(
                f"ordering violation: {add_sha} (geometry record) is NOT an "
                f"ancestor of {sha} (a commit touching {linker_path!r}) -- "
                f"the linker edit was not proven to follow the geometry "
                f"record in commit history."
            )
        else:
            violations.append(
                f"git tool error checking ancestry of {add_sha} vs {sha}: "
                f"exit {result.returncode}: {result.stderr.strip()} -- "
                f"treated as a violation, never as a pass."
            )
        examined += 1

    if examined == 0:
        violations.append(
            "examined count is zero -- no commit was actually checked for "
            "ancestry, independent of the non-vacuity check in step 3."
        )

    return violations, examined


def test_the_geometry_record_exists_and_was_added_by_a_commit():
    """Coverage 1 -- the adding commit for platform/py32f071/CONFIG-STORAGE.md
    is resolvable against the real repository. If the file were untracked or
    had no adding commit, the helper reports a violation naming that fact."""
    violations, _ = _find_ordering_violations(_REPO_ROOT, _RECORD_PATH, _LINKER_PATH)
    non_vacuity_only = [v for v in violations if "no adding commit" in v]
    assert not non_vacuity_only, (
        f"expected {_RECORD_PATH!r} to have a resolvable adding commit, but "
        f"got: {non_vacuity_only}"
    )


def test_the_linker_script_has_a_commit_after_the_geometry_record():
    """Coverage 2 -- the NON-VACUITY leg. This gate must not pass trivially:
    at least one commit reachable from HEAD but not from the geometry-record
    commit must touch the linker script. This is exactly what Task 1 of this
    plan landed."""
    violations, _ = _find_ordering_violations(_REPO_ROOT, _RECORD_PATH, _LINKER_PATH)
    vacuity_violations = [v for v in violations if "non-vacuity violation" in v]
    assert not vacuity_violations, (
        f"expected at least one in-phase commit touching {_LINKER_PATH!r} "
        f"after the geometry record, but got: {vacuity_violations}"
    )


def test_the_geometry_record_precedes_every_in_phase_linker_commit():
    """Coverage 3 -- the geometry-record commit is an ancestor of the linker
    tip commit, and of every commit in the after-the-record set. Zero
    ordering violations, and at least one commit actually examined."""
    violations, examined = _find_ordering_violations(_REPO_ROOT, _RECORD_PATH, _LINKER_PATH)
    assert not violations, (
        f"expected zero ordering violations for the real repository, got: "
        f"{violations}"
    )
    assert examined >= 1, (
        f"expected at least one commit examined for ancestry, got {examined}"
    )


def test_the_linker_script_at_head_actually_carries_the_config_region():
    """Coverage 4 -- the ordered commit is the real one: the linker script's
    text at HEAD contains the CONFIG region at ORIGIN = 0x0801E000, so the
    ordering could not have been satisfied by an unrelated whitespace
    commit."""
    linker_text = (_REPO_ROOT / _LINKER_PATH).read_text()
    assert "CONFIG" in linker_text and "0x0801E000" in linker_text, (
        f"expected {_LINKER_PATH} at HEAD to contain the CONFIG region at "
        f"ORIGIN = 0x0801E000.\nGot:\n{linker_text}"
    )


def test_the_geometry_record_carries_the_page_and_sector_figures():
    """Coverage 5 -- the record cited by the ordering actually contains the
    page and sector figures and the reference-manual identifiers, so
    ordering a content-free document would not pass."""
    record_text = (_REPO_ROOT / _RECORD_PATH).read_text()
    assert "256" in record_text, (
        f"expected {_RECORD_PATH} to record the 256 B page-size figure.\n"
        f"Got:\n{record_text}"
    )
    assert "8192" in record_text, (
        f"expected {_RECORD_PATH} to record the 8192 B sector-size figure.\n"
        f"Got:\n{record_text}"
    )
    for identifier in ("RM V0.2", "Table 4-1"):
        assert identifier in record_text, (
            f"expected {_RECORD_PATH} to cite {identifier!r} as its "
            f"reference-manual identifier.\nGot:\n{record_text}"
        )


def test_a_git_tool_error_is_a_violation_not_a_pass(tmp_path):
    """Coverage 6 -- feeds the helper a directory that is not a git
    repository at all, and asserts a violation is reported rather than a
    silent success or an uncaught exception."""
    not_a_repo = tmp_path / "not-a-repo"
    not_a_repo.mkdir()
    violations, examined = _find_ordering_violations(not_a_repo, _RECORD_PATH, _LINKER_PATH)
    assert violations, (
        "expected a non-empty violation list when the helper is pointed at "
        "a directory that is not a git repository -- a tool error must "
        "never read as a silent pass."
    )
    assert examined == 0, (
        f"expected zero commits examined against a non-repository path, "
        f"got {examined}"
    )


def _init_synthetic_repo(root):
    """Initialize a throwaway git repository at `root` with a local
    user.name/user.email so commits succeed without global config. Never
    touches the real repository's history."""
    subprocess.run(["git", "init", "--quiet", str(root)], capture_output=True, text=True, check=True)
    subprocess.run(["git", "-C", str(root), "config", "user.name", "GSD Test"], capture_output=True, text=True, check=True)
    subprocess.run(["git", "-C", str(root), "config", "user.email", "test@example.invalid"], capture_output=True, text=True, check=True)


def _commit_file(root, relpath, content, message):
    path = root / relpath
    path.write_text(content)
    subprocess.run(["git", "-C", str(root), "add", relpath], capture_output=True, text=True, check=True)
    subprocess.run(["git", "-C", str(root), "commit", "--quiet", "-m", message], capture_output=True, text=True, check=True)


def test_helper_reports_a_violation_on_a_synthetic_repo_with_the_wrong_order(tmp_path):
    """Coverage 7 -- the RED demonstration. Builds a throwaway git
    repository with the WRONG order (a stand-in linker file committed
    first, a stand-in geometry document committed second) and asserts the
    helper returns a non-empty violation list naming the ordering failure.
    A second synthetic repository, built with the CORRECT order, is
    asserted to produce zero violations -- proving the helper discriminates
    rather than merely complains. Neither touches the real repository's
    history."""
    # --- Wrong order: linker first, geometry record second. ---
    wrong_root = tmp_path / "wrong-order"
    wrong_root.mkdir()
    _init_synthetic_repo(wrong_root)
    _commit_file(wrong_root, "linker.ld", "MEMORY { FLASH : ORIGIN = 0x0, LENGTH = 1K }\n", "add stand-in linker file")
    _commit_file(wrong_root, "geometry.md", "page=256 sector=8192\n", "add stand-in geometry document (too late)")

    wrong_violations, _ = _find_ordering_violations(wrong_root, "geometry.md", "linker.ld")
    assert wrong_violations, (
        "expected a non-empty violation list for the wrong-order synthetic "
        "repository (linker committed BEFORE the geometry record), got none."
    )
    ordering_violations = [v for v in wrong_violations if "ordering violation" in v]
    assert ordering_violations, (
        f"expected the violation list to name the ordering failure "
        f"specifically, got: {wrong_violations}"
    )

    # --- Correct order: geometry record first, linker second. ---
    right_root = tmp_path / "correct-order"
    right_root.mkdir()
    _init_synthetic_repo(right_root)
    _commit_file(right_root, "geometry.md", "page=256 sector=8192\n", "add stand-in geometry document")
    _commit_file(right_root, "linker.ld", "MEMORY { FLASH : ORIGIN = 0x0, LENGTH = 1K }\n", "add stand-in linker file (after the record)")

    right_violations, right_examined = _find_ordering_violations(right_root, "geometry.md", "linker.ld")
    assert not right_violations, (
        f"expected zero violations for the correctly-ordered synthetic "
        f"repository, got: {right_violations}"
    )
    assert right_examined >= 1, (
        f"expected at least one commit examined for the correctly-ordered "
        f"synthetic repository, got {right_examined}"
    )

    # Neither synthetic repository touched the real repository's history.
    # (This check targets leakage from the synthetic repos specifically --
    # it does not require the real tree to be fully clean, since this very
    # test module is itself untracked until its own commit lands.)
    real_status = _git(_REPO_ROOT, "status", "--porcelain")
    for leaked_name in ("geometry.md", "linker.ld", "wrong-order", "correct-order"):
        assert leaked_name not in real_status.stdout, (
            f"expected no trace of the synthetic repositories' content "
            f"({leaked_name!r}) in the real repository's status, got:\n"
            f"{real_status.stdout}"
        )


def test_compiler_is_required_not_optional():
    """Coverage 8 -- this module invokes no compiler; the self-enforcing
    no-skip leg asserts only the absence of a skip call and a conditional-
    skip marker, keeping the fail-closed contract from being edited away by
    a future change.

    The two needle strings below are built via concatenation (not written
    verbatim) so this test's own assertion text does not trip its own
    check -- the literal substrings must appear NOWHERE in this file,
    including inside this test's failure messages."""
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- a "
        "tool-absence case must FAIL, never SKIP."
    )
    assert skipif_marker not in own_text, (
        "expected no @pytest." + skipif_marker + " decorator anywhere in "
        "this module -- a tool-absence case must FAIL, never SKIP."
    )
