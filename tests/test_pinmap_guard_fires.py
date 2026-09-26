"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 124 Plan 09 — MERGE-04's fire-proof for the PY32F071 pin-map guard.

Requirements: MERGE-04
Decisions covered: D-14

As landed, include/boards/py32f071_rurp_shield.h defined
RURP_PY32F071_PINMAP_CONFIGURED to 1 roughly thirty lines above the
`#if !CONFIGURED -> #error` that tested it, so the guard could not fire
under any build configuration -- a hollow gate inside the branch being
landed. This defect class is prevention-only: an entire prior milestone
(v1.18) was caused by one mis-modelled pin trusted without a working guard.
Plan 124-09 hoists the guard into a dependency-free fragment header
(include/boards/py32f071_pinmap_guard.h) and moves the #define out of the
header into the ARM build's CMake compile definitions, so the header only
TESTS what the build supplies. This module is the fire-proof: it
preprocesses that fragment standalone with a real host compiler across
three discriminating arms (macro unset, =1, =0) and asserts the exact
mechanical outcome RESEARCH.md already executed by hand.

Self-contained path resolution below -- NOT in conftest.py (firestarter/tests/
has no conftest.py anywhere in the repo; a recorded house-rule pattern
decision per test_update_version.py's own comment, not an omission).
Stdlib and pytest only.

Coverage:
  1. test_guard_fires_when_the_macro_is_unset -- no define -> non-zero exit,
     the fragment's own #error text appears in stderr.
  2. test_guard_is_silent_when_the_macro_is_one -- define set to 1 -> exit 0,
     the error text absent from stderr.
  3. test_guard_fires_when_the_macro_is_zero -- define set to 0 -> non-zero
     exit, error text present. This is the arm that distinguishes a real
     value test from a mere defined-ness test.
  4. test_fragment_header_is_dependency_free -- the fragment header's own
     text contains no #include directive, so coverage 1-3 are evaluating
     the real production header, not a stand-in.
  5. test_board_header_does_not_define_what_it_tests -- the board header
     contains no top-level #define of the configured macro. Regression
     guard against the hollow shape returning.
  6. test_compiler_is_required_not_optional -- this module's own source
     contains no skip decorator and no skip call, so the fail-closed
     contract is self-enforcing.
"""

import os
import re
import shutil
import subprocess
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_INCLUDE_BOARDS = _REPO_ROOT / "include" / "boards"
_GUARD_HEADER = _INCLUDE_BOARDS / "py32f071_pinmap_guard.h"
_BOARD_HEADER = _INCLUDE_BOARDS / "py32f071_rurp_shield.h"

_MACRO_NAME = "RURP_PY32F071_PINMAP_CONFIGURED"


def _resolve_compiler():
    """Resolve the host C++ compiler, fail-closed.

    Deliberately never bypassed via any decorator or runtime call that would
    mark this outcome as skipped, anywhere in this module: doing so would
    recreate the exact BASE-02/BASE-03 absence-proxy failure class this
    milestone's Phase 123 removed. If $CXX (or 'g++') cannot be resolved via
    shutil.which, this raises via a plain assert, which the test runner
    reports as a FAILURE, never a skipped outcome. The AVR/ARM cross
    compilers are never invoked here -- both firmware CI workflows run
    `pytest tests/ -v` on a runner that has g++, before any embedded
    toolchain is installed.
    """
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    assert compiler is not None, (
        "host C++ compiler not found on PATH (checked $CXX, falling back to "
        "'g++'). This must FAIL the suite, never be silently skipped -- no "
        "embedded toolchain is invoked here."
    )
    return compiler


def _write_tu(tmp_path):
    """Write a minimal translation unit that includes ONLY the fragment
    header, by relative name (resolved via the -I include path passed to
    the compiler in _preprocess), plus an empty body. No fixture
    translation unit is committed to the repo -- the whole point of the
    hoist is that the fragment stands alone, so the TU is built fresh here
    each time."""
    tu_path = tmp_path / "pinmap_guard_tu.cpp"
    tu_path.write_text('#include "py32f071_pinmap_guard.h"\n')
    return tu_path


def _preprocess(compiler, tu_path, define=None):
    """Run the resolved compiler in preprocess-only mode (-E) against the
    given translation unit, with the repository's include/boards directory
    on the include path. Output is discarded; stdout, stderr and returncode
    are captured. List argv only -- the shell is never invoked."""
    argv = [compiler, "-E", "-I", str(_INCLUDE_BOARDS)]
    if define is not None:
        argv += [f"-D{_MACRO_NAME}={define}"]
    argv += [str(tu_path), "-o", os.devnull]
    return subprocess.run(argv, capture_output=True, text=True)


def _expected_error_text():
    """Read the #error message out of the fragment header at test time,
    rather than hardcoding it a second time here -- a literal that could
    silently desync from the real header on a future edit."""
    text = _GUARD_HEADER.read_text()
    m = re.search(r'#error\s+"([^"]*)"', text)
    assert m, f"expected exactly one #error directive with a quoted message in {_GUARD_HEADER}.\nGot:\n{text}"
    return m.group(1)


def test_guard_fires_when_the_macro_is_unset(tmp_path):
    """Coverage 1 -- with RURP_PY32F071_PINMAP_CONFIGURED left entirely
    unset, the guard must fire: non-zero exit, the fragment's own #error
    text present in stderr."""
    compiler = _resolve_compiler()
    tu_path = _write_tu(tmp_path)
    expected_text = _expected_error_text()

    result = _preprocess(compiler, tu_path, define=None)

    assert result.returncode != 0, (
        f"expected a non-zero exit with the macro unset (the guard must "
        f"fire).\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert expected_text in result.stderr, (
        f"expected the fragment's own #error text in stderr.\n"
        f"Expected substring: {expected_text!r}\nGot stderr:\n{result.stderr}"
    )


def test_guard_is_silent_when_the_macro_is_one(tmp_path):
    """Coverage 2 -- with the macro defined to 1 (the value the ARM build
    now supplies via CMakeLists.txt), the guard must be silent: exit 0, no
    error text in stderr."""
    compiler = _resolve_compiler()
    tu_path = _write_tu(tmp_path)
    expected_text = _expected_error_text()

    result = _preprocess(compiler, tu_path, define=1)

    assert result.returncode == 0, (
        f"expected a zero exit with the macro set to 1.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert expected_text not in result.stderr, (
        f"expected no #error text in stderr when the macro is 1.\n"
        f"Got stderr:\n{result.stderr}"
    )


def test_guard_fires_when_the_macro_is_zero(tmp_path):
    """Coverage 3 -- with the macro explicitly defined to 0, the guard must
    still fire: non-zero exit, error text present. This is the arm that
    distinguishes a real VALUE test (`!X`) from a mere DEFINED-NESS test
    (`!defined(X)` alone would treat 0 as configured, which is wrong)."""
    compiler = _resolve_compiler()
    tu_path = _write_tu(tmp_path)
    expected_text = _expected_error_text()

    result = _preprocess(compiler, tu_path, define=0)

    assert result.returncode != 0, (
        f"expected a non-zero exit with the macro explicitly set to 0 (the "
        f"guard must fire -- this is what distinguishes a value test from a "
        f"defined-ness-only test).\nstdout:\n{result.stdout}\n"
        f"stderr:\n{result.stderr}"
    )
    assert expected_text in result.stderr, (
        f"expected the fragment's own #error text in stderr.\n"
        f"Expected substring: {expected_text!r}\nGot stderr:\n{result.stderr}"
    )


def test_fragment_header_is_dependency_free():
    """Coverage 4 -- the fragment header's text contains no #include
    directive, so the three arms above are evaluating the real production
    header standalone, not a stand-in that happens to also compile."""
    text = _GUARD_HEADER.read_text()
    assert not re.search(r"^\s*#include\b", text, re.MULTILINE), (
        f"expected {_GUARD_HEADER} to be dependency-free (no #include "
        f"directive) -- hoisting only works if a host preprocessor can "
        f"evaluate this file standalone.\nGot:\n{text}"
    )


def test_board_header_does_not_define_what_it_tests():
    """Coverage 5 -- regression guard against the hollow shape returning:
    the board header must contain no top-level #define of
    RURP_PY32F071_PINMAP_CONFIGURED. If this fails, it means someone
    re-added a `#define RURP_PY32F071_PINMAP_CONFIGURED 1` (or an
    #ifndef-wrapped fallback) directly in the board header, which would
    make the fragment's guard permanently unfirable again -- the exact
    defect this plan repairs."""
    text = _BOARD_HEADER.read_text()
    assert not re.search(rf"^\s*#define\s+{_MACRO_NAME}\b", text, re.MULTILINE), (
        f"expected {_BOARD_HEADER} to contain NO #define of {_MACRO_NAME} -- "
        f"the board header must only TEST this macro (via "
        f"py32f071_pinmap_guard.h), never define it itself. Finding a "
        f"#define here means the hollow guard defect has returned: the "
        f"macro would once again be defined by the very file that tests it, "
        f"so the guard could never fire under any build configuration."
    )


def test_compiler_is_required_not_optional():
    """Coverage 6 -- this module's own source contains no skip decorator and
    no skip call anywhere, so the fail-closed contract in _resolve_compiler
    is self-enforcing and cannot be silently bypassed by a future edit.

    The two needle strings below are built via concatenation (not written
    verbatim) so this test's own assertion text does not trip its own
    check -- the literal substrings must appear NOWHERE in this file,
    including inside this test's failure messages."""
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- the "
        "compiler-absence case must FAIL, never SKIP."
    )
    assert skipif_marker not in own_text, (
        "expected no @pytest." + skipif_marker + " decorator anywhere in "
        "this module -- the compiler-absence case must FAIL, never SKIP."
    )
