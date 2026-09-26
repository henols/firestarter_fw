"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 123 Plan 04 — the BASE-08 anti-hollow pairing for
scripts/check_cmake_manifest.py.

Requirements: BASE-04, BASE-08
Decisions covered: D-06, D-07

This is the MANDATORY anti-hollow pairing for the BASE-04 CMake source-list
drift gate: a checker with no negative-fixture test is exactly this
project's v1.12 hollow-GATE-03 failure mode -- a declared-empty detector
that could never fail because nothing concrete was asserted against it.
Every test below invokes check_cmake_manifest.py as a REAL SUBPROCESS
(list-form subprocess.run, never shell=True) against one of the four
committed fixture trees under tests/fixtures/ via the
FIRESTARTER_MANIFEST_ROOT env seam set in the CHILD's environment -- never
an in-process import, and never an in-process env-var patch, because ARMED and
FIRESTARTER_MANIFEST_ROOT bind at import time and an in-process patch would
be silently ineffective (123-RESEARCH.md Correction C-15). This module
never imports check_cmake_manifest.

Coverage:
  1. test_armed_and_passing_on_the_real_tree -- no seam override: Phase 124
     landed platform/py32f071/ and repaired the flash_type_3/4.cpp rename
     damage (124-05), so this pins the ARMED, PASSING state going forward.
     A regression to the UNARMED line here would mean platform/py32f071/
     had disappeared and must fail this test.
  2. UNARMED on clean_unarmed_tree/ through the seam -- proves the arming
     decision follows the supplied root, not the process cwd.
  3. Mismatched path fails with exactly one violation -- the SDK-exempt
     entries must never be counted (123-RESEARCH.md Pitfall 6).
  4. The SDK list is exempt, stated positively in the output.
  5. Unreasoned PY32_EXCLUDED entry fails, naming the entry and the missing
     reason.
  6. Reasoned omission passes and is named on the PASS: line (ROADMAP
     criterion 3's second half).
  7. Armed but manifest missing is a hard failure, NOT a reversion to
     UNARMED -- the single most important test in this module (D-07: a
     rename inside the port cannot disarm the gate).
  8. Unknown source list is exit 2 (the literal code).

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo; a recorded house-rule
pattern decision per test_update_version.py's own comment, not an
omission). Stdlib and pytest only.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_CHECKER = _REPO_ROOT / "scripts" / "check_cmake_manifest.py"
_FIXTURES = _HERE / "fixtures"

_MISSING_SOURCE = _FIXTURES / "planted_cmake_manifest_missing_source"
_EXCLUDED_NO_REASON = _FIXTURES / "planted_cmake_manifest_excluded_no_reason"
_CLEAN_EXCLUDED = _FIXTURES / "clean_cmake_manifest_excluded"
_CLEAN_UNARMED = _FIXTURES / "clean_unarmed_tree"


def _run_checker(manifest_root=None):
    """Invoke check_cmake_manifest.py as a real subprocess (list argv, never
    shell=True). `manifest_root`, when not None, sets
    FIRESTARTER_MANIFEST_ROOT in the CHILD's environment to that exact
    path -- when None, the env var is left absent entirely, reaching the
    "variable genuinely absent -> defaults to this repo's own root" path
    (which is the real, still-UNARMED, tree today)."""
    env = {**os.environ}
    if manifest_root is not None:
        env["FIRESTARTER_MANIFEST_ROOT"] = str(manifest_root)
    else:
        env.pop("FIRESTARTER_MANIFEST_ROOT", None)
    return subprocess.run(
        [sys.executable, str(_CHECKER)],
        cwd=str(_REPO_ROOT),
        capture_output=True,
        text=True,
        env=env,
    )


