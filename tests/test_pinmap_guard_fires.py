"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson
Permission is hereby granted under MIT license.

Preprocess the real PY32 pin-map guard with missing, valid and invalid definitions and assert the compiler's observable success or refusal.
"""


import os
import shutil
import subprocess
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_INCLUDE_BOARDS = _REPO_ROOT / "include" / "boards"

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


def _has_guard_error(stderr):
    """True when one compiler diagnostic line is an #error directive that
    names the pin-map macro."""
    return any("#error" in line and _MACRO_NAME in line for line in stderr.splitlines())


def test_guard_fires_when_the_macro_is_unset(tmp_path):
    """Coverage 1 -- with RURP_PY32F071_PINMAP_CONFIGURED left entirely
    unset, the guard must fire: non-zero exit, the fragment's #error, naming
    the macro, in stderr."""
    compiler = _resolve_compiler()
    tu_path = _write_tu(tmp_path)

    result = _preprocess(compiler, tu_path, define=None)

    assert result.returncode != 0, (
        f"expected a non-zero exit with the macro unset (the guard must "
        f"fire).\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert _has_guard_error(result.stderr), (
        f"expected an #error diagnostic that names {_MACRO_NAME} in stderr.\n"
        f"Got stderr:\n{result.stderr}"
    )


def test_guard_is_silent_when_the_macro_is_one(tmp_path):
    """Coverage 2 -- with the macro defined to 1 (the value the ARM build
    now supplies via CMakeLists.txt), the guard must be silent: exit 0, no
    error text in stderr."""
    compiler = _resolve_compiler()
    tu_path = _write_tu(tmp_path)

    result = _preprocess(compiler, tu_path, define=1)

    assert result.returncode == 0, (
        f"expected a zero exit with the macro set to 1.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "#error" not in result.stderr, (
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

    result = _preprocess(compiler, tu_path, define=0)

    assert result.returncode != 0, (
        f"expected a non-zero exit with the macro explicitly set to 0 (the "
        f"guard must fire -- this is what distinguishes a value test from a "
        f"defined-ness-only test).\nstdout:\n{result.stdout}\n"
        f"stderr:\n{result.stderr}"
    )
    assert _has_guard_error(result.stderr), (
        f"expected an #error diagnostic that names {_MACRO_NAME} in stderr.\n"
        f"Got stderr:\n{result.stderr}"
    )
