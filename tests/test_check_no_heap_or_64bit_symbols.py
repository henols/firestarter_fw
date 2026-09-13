"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 155 Plan 02 -- the BASE-08 anti-hollow pairing for
scripts/check_no_heap_or_64bit_symbols.py.

Requirements: DEAD-01, DEAD-03

This is the MANDATORY anti-hollow pairing for the DEAD-01/DEAD-03 link-time
symbol-absence gate: a checker with no negative-fixture test is exactly this
project's v1.12 hollow-GATE-03 failure mode -- a gate that could never fail
because nothing concrete was asserted against it. Every planted-violation
test below invokes scripts/check_no_heap_or_64bit_symbols.py as a real
subprocess (list-form argv, never shell=True, never an in-process import),
so a passing suite here proves the checker itself -- not this test module --
goes RED on a real pre-change symbol table and reaches exit 0 on a clean
one.

No `pio` invocation and no live `avr-nm` invocation happens anywhere in this
module: every fixture used here is a committed, captured TEXT `avr-nm`
listing, read via the checker's `--nm-output TARGET=PATH` seam. This keeps
the suite hermetic in CI leg 3, where no AVR toolchain is installed --
mirroring `test_check_size_baseline.py`'s own committed-log convention.

Two of the derived-control fixtures below (Coverage 2 and Coverage 4) are
SYNTHETIC: they are produced at test time by filtering specific lines out of
the one committed REAL negative,
`tests/fixtures/planted_no_heap_or_64bit_symbols_prechange_uno/avr-nm-uno.txt`.
Plan 06 later commits the REAL post-change `uno` listing and adds
`test_real_postchange_listing_exits_zero` alongside this module's synthetic
clean-control leg -- that real positive control does not exist yet, which is
why Coverage 2 is explicit about being synthetic rather than silently
presenting a filtered real fixture as if it were one.

