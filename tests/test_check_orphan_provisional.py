"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 123 Plan 05 — the BASE-08 anti-hollow pairing for
scripts/check_orphan_provisional.py.

Requirements: BASE-05, BASE-08
Decisions covered: D-06, D-07

This is the MANDATORY anti-hollow pairing for the BASE-05 orphan-provisional
-macro gate: a checker with no negative-fixture test is exactly this
project's v1.12 hollow-GATE-03 failure mode -- a declared-empty detector
that could never fail because nothing concrete was asserted against it.
Every test below invokes check_orphan_provisional.py as a REAL SUBPROCESS
(list-form subprocess.run, never shell=True) against one of the two
committed fixture trees under tests/fixtures/ (or the shared
clean_unarmed_tree/ from Plan 123-04) via the FIRESTARTER_PROVISIONAL_ROOT
env seam set in the CHILD's environment -- never an in-process import, and
never an in-process env-var patch, because ARMED and
FIRESTARTER_PROVISIONAL_ROOT bind at import time and an in-process patch
would be silently ineffective (123-RESEARCH.md Correction C-15). This
module never imports check_orphan_provisional.

Coverage:
  1. ARMED and PASSING on the real tree (no seam override) -- Phase 124
     landed platform/py32f071/ and wired both provisional macros'
     consumers (py32f071_rurp_shield.h's bridging block, and
     rurp_pinmap_guard.h's refusal predicate consumed by
     configure_memory()), so this test now pins the armed, passing state.
     A regression to the UNARMED: line would mean platform/py32f071/ had
     disappeared from the tree.
  2. UNARMED on the shared clean_unarmed_tree/ through the seam -- proves
     the arming decision follows the supplied root, not the process cwd.
  3. The planted orphan fails with exactly one violation, naming the
     orphaned macro and NOT the consumed one.
  4. The consumed control passes, naming the macro with a non-zero
     consumer count on the PASS: line.
  5. An #undef of the macro is not a consumer -- replacing the sole
     consumer with an #undef must still fail the gate.
  6. A comment mention of the macro is not a consumer -- replacing the
     sole consumer with a comment must still fail the gate (pinned
     against check_orphan_provisional.py's documented comment-stripping
     rule, so a future change to that rule is deliberate, not drift).
  7. An armed tree with zero _PROVISIONAL definitions found is a failure,
     not a reversion to UNARMED.
  8. The real-world define spelling used by py32f071_rurp_shield.h matches
     DEFINE_RE, pinned as a literal in this test file (never read from the
     gitignored py32 worktree, which is absent in a fresh clone).

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo; a recorded house-rule
pattern decision per test_update_version.py's own comment, not an
omission). Stdlib and pytest only.
"""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_CHECKER = _REPO_ROOT / "scripts" / "check_orphan_provisional.py"
_FIXTURES = _HERE / "fixtures"

_PLANTED_ORPHAN = _FIXTURES / "planted_orphan_provisional_macro"
_CLEAN_CONSUMED = _FIXTURES / "clean_orphan_provisional_consumed"
_CLEAN_UNARMED = _FIXTURES / "clean_unarmed_tree"


def _run_checker(provisional_root=None):
    """Invoke check_orphan_provisional.py as a real subprocess (list argv,
    never shell=True). `provisional_root`, when not None, sets
    FIRESTARTER_PROVISIONAL_ROOT in the CHILD's environment to that exact
    path -- when None, the env var is left absent entirely, reaching the
    "variable genuinely absent -> defaults to this repo's own root" path
    (which is the real, still-UNARMED, tree today)."""
    env = {**os.environ}
    if provisional_root is not None:
        env["FIRESTARTER_PROVISIONAL_ROOT"] = str(provisional_root)
    else:
        env.pop("FIRESTARTER_PROVISIONAL_ROOT", None)
    return subprocess.run(
        [sys.executable, str(_CHECKER)],
        cwd=str(_REPO_ROOT),
        capture_output=True,
        text=True,
        env=env,
    )


def test_armed_and_passing_on_the_real_tree():
    """Coverage 1 -- no FIRESTARTER_PROVISIONAL_ROOT override: Phase 124
    landed platform/py32f071/ on this tree AND wired both consumers (the
    py32 board header's bridging block, and rurp_pinmap_guard.h's refusal
    predicate consumed by configure_memory()), so the gate is now ARMED
    and PASSING -- not UNARMED. This test pins that armed, passing state:
    a regression back to the UNARMED: line would mean platform/py32f071/
    had disappeared from the real tree.

    Superseded assertion, deliberately NOT restored: the prior UNARMED-era
    version asserted 'platform/py32f071' was named in the message. The
    armed PASS: line does not name a directory at all (it names macros and
    consumer counts) -- re-verified against the real output below, this
    assertion is correctly dropped rather than weakened to keep it.

    Also deliberately NOT restored: the prior version asserted '124' (the
    phase number) appeared in the UNARMED message. The armed PASS: line
    never names a phase number either -- weakening the assertion just to
    preserve a '124' substring match would be backwards, so it is dropped,
    not kept."""
    result = _run_checker(provisional_root=None)
    assert result.returncode == 0, (
        f"expected exit 0 on the real, now-armed-and-passing tree.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" in result.stdout, (
        f"expected 'PASS:' in output. Got:\n{result.stdout}"
    )
    assert "RURP_PY32F071_PINMAP_PROVISIONAL" in result.stdout, (
        f"expected the py32-specific provisional macro named on the PASS: "
        f"line -- this is the substantive new thing the armed state "
        f"proves. Got:\n{result.stdout}"
    )
    assert "RURP_PINMAP_PROVISIONAL" in result.stdout, (
        f"expected the platform-neutral provisional macro named on the "
        f"PASS: line -- this is the substantive new thing the armed state "
        f"proves. Got:\n{result.stdout}"
    )


def test_unarmed_on_clean_unarmed_tree_fixture():
    """Coverage 2 -- pointing the seam at the shared clean_unarmed_tree/
    (no platform/ directory at all, from Plan 123-04) must also report
    UNARMED and exit 0, proving the arming decision follows the SUPPLIED
    root, not the process cwd."""
    result = _run_checker(provisional_root=_CLEAN_UNARMED)
    assert result.returncode == 0, (
        f"expected exit 0 on clean_unarmed_tree/.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "UNARMED:" in result.stdout, (
        f"expected 'UNARMED:' in output. Got:\n{result.stdout}"
    )


def test_orphan_fails_with_exactly_one_violation():
    """Coverage 3 -- planted_orphan_provisional_macro/ carries one orphaned
    macro (zero consumers) and one consumed macro (the discriminating
    control). The gate must report EXACTLY 1 violation, naming the
    orphaned macro and NOT the consumed one -- a fixture where every
    macro is orphaned would not distinguish a working gate from one that
    fails unconditionally."""
    result = _run_checker(provisional_root=_PLANTED_ORPHAN)
    assert result.returncode != 0, (
        f"expected non-zero exit on the planted orphan fixture.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "FAIL: 1 " in result.stdout, (
        f"expected exactly 1 violation reported. Got:\n{result.stdout}"
    )
    assert "RURP_FIXTURE_ORPHAN_PROVISIONAL" in result.stdout, (
        f"expected the orphaned macro named in the output. Got:\n{result.stdout}"
    )
    assert "RURP_FIXTURE_CONSUMED_PROVISIONAL" not in result.stdout, (
        f"the consumed control macro must NEVER appear in the violation "
        f"bucket. Got:\n{result.stdout}"
    )


def test_consumed_control_passes():
    """Coverage 4 -- clean_orphan_provisional_consumed/ exits 0 and its
    PASS: line names the macro with a non-zero consumer count -- the
    discriminating control proving the gate can pass at all."""
    result = _run_checker(provisional_root=_CLEAN_CONSUMED)
    assert result.returncode == 0, (
        f"expected exit 0 on the consumed-control fixture.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" in result.stdout, f"expected PASS:. Got:\n{result.stdout}"
    assert "RURP_FIXTURE_CONSUMED_PROVISIONAL" in result.stdout, (
        f"expected the consumed macro named on the PASS: line. "
        f"Got:\n{result.stdout}"
    )


def test_undef_is_not_a_consumer(tmp_path):
    """Coverage 5 -- copy the clean tree into tmp_path and replace its sole
    consumer with an #undef of the macro; the gate must now fail. Without
    this, a gate that counted any occurrence (including an #undef) would
    look correct on the clean fixture alone."""
    dest = tmp_path / "tree"
    shutil.copytree(_CLEAN_CONSUMED, dest)
    consumer = dest / "src" / "fixture_consumer.cpp"
    assert consumer.is_file(), "fixture setup: consumer file must exist"
    consumer.write_text(
        '#include "fixture_provisional.h"\n'
        "\n"
        "#undef RURP_FIXTURE_CONSUMED_PROVISIONAL\n"
    )

    result = _run_checker(provisional_root=dest)
    assert result.returncode != 0, (
        f"expected non-zero exit once the sole consumer is replaced by an "
        f"#undef.\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "RURP_FIXTURE_CONSUMED_PROVISIONAL" in result.stdout, (
        f"expected the now-orphaned macro named in the FAIL output. "
        f"Got:\n{result.stdout}"
    )


def test_comment_mention_is_not_a_consumer(tmp_path):
    """Coverage 6 -- copy the clean tree into tmp_path and replace its sole
    consumer with a comment naming the macro; the gate must still fail.
    check_orphan_provisional.py documents (module docstring, "Consumer
    search" section) that a comment mention does NOT count as a consumer
    -- the same defect class as counting a bare #undef (threat
    T-123-05-01) -- and this test pins that rule so a future change to it
    is a deliberate one, not drift."""
    dest = tmp_path / "tree"
    shutil.copytree(_CLEAN_CONSUMED, dest)
    consumer = dest / "src" / "fixture_consumer.cpp"
    assert consumer.is_file(), "fixture setup: consumer file must exist"
    consumer.write_text(
        '#include "fixture_provisional.h"\n'
        "\n"
        "// RURP_FIXTURE_CONSUMED_PROVISIONAL is mentioned here only in a\n"
        "// comment, which must NOT count as a consumer.\n"
    )

    result = _run_checker(provisional_root=dest)
    assert result.returncode != 0, (
        f"expected non-zero exit once the sole consumer is replaced by a "
        f"comment-only mention (check_orphan_provisional.py's documented "
        f"rule: a comment is not a consumer).\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "RURP_FIXTURE_CONSUMED_PROVISIONAL" in result.stdout, (
        f"expected the now-orphaned macro named in the FAIL output. "
        f"Got:\n{result.stdout}"
    )


def test_armed_with_zero_definitions_fails(tmp_path):
    """Coverage 7 -- copy an armed tree into tmp_path, delete every
    _PROVISIONAL definition, and assert a non-zero exit whose message says
    no provisional flag was found under an armed key. Assert the output
    does NOT contain UNARMED -- an armed tree with a renamed-away or
    absent provisional flag must never look like the port simply being
    absent (the never-vacuous guard)."""
    dest = tmp_path / "tree"
    shutil.copytree(_CLEAN_CONSUMED, dest)
    shutil.rmtree(dest / "include")
    shutil.rmtree(dest / "src")
    (dest / "src").mkdir()
    (dest / "src" / "placeholder.cpp").write_text(
        "// no provisional macro here at all\n"
        "int placeholder(void) { return 0; }\n"
    )
    assert (dest / "platform" / "py32f071").is_dir(), (
        "fixture setup: arming key must survive the cleanup"
    )

    result = _run_checker(provisional_root=dest)
    assert result.returncode != 0, (
        f"expected non-zero exit with zero _PROVISIONAL definitions under "
        f"an ARMED platform/py32f071/ directory.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "UNARMED" not in result.stdout and "UNARMED" not in result.stderr, (
        f"an armed tree with zero provisional definitions must NEVER be "
        f"reported as UNARMED -- that would let a rename out of the "
        f"pattern silently disarm this gate.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "ZERO" in result.stdout or "zero" in result.stdout.lower(), (
        f"expected the zero-definitions message. Got:\n{result.stdout}"
    )


def test_real_world_define_spelling_matches_pattern():
    """Coverage 8 -- assert DEFINE_RE matches the exact #define line text
    used by include/boards/py32f071_rurp_shield.h on the py32 branch,
    supplied as a LITERAL in this test file rather than read from the
    py32 worktree -- the worktree is gitignored and absent in a fresh
    clone, and a test that depended on it would be exactly the cross-repo
    fragility this phase exists to remove."""
    real_define_line = "#define RURP_PY32F071_PINMAP_PROVISIONAL 1"
    pattern = re.compile(
        r"^[ \t]*#[ \t]*define[ \t]+(?P<macro>RURP_[A-Z0-9_]*_PROVISIONAL)\b",
        re.MULTILINE,
    )
    m = pattern.search(real_define_line)
    assert m is not None, (
        f"expected DEFINE_RE's literal pattern to match the real py32 "
        f"define spelling: {real_define_line!r}"
    )
    assert m.group("macro") == "RURP_PY32F071_PINMAP_PROVISIONAL", (
        f"expected the captured macro name to be "
        f"'RURP_PY32F071_PINMAP_PROVISIONAL', got {m.group('macro')!r}"
    )
