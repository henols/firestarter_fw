"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson
Permission is hereby granted under MIT license.

Preprocess the real PY32 pin-map guard with missing, valid and invalid definitions and assert the compiler's observable success or refusal.
"""


import os

import pytest

from ._host_build import INCLUDE, host_cxx, run_compiler

_INCLUDE_BOARDS = INCLUDE / "boards"

_MACRO_NAME = "RURP_PY32F071_PINMAP_CONFIGURED"


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


def _preprocess(tu_path, define=None):
    """Run the host compiler in preprocess-only mode (-E) against the given
    translation unit, with the repository's include/boards directory on the
    include path. Output is discarded; stdout, stderr and returncode are
    captured."""
    argv = [host_cxx(), "-E", "-I", str(_INCLUDE_BOARDS)]
    if define is not None:
        argv += [f"-D{_MACRO_NAME}={define}"]
    argv += [str(tu_path), "-o", os.devnull]
    return run_compiler(argv)


def _has_guard_error(stderr):
    """True when one compiler diagnostic line is an #error directive that
    names the pin-map macro."""
    return any("#error" in line and _MACRO_NAME in line for line in stderr.splitlines())


# (define, should_fire). Unset must fire. 1 is the value the ARM build
# supplies via CMakeLists.txt and must be silent. 0 must still fire: it
# distinguishes a real VALUE test (`!X`) from a mere DEFINED-NESS test
# (`!defined(X)` alone would treat 0 as configured, which is wrong).
@pytest.mark.parametrize(
    ("define", "should_fire"),
    [(None, True), (1, False), (0, True)],
    ids=["unset", "one", "zero"],
)
def test_guard_fires_unless_the_macro_is_one(tmp_path, define, should_fire):
    result = _preprocess(_write_tu(tmp_path), define=define)

    if should_fire:
        assert result.returncode != 0, (
            f"expected a non-zero exit with the macro {define!r} (the guard "
            f"must fire).\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        assert _has_guard_error(result.stderr), (
            f"expected an #error diagnostic that names {_MACRO_NAME} in stderr.\n"
            f"Got stderr:\n{result.stderr}"
        )
    else:
        assert result.returncode == 0, (
            f"expected a zero exit with the macro set to {define!r}.\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        assert "#error" not in result.stderr, (
            f"expected no #error text in stderr when the macro is {define!r}.\n"
            f"Got stderr:\n{result.stderr}"
        )