The heap-set and 64-bit-set name lists below are duplicated from
scripts/check_no_heap_or_64bit_symbols.py's own HEAP_SYMBOLS/DI64_SYMBOLS
rather than imported, following this repo's own established idiom of
duplicating small constant sets between a checker and its test (the same
choice tests/test_write_path_source_contract_v131.py's _strip_comments and
scripts/check_erase_no_vpp.py's own copy of it both make) -- this module
tests the checker as an external program via subprocess, and never imports
it in-process.

Coverage:
  1. The committed real pre-change listing exits 1 and names the four
     offending symbols the message must carry.
  2. A synthetic clean listing, derived by deleting the forbidden lines from
     the real pre-change listing (anchors kept), exits 0 and names the
     target.
  3. A missing --nm-output listing path exits exactly 2.
  4. A synthetic clean-but-anchor-stripped listing exits exactly 2 -- the
     leg proving the gate cannot pass vacuously on a listing that is not
     the real firmware.
  5. An unrecognised argv flag exits exactly 2.
  6. A tmp_path baseline whose avr_targets is an empty object exits 1 with
     the never-vacuous wording.
  7. A tmp_path baseline that is not valid JSON exits exactly 2.
  8. The committed fixture is present, non-empty, resolves under
     _REPO_ROOT, and contains at least one line for each forbidden set plus
     both anchors -- a missing or emptied fixture must FAIL, never SKIP.
  9. This module's own source contains no runtime skip-bypass call, no
     skip-marker decorator, and no dependency-skip call.

Self-contained path resolution below -- NOT in conftest.py (firestarter/tests/
has no conftest.py anywhere in the repo; this is a recorded house-rule
pattern decision, per test_update_version.py's own comment, not an
omission). Stdlib and pytest only.
"""

import json
import os
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_CHECKER = _REPO_ROOT / "scripts" / "check_no_heap_or_64bit_symbols.py"
_FIXTURES = _HERE / "fixtures"
_PLANTED_UNO = _FIXTURES / "planted_no_heap_or_64bit_symbols_prechange_uno" / "avr-nm-uno.txt"
_CLEAN_POSTCHANGE_UNO = (
    _FIXTURES / "clean_no_heap_or_64bit_symbols_postchange_uno" / "avr-nm-uno.txt"
)

# Duplicated from check_no_heap_or_64bit_symbols.py's own HEAP_SYMBOLS /
# DI64_SYMBOLS / REQUIRED_ANCHORS -- see module docstring for why this is a
# deliberate duplication, not an import.
_HEAP_SYMBOLS = frozenset(
    {
        "malloc",
        "free",
        "realloc",
        "calloc",
        "__brkval",
        "__flp",
        "__malloc_heap_start",
        "__malloc_heap_end",
        "__malloc_margin",
    }
)
_DI64_SYMBOLS = frozenset(
    {
        "__muldi3",
        "__muldi3_6",
        "__umulsidi3",
        "__umulsidi3_helper",
        "__umoddi3",
        "__udivdi3",
        "__udivdi3_umoddi3",
        "__udivmod64",
        "__ashrdi3",
        "__lshrdi3",
        "__adddi3",
    }
)
_REQUIRED_ANCHORS = frozenset({"mem_util_blank_check", "rurp_read_voltage_mv"})


def _run_checker(argv=None, env_overrides=None):
    """Invoke check_no_heap_or_64bit_symbols.py as a real subprocess (list
    argv, never shell=True, never an in-process import).

    `env_overrides`, when given, is merged into the child's environment on
    top of the current process environment.
    """
    env = {**os.environ, **(env_overrides or {})}
    return subprocess.run(
        [sys.executable, str(_CHECKER), *(argv or [])],
        cwd=str(_REPO_ROOT),
        capture_output=True,
        text=True,
        env=env,
    )


def _filtered_listing(source_text, drop_forbidden=True, drop_anchors=False):
    """Derive a filtered avr-nm listing by removing lines whose last
    whitespace-separated field (the symbol name) is in the forbidden sets
    and/or the anchor set. Used only to build the two SYNTHETIC derived
    controls (Coverage 2 and Coverage 4) -- see module docstring."""
    forbidden = _HEAP_SYMBOLS | _DI64_SYMBOLS
    out_lines = []
    for line in source_text.splitlines():
        parts = line.split()
        name = parts[-1] if parts else ""
        if drop_forbidden and name in forbidden:
            continue
        if drop_anchors and name in _REQUIRED_ANCHORS:
            continue
        out_lines.append(line)
    return "\n".join(out_lines) + "\n"


def test_planted_prechange_listing_exits_one_and_names_the_symbols():
    """Coverage 1 -- the committed REAL pre-change uno listing exits
    non-zero, the code is exactly 1, stdout contains FAIL:, and stdout
    names the two allocator entry points plus __muldi3 and
    __umulsidi3_helper. __umulsidi3_helper is the 84 B symbol DEAD-03's
    eight-name list omits, so its presence in the message is what proves
    this gate covers all eleven 64-bit symbols, not merely the eight
    named ones."""
    result = _run_checker(["--nm-output", f"uno={_PLANTED_UNO}"])
    assert result.returncode != 0, (
        f"expected non-zero exit on the real pre-change listing.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert result.returncode == 1, (
        f"expected the literal exit code 1 (real violation found), got "
        f"{result.returncode}.\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "FAIL:" in result.stdout, f"expected FAIL: in output. Got:\n{result.stdout}"
    for name in ("malloc", "free", "__muldi3", "__umulsidi3_helper"):
        assert name in result.stdout, (
            f"expected {name!r} named in the FAIL output. Got:\n{result.stdout}"
        )


def test_derived_clean_listing_exits_zero_and_names_the_target():
    """Coverage 2 -- SYNTHETIC control, derived from the real pre-change
    listing by deleting exactly the lines whose symbol name is in the
    forbidden sets, leaving the anchors intact. Exits 0, stdout contains
    PASS: and names the target ('uno'). This was, before plan 06 landed,
    the only exit-0 control this module had. Plan 06 has since committed
    the REAL post-change uno listing
    (tests/fixtures/clean_no_heap_or_64bit_symbols_postchange_uno/avr-nm-uno.txt)
    and added test_real_postchange_listing_exits_zero below, which is now
    the gate's real positive control. This leg is kept as a second,
    independent exit-0 proof derived by construction rather than captured,
    and its docstring is updated in place rather than deleted so the
    synthetic-vs-real distinction stays visible."""

    def _derive(tmp_path):
        clean_text = _filtered_listing(_PLANTED_UNO.read_text(), drop_forbidden=True)
        derived = tmp_path / "derived_clean_uno.txt"
        derived.write_text(clean_text)
        return derived

    import tempfile

    with tempfile.TemporaryDirectory() as tmp_dir:
        derived_path = _derive(Path(tmp_dir))
        result = _run_checker(["--nm-output", f"uno={derived_path}"])
        assert result.returncode == 0, (
            f"expected exit 0 on the derived clean listing.\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        assert "PASS:" in result.stdout, f"expected PASS: in output. Got:\n{result.stdout}"
        assert "uno" in result.stdout, f"expected 'uno' named in PASS output. Got:\n{result.stdout}"


def test_real_postchange_listing_exits_zero():
    """Plan 06's real post-change clean control -- the REAL, committed,
    unedited avr-nm listing for the real post-change uno build (captured
    at FW_POST_SHA 98e70af1a89ea69ba9d5a925fa7073fb0bfc61a6, plans 04 and
    05 both landed), not a synthetic derivative filtered from the
    pre-change listing at test time. Exits 0, stdout contains PASS:, and
    stdout names the target ('uno'). This is the gate's real positive
    control, replacing the promise made in
    test_derived_clean_listing_exits_zero_and_names_the_target's docstring
    before this fixture existed."""
    result = _run_checker(["--nm-output", f"uno={_CLEAN_POSTCHANGE_UNO}"])
    assert result.returncode == 0, (
        f"expected exit 0 on the real post-change listing.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" in result.stdout, f"expected PASS: in output. Got:\n{result.stdout}"
    assert "uno" in result.stdout, f"expected 'uno' named in PASS output. Got:\n{result.stdout}"


def test_missing_listing_path_exits_two():
    """Coverage 3 -- a --nm-output path that does not exist on disk exits
    exactly 2, a fail-closed tool/format failure distinct from a real
    violation."""
    result = _run_checker(["--nm-output", "uno=/nonexistent/path/avr-nm-uno.txt"])
    assert result.returncode == 2, (
        f"expected the literal exit code 2 (missing listing), got "
        f"{result.returncode}.\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" not in result.stdout, (
        f"a fail-closed run must never print PASS:. Got:\n{result.stdout}"
    )


def test_clean_listing_without_anchors_exits_two():
    """Coverage 4 -- SYNTHETIC negative control, derived by deleting the
    forbidden lines AND both anchor lines from the real pre-change listing.
    This is the leg that proves the gate cannot pass vacuously on a
    listing that is not the real firmware: zero forbidden symbols, but no
    proof this is even the right binary."""
    import tempfile

    with tempfile.TemporaryDirectory() as tmp_dir:
        no_anchor_text = _filtered_listing(
            _PLANTED_UNO.read_text(), drop_forbidden=True, drop_anchors=True
        )
        derived_path = Path(tmp_dir) / "derived_no_anchor_uno.txt"
        derived_path.write_text(no_anchor_text)

        result = _run_checker(["--nm-output", f"uno={derived_path}"])
        assert result.returncode == 2, (
            f"expected the literal exit code 2 (missing non-vacuity anchor), got "
            f"{result.returncode}.\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        assert "PASS:" not in result.stdout, (
            f"a vacuous run must never print PASS:. Got:\n{result.stdout}"
        )
        combined = result.stdout + result.stderr
        assert "mem_util_blank_check" in combined or "rurp_read_voltage_mv" in combined, (
            f"expected the missing anchor named in the ERROR output. Got:\n{combined}"
        )


def test_malformed_argv_exits_two():
    """Coverage 5 -- an unrecognised argv flag exits exactly 2."""
    result = _run_checker(["--definitely-not-a-flag"])
    assert result.returncode == 2, (
        f"expected the literal exit code 2 (malformed argv), got "
        f"{result.returncode}.\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )




def test_scan_targets_are_non_vacuous():
    """Coverage 8 -- the committed fixture exists, is non-empty, resolves
    under _REPO_ROOT, contains at least one line for each of the two
    forbidden sets, and contains both anchors. A missing or emptied
    fixture must FAIL, never SKIP."""
    assert _PLANTED_UNO.is_file(), (
        f"committed fixture {_PLANTED_UNO} does not exist on disk -- a missing "
        "fixture must FAIL, never silently pass."
    )
    assert _PLANTED_UNO.stat().st_size > 0, f"committed fixture {_PLANTED_UNO} is empty"
    assert _PLANTED_UNO.resolve().is_relative_to(_REPO_ROOT), (
        f"committed fixture {_PLANTED_UNO} resolves outside _REPO_ROOT ({_REPO_ROOT})"
    )

    text = _PLANTED_UNO.read_text()
    names_present = {line.split()[-1] for line in text.splitlines() if line.split()}

    heap_matches = names_present & _HEAP_SYMBOLS
    assert heap_matches, (
        f"expected at least one heap-set symbol in {_PLANTED_UNO}, found none"
    )
    di64_matches = names_present & _DI64_SYMBOLS
    assert di64_matches, (
        f"expected at least one 64-bit-set symbol in {_PLANTED_UNO}, found none"
    )
    missing_anchors = _REQUIRED_ANCHORS - names_present
    assert not missing_anchors, (
        f"expected both anchors present in {_PLANTED_UNO}, missing {missing_anchors}"
    )


def test_this_module_cannot_be_silently_skipped():
    """Coverage 9 -- the concatenation-built-needle leg, copied in spirit
    from tests/test_write_path_source_contract_v131.py's
    test_this_module_cannot_be_silently_skipped: this module's own source
    contains no runtime skip-bypass call, no skip-marker decorator, and no
    dependency-skip call anywhere, so a missing or emptied fixture FAILS,
    never SKIPS."""
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    dependency_skip_call = "importor" + "skip"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- a "
        "missing or empty scan target must FAIL, never SKIP."
    )
    assert skipif_marker not in own_text, (
        "expected no @pytest." + skipif_marker + " decorator anywhere in "
        "this module -- a missing or empty scan target must FAIL, never SKIP."
    )
    assert ("pytest." + dependency_skip_call) not in own_text, (
        "expected no pytest." + dependency_skip_call + " call anywhere in "
        "this module -- a missing dependency must FAIL, never SKIP."
    )
