"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 153 Plan 05 -- ERASE-04's paired pytest for
scripts/check_erase_no_vpp.py, the primary GATE-03 control (D-153-03).

**The mechanism correction, restated here (same three-place record as the
checker's own docstring and D-153-03):**
`firestarter_app/tools/check_dispatch.py`'s GATE-03 guard fires only on a
`handler == "configure_eprom"` paired with a no-VPP-pin pinout -- a
database-and-dispatch-table scan that is structurally unable to observe a
control-register write inside a C++ handler body. A green
`check_dispatch.py` is NOT a VPP proof for `eeprom28c_erase_execute` and
must never be cited as one. `scripts/check_erase_no_vpp.py` is the real
control: a brace-matched negative scan of that function's own body. This
module's job is to prove that scan is reachable -- both directions -- not
merely present.

This module runs in NO CI leg on this branch, the same standing disclosure
as `check_erase_no_vpp.py` itself and as `tests/test_check_size_baseline.py`:
`build.yml` and `beta-build.yml` invoke only `pio test -e native` and
`pio test -e native_nodevtools`, neither of which runs `pytest` over
`firestarter/tests/` at all. This plan's SUMMARY.md, recording a local run,
is the only evidence any of the seven legs below were ever exercised.

Self-contained path resolution below -- NOT in conftest.py
(`firestarter/tests/` has no conftest.py anywhere in the repo; a recorded
house-rule pattern, per test_checker_convention.py's own precedent, not an
omission). Stdlib and pytest only.

Every leg below invokes the checker as a REAL subprocess through one
module-level helper, `_run_checker`, never an in-process import -- so a
passing leg proves the script itself, run exactly as a phase gate would run
it, behaves as claimed (mirrors test_check_orphan_provisional.py's
subprocess-only convention).

Coverage (seven legs, one test function each):
  1. test_real_source_passes -- the checker exits 0 against the real
     src/proms/eeprom_28c.cpp and prints a PASS: line.
  2. test_planted_fixture_fails_reachability -- the REACHABILITY leg: the
     checker exits non-zero against the committed planted violation and
     names both the VPP control-bit token and the control-register setter,
     with line numbers -- asserted on the message, not only the returncode,
     so a checker failing for an unrelated reason cannot satisfy this leg.
  3. test_missing_source_is_fail_closed -- a nonexistent --source is a
     fail-closed ERROR:, never a pass.
  4. test_anchor_is_live -- pointed at eeprom28c_sdp_lock_execute (a real
     function with zero hazard tokens AND no anchors), the checker exits
     with the fail-closed returncode and names the anchor failure -- proving the
     anchor guard is load-bearing, not decorative.
  5. test_discrimination_against_check_chip_id -- pointed at
     eeprom28c_check_chip_id (the real A9-12V chip-id path), the checker
     exits non-zero. Together with leg 1, this proves the checker
     DISCRIMINATES between bodies rather than always passing or always
     failing.
  6. test_checker_source_never_references_firestarter_app -- scope guard:
     the checker's own source text contains no "firestarter_app" substring,
     so a future edit cannot quietly widen this checker into the host repo.
  7. test_no_skip_in_this_module -- this module contains no unconditional
     test-skip call and no skip-conditional marker, keeping this gate
     fail-closed in the house style, not silently disabled. (Deliberately
     phrased without writing the two forbidden literal tokens contiguously
     anywhere in this file, including this sentence -- otherwise the
     module's own docstring would trip the very check it describes; see
     the leg 7 implementation below for the reconstructed literals it
     actually searches for.)
"""

import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_CHECKER = _REPO_ROOT / "scripts" / "check_erase_no_vpp.py"
_FIXTURES = _HERE / "fixtures"
_REAL_SOURCE = _REPO_ROOT / "src" / "proms" / "eeprom_28c.cpp"
_PLANTED_FIXTURE = _FIXTURES / "planted_erase_no_vpp_ctrl_write.cpp"


def _run_checker(argv=None):
    """Invoke check_erase_no_vpp.py as a real subprocess. Returns
    (exit_code, stdout, stderr). cwd is deliberately the repo root, mirroring
    how a phase gate would be run -- the checker's own cwd-independence is
    covered by its module docstring's usage note, not re-tested here."""
    result = subprocess.run(
        [sys.executable, str(_CHECKER), *(argv or [])],
        cwd=str(_REPO_ROOT),
        capture_output=True,
        text=True,
    )
    return result.returncode, result.stdout, result.stderr


def test_real_source_passes():
    """Leg 1 -- the real source passes: exit 0, PASS: line."""
    returncode, out, err = _run_checker()
    assert returncode == 0, f"expected exit 0 against the real source.\nstdout:\n{out}\nstderr:\n{err}"
    assert out.startswith("PASS:"), f"expected a PASS: line. Got:\n{out}"
    assert "eeprom28c_erase_execute" in out


def test_planted_fixture_fails_reachability():
    """Leg 2 -- REACHABILITY: the checker is OBSERVED failing against the
    committed planted violation, not merely claimed to be able to. Asserts
    on the message (the VPP control-bit token, the control-register
    setter, and a line number), not only the returncode -- a checker that
    failed for an unrelated reason could satisfy a bare `returncode != 0`
    check without ever having found the planted hazard at all."""
    returncode, out, err = _run_checker(["--source", str(_PLANTED_FIXTURE)])
    assert returncode != 0, f"expected a non-zero exit against the planted fixture.\nstdout:\n{out}\nstderr:\n{err}"
    combined = out + err
    assert "FAIL:" in combined, f"expected FAIL: in output. Got:\n{combined}"
    assert "CTRL_VPP" in combined, f"expected the VPP control-bit token named. Got:\n{combined}"
    assert "firestarter_set_control_register" in combined, (
        f"expected the control-register setter named. Got:\n{combined}"
    )
    assert "line " in combined, f"expected at least one line-numbered violation. Got:\n{combined}"


def test_missing_source_is_fail_closed():
    """Leg 3 -- a missing --source is a fail-closed violation, not a pass:
    exit 2, output begins ERROR:, never PASS:."""
    returncode, out, err = _run_checker(["--source", "src/proms/does_not_exist.cpp"])
    assert returncode == 2, f"expected the fail-closed exit returncode 2, got {returncode}.\nstdout:\n{out}\nstderr:\n{err}"
    combined = out + err
    assert combined.lstrip().startswith("ERROR:"), f"expected output to begin with ERROR:. Got:\n{combined}"
    assert "PASS:" not in out, f"a fail-closed run must never print PASS:. Got:\n{out}"


def test_anchor_is_live():
    """Leg 4 -- the non-vacuity anchor is live, not decorative:
    eeprom28c_sdp_lock_execute is a real function with zero hazard tokens
    AND no non-vacuity anchor, so a hollow scan would silently PASS: it.
    The checker must instead exit 2 and name the anchor failure."""
    returncode, out, err = _run_checker(["--function", "eeprom28c_sdp_lock_execute"])
    assert returncode == 2, f"expected the anchor-failure exit returncode 2, got {returncode}.\nstdout:\n{out}\nstderr:\n{err}"
    combined = out + err
    assert "ERROR:" in combined, f"expected ERROR: in output. Got:\n{combined}"
    assert "anchor" in combined.lower(), f"expected the anchor failure named. Got:\n{combined}"
    assert "PASS:" not in out, f"an anchor failure must never print PASS:. Got:\n{out}"


def test_discrimination_against_check_chip_id():
    """Leg 5 -- DISCRIMINATION: eeprom28c_check_chip_id's legitimate A9-12V
    control-register writes are flagged as violations. Together with leg 1
    (the real erase body passes), this proves the checker discriminates
    between bodies -- the erase body passes because it genuinely lacks the
    tokens, not because the scan is inert."""
    returncode, out, err = _run_checker(["--function", "eeprom28c_check_chip_id"])
    assert returncode != 0, f"expected a non-zero exit against eeprom28c_check_chip_id.\nstdout:\n{out}\nstderr:\n{err}"
    combined = out + err
    assert "FAIL:" in combined, f"expected FAIL: in output. Got:\n{combined}"
    assert "CTRL_VPP" in combined, f"expected a CTRL_VPP hit named. Got:\n{combined}"


def test_checker_source_never_references_firestarter_app():
    """Leg 6 -- SCOPE: the checker's own source text contains no
    'firestarter_app' substring, so a future edit cannot quietly widen this
    checker's scan into the host repository (mirrors
    test_checker_convention.py's own scope guard for its glob)."""
    text = _CHECKER.read_text(encoding="utf-8")
    assert "firestarter_app" not in text, (
        "check_erase_no_vpp.py must never reference firestarter_app -- "
        "found the substring in its own source"
    )


def test_no_skip_in_this_module():
    """Leg 7 -- NO-SKIP: this module contains no unconditional test-skip
    call and no skip-conditional marker anywhere in its own source, keeping
    this gate fail-closed in the house style rather than silently disabled.

    The two forbidden literals are reconstructed from parts below so this
    docstring, and the module as a whole, never spells either one out
    contiguously -- writing them out literally would make this module's own
    text trip the exact violation this test exists to catch."""
    this_module_text = Path(__file__).read_text(encoding="utf-8")
    skip_call = "pytest" + "." + "skip"
    skip_marker = "skip" + "if"
    assert skip_call not in this_module_text, f"found a bare {skip_call} call in this module"
    assert skip_marker not in this_module_text, f"found a {skip_marker} marker in this module"