def test_armed_and_passing_on_the_real_tree():
    """Coverage 1 -- no FIRESTARTER_MANIFEST_ROOT override: Phase 124 landed
    platform/py32f071/ and repaired the flash_type_3/4.cpp rename damage
    (124-05), so the gate is now ARMED and must exit 0 with a PASS: line
    naming platform/py32f071. A fixed, armed gate never reprints the
    UNARMED line on its own -- a regression back to UNARMED here would mean
    platform/py32f071/ had disappeared from the tree entirely, and this
    test must fail that."""
    result = _run_checker(manifest_root=None)
    assert result.returncode == 0, (
        f"expected exit 0 on the real, now-armed-and-passing tree.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" in result.stdout, (
        f"expected output to contain 'PASS:'. Got:\n{result.stdout}"
    )
    assert "platform/py32f071" in result.stdout, (
        f"expected 'platform/py32f071' named on the PASS line. "
        f"Got:\n{result.stdout}"
    )


def test_unarmed_on_clean_unarmed_tree_fixture():
    """Coverage 2 -- pointing the seam at clean_unarmed_tree/ (no platform/
    directory at all) must also report UNARMED and exit 0, proving the
    arming decision follows the SUPPLIED root, not the process cwd."""
    result = _run_checker(manifest_root=_CLEAN_UNARMED)
    assert result.returncode == 0, (
        f"expected exit 0 on clean_unarmed_tree/.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "UNARMED:" in result.stdout, (
        f"expected 'UNARMED:' in output. Got:\n{result.stdout}"
    )


def test_mismatched_path_fails_with_exactly_one_violation():
    """Coverage 3 -- planted_cmake_manifest_missing_source/ names one
    FIRESTARTER_COMMON_SOURCES entry that does not resolve, and two
    PY32_SDK_SOURCES entries that also do not resolve but are exempt.
    The gate must report EXACTLY 1 violation, never 3 -- Pitfall 6's
    failure mode is a gate that reports permanent SDK violations on a tree
    with no real defect, so the absence of any SDK path is asserted
    explicitly, not just the count."""
    result = _run_checker(manifest_root=_MISSING_SOURCE)
    assert result.returncode != 0, (
        f"expected non-zero exit on the planted missing-source fixture.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "FAIL: 1 " in result.stdout, (
        f"expected exactly 1 violation reported. Got:\n{result.stdout}"
    )
    assert "flash_type_3.cpp" in result.stdout, (
        f"expected the missing entry's filename named in the output. "
        f"Got:\n{result.stdout}"
    )
    assert "PY32_SDK_ROOT" not in result.stdout, (
        f"the SDK entries must never appear in the violation output -- "
        f"they are structurally exempt. Got:\n{result.stdout}"
    )


def test_sdk_list_is_exempt_stated_positively():
    """Coverage 4 -- a clean armed run's PASS: output must record
    PY32_SDK_SOURCES by name, so the exemption is visible to a reader
    rather than implicit in a silence."""
    result = _run_checker(manifest_root=_CLEAN_EXCLUDED)
    assert result.returncode == 0, (
        f"expected exit 0 on the clean-excluded fixture.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PY32_SDK_SOURCES" in result.stdout, (
        f"expected PY32_SDK_SOURCES named in the PASS: output. "
        f"Got:\n{result.stdout}"
    )


def test_unreasoned_allow_list_entry_fails():
    """Coverage 5 -- planted_cmake_manifest_excluded_no_reason/ carries a
    PY32_EXCLUDED comment with a path but no '-- reason' segment. This
    must fail, naming the entry and the missing reason -- an allow-list
    without required reasons degrades into a silencer."""
    result = _run_checker(manifest_root=_EXCLUDED_NO_REASON)
    assert result.returncode != 0, (
        f"expected non-zero exit on the unreasoned-exclusion fixture.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "uno_rurp_shield.cpp" in result.stdout, (
        f"expected the unreasoned entry's path named. Got:\n{result.stdout}"
    )
    assert "reason" in result.stdout.lower(), (
        f"expected the missing-reason message. Got:\n{result.stdout}"
    )


def test_reasoned_omission_passes_and_is_named():
    """Coverage 6 -- clean_cmake_manifest_excluded/ exits 0 and its PASS:
    line names the allow-listed omission -- ROADMAP criterion 3's second
    half: the gate must distinguish a deliberate, reasoned omission from
    rename damage."""
    result = _run_checker(manifest_root=_CLEAN_EXCLUDED)
    assert result.returncode == 0, (
        f"expected exit 0 on the clean-excluded fixture.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" in result.stdout, f"expected PASS:. Got:\n{result.stdout}"
    assert "uno_rurp_shield.cpp" in result.stdout, (
        f"expected the allow-listed omission named on the PASS: line. "
        f"Got:\n{result.stdout}"
    )


def test_armed_but_manifest_missing_is_a_hard_failure(tmp_path):
    """Coverage 7 -- the single most important test in this module (D-07):
    copy an armed fixture tree into tmp_path, delete its CMakeLists.txt
    (platform/py32f071/ itself still exists), and assert the gate exits
    non-zero rather than reverting to UNARMED. A rename or deletion of the
    manifest INSIDE the port must never look like the port being absent."""
    dest = tmp_path / "tree"
    shutil.copytree(_CLEAN_EXCLUDED, dest)
    manifest = dest / "platform" / "py32f071" / "CMakeLists.txt"
    assert manifest.is_file(), "fixture setup: manifest must exist before deletion"
    manifest.unlink()

    result = _run_checker(manifest_root=dest)
    assert result.returncode != 0, (
        f"expected non-zero exit with the manifest deleted under an armed "
        f"platform/py32f071/ directory.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "UNARMED" not in result.stdout and "UNARMED" not in result.stderr, (
        f"a deleted manifest under an ARMED key must NEVER be reported as "
        f"UNARMED -- that would let a rename/deletion inside the port "
        f"silently disarm this gate.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )


def test_unknown_source_list_is_exit_2(tmp_path):
    """Coverage 8 -- copy an armed fixture tree into tmp_path, append a
    fourth set() source list with an unrecognised name, and assert the
    LITERAL return code 2. An unrecognised source list is a configuration
    change this gate has not been taught about, and must never be silently
    ignored."""
    dest = tmp_path / "tree"
    shutil.copytree(_CLEAN_EXCLUDED, dest)
    manifest = dest / "platform" / "py32f071" / "CMakeLists.txt"
    text = manifest.read_text()
    text += '\nset(WEIRD_UNKNOWN_SOURCES\n    "weird_unknown.cpp"\n)\n'
    manifest.write_text(text)

    result = _run_checker(manifest_root=dest)
    assert result.returncode == 2, (
        f"expected the literal exit code 2 for an unrecognised source "
        f"list, got {result.returncode}.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
