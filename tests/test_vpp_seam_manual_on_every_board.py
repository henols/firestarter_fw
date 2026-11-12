"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson
Permission is hereby granted under MIT license.

Compile and execute the production VPP seam under supported board macro sets; assert manual-only behavior and fail-closed compilation.

Board macro sets come from tools that resolve the build configuration, never
from a scan of platformio.ini or CMakeLists.txt:

- AVR envs: `pio project config --json-output` (interpolated build_flags).
- py32f071: the compile_commands.json of a real CMake configure of
  platform/py32f071 (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`), named by the
  FIRESTARTER_PY32_COMPILE_COMMANDS environment variable. Without it, the
  py32f071 case is skipped, because the configure needs the ARM toolchain
  and the SDK download.
"""


import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

import pytest

from ._host_build import INCLUDE, REPO_ROOT, host_cxx, run_binary, run_compiler

_SEAM_SRC = REPO_ROOT / "src" / "rurp_vpp.cpp"
_PY32_COMPILE_COMMANDS_ENV = "FIRESTARTER_PY32_COMPILE_COMMANDS"

_EXPECTED_MODE_MANUAL = 0
_EXPECTED_RESULT_MANUAL_REQUIRED = 1


def _defines_from_argv(tokens):
    """The -D definitions in a compiler argv, one token per argument."""
    defines = []
    for index, token in enumerate(tokens):
        if token == "-D" and index + 1 < len(tokens):
            defines.append(tokens[index + 1])
        elif token.startswith("-D") and len(token) > 2:
            defines.append(token[2:])
    return defines


def _avr_macro_sets():
    """One macro set per atmelavr env, from `pio project config
    --json-output`, plus `__AVR__`, which the AVR toolchain itself defines.
    Returns (params, error); error is set when PlatformIO cannot resolve the
    configuration, so the test fails instead of collecting nothing."""
    result = subprocess.run(
        [sys.executable, "-m", "platformio", "project", "config", "-d", str(REPO_ROOT), "--json-output"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return [], f"`pio project config` exited {result.returncode}.\nstderr:\n{result.stderr}"
    params = []
    for section, options in json.loads(result.stdout):
        options = dict(options)
        if not section.startswith("env:") or options.get("platform") != "atmelavr":
            continue
        tokens = [token for flag in options.get("build_flags", []) for token in shlex.split(flag)]
        defines = _defines_from_argv(tokens)
        params.append(pytest.param(("__AVR__", *defines), id=section[len("env:"):]))
    return params, None


def _py32_macro_set():
    """The definitions the py32 CMake build passes when it compiles
    src/rurp_vpp.cpp, read from the compile_commands.json that CMake wrote.
    A skip mark when the environment variable is not set; an error string
    (which fails the test) when the file has no entry for that source."""
    path = os.environ.get(_PY32_COMPILE_COMMANDS_ENV)
    if not path:
        return pytest.param(
            None,
            id="py32f071",
            marks=pytest.mark.skip(
                reason=(
                    f"{_PY32_COMPILE_COMMANDS_ENV} is not set: configure platform/py32f071 "
                    "with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON and point the variable at the "
                    "compile_commands.json to test the py32f071 macro set"
                )
            ),
        )
    for entry in json.loads(Path(path).read_text()):
        if (Path(entry["directory"]) / entry["file"]).resolve() == _SEAM_SRC:
            tokens = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
            return pytest.param(tuple(_defines_from_argv(tokens)), id="py32f071")
    return pytest.param(f"{path} has no entry for {_SEAM_SRC}", id="py32f071")


_AVR_MACRO_SETS, _AVR_CONFIG_ERROR = _avr_macro_sets()
_BOARD_MACRO_SETS = [*_AVR_MACRO_SETS, _py32_macro_set()]


def test_every_real_build_has_a_macro_set():
    """Non-vacuity: PlatformIO resolved the configuration, it has at least
    one AVR env, and every AVR env passes at least one -D besides __AVR__."""
    assert _AVR_CONFIG_ERROR is None, _AVR_CONFIG_ERROR
    assert _AVR_MACRO_SETS, "`pio project config` reported no atmelavr env"
    for param in _AVR_MACRO_SETS:
        assert len(param.values[0]) > 1, f"env {param.id} has no -D build flags: {param.values[0]!r}"


def _write_shim_tu(tmp_path):
    """Write the measured RESEARCH shim into tmp_path, fresh every call --
    never a committed fixture translation unit (D-03 declined one). Includes
    rurp_vpp.h and <cstdio>, and its main prints the control mode and the
    result of rurp_set_vpp_target_mv(12000, 200, 50) in the exact form
    'mode=%d result=%d', each cast to int, then calls
    rurp_disable_vpp_control() and returns 0 unconditionally."""
    tu_path = tmp_path / "vpp_seam_main.cpp"
    tu_path.write_text(
        '#include "rurp_vpp.h"\n'
        "#include <cstdio>\n"
        "int main(void) {\n"
        '    printf("mode=%d result=%d\\n", (int)rurp_vpp_control_mode(),\n'
        "           (int)rurp_set_vpp_target_mv(12000, 200, 50));\n"
        "    rurp_disable_vpp_control();\n"
        "    return 0;\n"
        "}\n"
    )
    return tu_path


def _compile(defines, sources, output_path):
    """Compile (and, when a real main is present, link) the given source
    files with the given macro definitions into a real binary at
    output_path; always with -std=gnu++17 -Wall -Wextra and the include/
    path. There is no shell in the way, so a definition whose value contains
    double quotes reaches the compiler exactly as written."""
    return run_compiler([
        host_cxx(), "-std=gnu++17", "-Wall", "-Wextra", "-I", str(INCLUDE),
        *(f"-D{define}" for define in defines),
        *(str(source) for source in sources),
        "-o", str(output_path),
    ])


def _has_error_directive_naming(stderr, text):
    """True when one compiler diagnostic line is an #error directive whose
    message contains text."""
    return any("#error" in line and text in line for line in stderr.splitlines())


@pytest.mark.parametrize("defines", _BOARD_MACRO_SETS)
def test_manual_control_on_every_board_macro_set(tmp_path, defines):
    """Coverage 1 -- one parametrized test asserting the seam refuses
    (control mode MANUAL, result MANUAL_ADJUSTMENT_REQUIRED) for every
    board macro set (each AVR env, plus py32f071 when its compile commands
    are supplied), compiled AND run. Parses the result from stdout, never
    the run subprocess's exit code (Trap 2)."""
    assert not isinstance(defines, str), defines
    tu_path = _write_shim_tu(tmp_path)
    binary_path = tmp_path / "seam_under_test"

    compile_result = _compile(defines, (tu_path, _SEAM_SRC), binary_path)
    assert compile_result.returncode == 0, (
        f"expected a clean compile for board macro-set {defines!r}.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    assert compile_result.stderr == "", (
        f"expected zero bytes of -Wall -Wextra warning output for board "
        f"macro-set {defines!r}.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )

    run_result = run_binary(binary_path)
    assert run_result.returncode == 0, (
        f"expected the compiled binary to run cleanly for board macro-set "
        f"{defines!r}.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )

    match = re.match(r"mode=(\d+) result=(\d+)\s*$", run_result.stdout)
    assert match, (
        f"expected stdout of the form 'mode=<int> result=<int>' for board "
        f"macro-set {defines!r}.\n"
        f"stdout:\n{run_result.stdout!r}\nstderr:\n{run_result.stderr!r}"
    )
    mode, result = int(match.group(1)), int(match.group(2))
    assert mode == _EXPECTED_MODE_MANUAL, (
        f"expected rurp_vpp_control_mode() == RURP_VPP_CONTROL_MANUAL "
        f"({_EXPECTED_MODE_MANUAL}) for board macro-set {defines!r}, got "
        f"mode={mode}.\nstdout:\n{run_result.stdout}"
    )
    assert result == _EXPECTED_RESULT_MANUAL_REQUIRED, (
        f"expected rurp_set_vpp_target_mv() == "
        f"RURP_VPP_MANUAL_ADJUSTMENT_REQUIRED "
        f"({_EXPECTED_RESULT_MANUAL_REQUIRED}) for board macro-set "
        f"{defines!r}, got result={result}.\nstdout:\n{run_result.stdout}"
    )


def test_forced_capability_macro_fails_closed_in_the_source(tmp_path):
    """Coverage 2 -- D-03 via RESEARCH C-4: the header's
    `#if !defined(...)` guard cannot reject an explicitly forced
    RURP_HAS_VPP_DAC=1 (measured: exits 0 with only the header present), so
    this leg compiles src/rurp_vpp.cpp itself -- whose own, separately
    authored #error is the one that must fire. This is what makes the
    harness non-vacuous: a header-only leg would pass vacuously and the
    "the macro is genuinely consulted" argument would evaporate."""
    binary_path = tmp_path / "forced_capability"

    result = _compile(("__AVR__", "RURP_HAS_VPP_DAC=1"), (_SEAM_SRC,), binary_path)

    assert result.returncode != 0, (
        "expected a non-zero compile exit with RURP_HAS_VPP_DAC forced to 1 "
        "while compiling src/rurp_vpp.cpp (the header alone cannot reject "
        f"this value).\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert _has_error_directive_naming(result.stderr, "RURP_HAS_VPP_DAC=1"), (
        "expected an #error diagnostic that names RURP_HAS_VPP_DAC=1 in stderr.\n"
        f"Got stderr:\n{result.stderr}"
    )


def test_unset_and_non_avr_fails_closed_in_the_header(tmp_path):
    """Coverage 3 -- D-08: with neither __AVR__ nor an explicit
    RURP_HAS_VPP_DAC defined, include/rurp_vpp.h's #else arm must fire.
    This is the arm every native translation unit would reach were the
    seam ever included from include/rurp_shield.h (measured: pio test
    -e native goes to 17 suites / 0 succeeded) -- this leg is the standing
    explanation for why that include does not exist."""
    tu_path = _write_shim_tu(tmp_path)
    binary_path = tmp_path / "unset_non_avr"

    result = _compile((), (tu_path,), binary_path)

    assert result.returncode != 0, (
        "expected a non-zero compile exit with neither __AVR__ nor "
        "RURP_HAS_VPP_DAC defined.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert _has_error_directive_naming(result.stderr, "RURP_HAS_VPP_DAC must be supplied"), (
        "expected an #error diagnostic that says RURP_HAS_VPP_DAC must be "
        f"supplied by the build in stderr.\nGot stderr:\n{result.stderr}"
    )
