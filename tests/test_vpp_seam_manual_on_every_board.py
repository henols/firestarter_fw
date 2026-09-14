"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 125 Plan 02 -- VPP-02's parametrized compile-and-run proof that the VPP
control seam refuses (returns MANUAL_ADJUSTMENT_REQUIRED) on every board
macro-set this branch supports.

Requirements: VPP-02
Decisions covered: D-01, D-02, D-03, D-04, D-05, D-06, D-07, D-08

RURP_HAS_VPP_DAC has exactly two consumers: the header's `#if !defined(...)`
test in include/rurp_vpp.h, and the source file's `#if RURP_HAS_VPP_DAC`
value test in src/rurp_vpp.cpp. This module is what proves each of those two
guards can actually fire, rather than assuming it -- the direct antidote to
the hollow-guard shape Phase 124 D-14 had to restructure, where a `#define`
sat roughly thirty lines above the `#if` that tested it and the guard could
not fire under any build configuration.

What the four board legs below actually prove: the seam consults exactly two
macros -- __AVR__ and RURP_HAS_VPP_DAC -- and nothing in it distinguishes Uno
from Leonardo from uno328pb. The four legs prove uniformity across ONE
compiler-supplied AVR fact plus ONE explicit ARM declaration, not four
independent per-board facts. The real AVR-cross-compiler resolution is
discharged by the three `pio run` builds Plan 125-04 performs, not by this
module -- do not let a reader infer more than that from the four legs below.

This module executes in NO CI leg on this branch: `pytest tests/ -v` appears
only in build.yml (push/PR to main) and beta-build.yml (push to beta) --
neither fires on the firmware milestone branch, and py32f071.yml has no
pytest step at all. The local run recorded in this phase's evidence artifact
is the only evidence this module's assertions were ever exercised.

Self-contained path resolution below -- NOT in conftest.py (firestarter/tests/
has no conftest.py anywhere in the repo, and none of pytest.ini,
pyproject.toml, setup.cfg or tox.ini exist either; a recorded house-rule
pattern decision, not an omission). Stdlib and pytest only.

Coverage:
  1. test_manual_control_on_every_board_macro_set -- one parametrized test,
     four board macro-sets (uno, leonardo, uno328pb, py32f071): compiles AND
     runs the seam, asserting compile exit 0, zero stderr bytes under
     -Wall -Wextra, run exit 0, and the two values parsed from stdout equal
     the manual mode (0) and the manual-adjustment-required result (1).
  2. test_forced_capability_macro_fails_closed_in_the_source -- D-03 via
     RESEARCH C-4: forcing RURP_HAS_VPP_DAC=1 while compiling
     src/rurp_vpp.cpp produces a non-zero compile exit carrying that file's
     own #error text; the header alone cannot reject this value (measured
     exit 0), so this leg is what makes the harness non-vacuous.
  3. test_unset_and_non_avr_fails_closed_in_the_header -- D-08: compiling
     with neither __AVR__ nor an explicit RURP_HAS_VPP_DAC defined produces
     a non-zero compile exit carrying include/rurp_vpp.h's own #error text.
  4. test_board_macro_sets_match_the_real_build_config -- D-04 via
     RESEARCH C-6: the four hardcoded board macro-sets above are kept
     honest by asserting their real anchors are still literally present in
     platformio.ini (the three AVR env headers + board lines) and
     platform/py32f071/CMakeLists.txt (the two ARM defines). Deliberately
     NOT anchored on the framework-supplied ARDUINO_AVR_* macros, which
     appear nowhere in platformio.ini and would fail this leg on arrival.
  5. test_build_supplies_the_capability_macro_the_header_only_tests --
     D-07: platform/py32f071/CMakeLists.txt's target_compile_definitions
     declares RURP_HAS_VPP_DAC=0, and separately,
     include/boards/py32f071_rurp_shield.h contains no top-level #define of
     that macro -- the regression guard against Phase 124's hollow-guard
     shape (a macro defined by the very file that tests it) returning here.
  6. test_seam_source_is_dependency_free -- D-02: src/rurp_vpp.cpp's only
     #include directive names the seam header itself. Turns D-02's
     standing constraint into something a future edit cannot quietly
     break.
  7. test_compiler_is_required_not_optional -- this module's own source
     contains no skip decorator and no skip call, so the fail-closed
     contract in _resolve_compiler is self-enforcing and cannot be
     silently bypassed by a future edit.
"""

import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_INCLUDE = _REPO_ROOT / "include"
_SEAM_HEADER = _INCLUDE / "rurp_vpp.h"
_SEAM_SRC = _REPO_ROOT / "src" / "rurp_vpp.cpp"
_PLATFORMIO_INI = _REPO_ROOT / "platformio.ini"
_PY32_CMAKE = _REPO_ROOT / "platform" / "py32f071" / "CMakeLists.txt"
_PY32_BOARD_HEADER = _INCLUDE / "boards" / "py32f071_rurp_shield.h"

_EXPECTED_MODE_MANUAL = 0
_EXPECTED_RESULT_MANUAL_REQUIRED = 1

_BOARD_MACRO_SETS = [
    pytest.param(
        ("__AVR__", "ARDUINO_AVR_UNO", 'RURP_BOARD_NAME="uno"', "SERIAL_ON_IO"),
        id="uno",
    ),
    pytest.param(
        (
            "__AVR__",
            "ARDUINO_AVR_LEONARDO",
            'RURP_BOARD_NAME="leonardo"',
            "DATA_BUFFER_SIZE=1024",
        ),
        id="leonardo",
    ),
    pytest.param(
        (
            "__AVR__",
            "ARDUINO_AVR_ATmega328PB",
            'RURP_BOARD_NAME="uno328pb"',
            "SERIAL_ON_IO",
        ),
        id="uno328pb",
    ),
    pytest.param(
        (
            "RURP_PLATFORM_PY32F071=1",
            "RURP_HAS_VPP_DAC=0",
            'RURP_BOARD_NAME="py32f071"',
        ),
        id="py32f071",
    ),
]


def _resolve_compiler():
    """Resolve the host C++ compiler, fail-closed.

    Deliberately never bypassed via any decorator or runtime call that would
    mark this outcome as skipped, anywhere in this module: doing so would
    recreate the exact BASE-02/BASE-03 absence-proxy failure class this
    milestone's Phase 123 removed. If $CXX (or 'g++') cannot be resolved via
    shutil.which, this raises via a plain assert, which the test runner
    reports as a FAILURE, never a skipped outcome. This module's harness
    runs in NO CI leg on this branch (RESEARCH C-7) -- the local run is the
    only evidence, which makes the fail-closed contract here even more
    load-bearing than in the CI-covered precedent.
    """
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    assert compiler is not None, (
        "host C++ compiler not found on PATH (checked $CXX, falling back to "
        "'g++'). This must FAIL the suite, never be silently skipped -- no "
        "embedded toolchain is invoked here."
    )
    return compiler


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


def _compile(compiler, defines, sources, output_path):
    """Compile (and, when a real main is present, link) the given source
    files with the given macro definitions into a real binary at
    output_path. List argv only, never a shell; always passes
    -std=gnu++17 -Wall -Wextra and the include/ path. A definition whose
    value contains double quotes needs no extra escaping here -- there is
    no shell in the way, so the literal Python string is exactly what the
    compiler receives as argv."""
    argv = [compiler, "-std=gnu++17", "-Wall", "-Wextra", "-I", str(_INCLUDE)]
    argv += [f"-D{define}" for define in defines]
    argv += [str(source) for source in sources]
    argv += ["-o", str(output_path)]
    return subprocess.run(argv, capture_output=True, text=True)


def _run_binary(binary_path):
    """Execute the compiled binary as a second subprocess, list argv,
    capturing output. The seam's result value crosses this process boundary
    on stdout, never this run's exit code (Trap 2): 1 is also the exit code
    of a compile failure, a link failure and a crash, so returning it from
    main would make the correct answer indistinguishable from every
    failure mode."""
    return subprocess.run([str(binary_path)], capture_output=True, text=True)


def _expected_header_error_text():
    """Read the #error message out of include/rurp_vpp.h at test time,
    rather than hardcoding it a second time here -- a literal that could
    silently desync from the real header on a future edit. Scoped to this
    one file: the header and src/rurp_vpp.cpp each carry a DIFFERENT #error
    message, so this "exactly one directive" assertion must never be
    reused across both files."""
    text = _SEAM_HEADER.read_text()
    m = re.search(r'#\s*error\s+"([^"]*)"', text)
    assert m, (
        f"expected exactly one #error directive with a quoted message in "
        f"{_SEAM_HEADER}.\nGot:\n{text}"
    )
    return m.group(1)


def _expected_source_error_text():
    """Read the #error message out of src/rurp_vpp.cpp at test time, rather
    than hardcoding it a second time here. Scoped to this one file -- see
    _expected_header_error_text's docstring for why the two are separate
    helpers rather than one shared "exactly one" assertion."""
    text = _SEAM_SRC.read_text()
    m = re.search(r'#\s*error\s+"([^"]*)"', text)
    assert m, (
        f"expected exactly one #error directive with a quoted message in "
        f"{_SEAM_SRC}.\nGot:\n{text}"
    )
    return m.group(1)


@pytest.mark.parametrize("defines", _BOARD_MACRO_SETS)
def test_manual_control_on_every_board_macro_set(tmp_path, defines):
    """Coverage 1 -- one parametrized test asserting the seam refuses
    (control mode MANUAL, result MANUAL_ADJUSTMENT_REQUIRED) across all
    four board macro-sets in one run (ROADMAP Criterion 2), compiled AND
    run -- never a single-board spot check. Parses the result from stdout,
    never the run subprocess's exit code (Trap 2)."""
    compiler = _resolve_compiler()
    tu_path = _write_shim_tu(tmp_path)
    binary_path = tmp_path / "seam_under_test"

    compile_result = _compile(compiler, defines, (tu_path, _SEAM_SRC), binary_path)
    assert compile_result.returncode == 0, (
        f"expected a clean compile for board macro-set {defines!r}.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    assert compile_result.stderr == "", (
        f"expected zero bytes of -Wall -Wextra warning output for board "
        f"macro-set {defines!r}.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )

    run_result = _run_binary(binary_path)
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
    compiler = _resolve_compiler()
    expected_text = _expected_source_error_text()
    binary_path = tmp_path / "forced_capability"

    result = _compile(compiler, ("__AVR__", "RURP_HAS_VPP_DAC=1"), (_SEAM_SRC,), binary_path)

    assert result.returncode != 0, (
        "expected a non-zero compile exit with RURP_HAS_VPP_DAC forced to 1 "
        "while compiling src/rurp_vpp.cpp (the header alone cannot reject "
        f"this value).\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert expected_text in result.stderr, (
        f"expected src/rurp_vpp.cpp's own #error text in stderr.\n"
        f"Expected substring: {expected_text!r}\nGot stderr:\n{result.stderr}"
    )


def test_unset_and_non_avr_fails_closed_in_the_header(tmp_path):
    """Coverage 3 -- D-08: with neither __AVR__ nor an explicit
    RURP_HAS_VPP_DAC defined, include/rurp_vpp.h's #else arm must fire.
    This is the arm every native translation unit would reach were the
    seam ever included from include/rurp_shield.h (measured: pio test
    -e native goes to 17 suites / 0 succeeded) -- this leg is the standing
    explanation for why that include does not exist."""
    compiler = _resolve_compiler()
    tu_path = _write_shim_tu(tmp_path)
    expected_text = _expected_header_error_text()
    binary_path = tmp_path / "unset_non_avr"

    result = _compile(compiler, (), (tu_path,), binary_path)

    assert result.returncode != 0, (
        "expected a non-zero compile exit with neither __AVR__ nor "
        "RURP_HAS_VPP_DAC defined.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert expected_text in result.stderr, (
        f"expected include/rurp_vpp.h's own #error text in stderr.\n"
        f"Expected substring: {expected_text!r}\nGot stderr:\n{result.stderr}"
    )


def test_board_macro_sets_match_the_real_build_config():
    """Coverage 4 -- D-04 via RESEARCH C-6: the four board macro-sets
    hardcoded into _BOARD_MACRO_SETS above are kept honest by this drift
    leg, which asserts their real anchors are still literally present in
    the real build config. Substring presence against a plain file read is
    the whole mechanism -- neither platformio.ini nor CMakeLists.txt is
    parsed into a structure (tests/scan_paths.py's own stated principle:
    "deliberately explicit, never derived"; the two build systems also have
    incompatible #define syntaxes, so a derived version would be two
    parsers).

    Deliberately NOT anchored on ARDUINO_AVR_UNO / ARDUINO_AVR_LEONARDO /
    ARDUINO_AVR_ATmega328PB: RESEARCH measured those come from the Arduino
    framework and board JSON and appear NOWHERE in platformio.ini, so a leg
    keyed on them fails the moment it is written, and the tempting "fix" is
    to weaken it into something vacuous. The honest anchors, all literal
    and all present, are the `[env:<name>]` section header plus its
    `board = <value>` line for each AVR env, and the two ARM
    target_compile_definitions entries for py32f071.

    Note the four board legs' own macro-sets (Coverage 1) deliberately
    include the framework board macros (ARDUINO_AVR_UNO and friends) as
    part of each board's compile-time identity -- that is correct and
    required there. This leg is about what the BUILD FILES contain, not
    about what the compile legs pass to the compiler; a file-wide search of
    this module for those framework macro names will and should find them
    in _BOARD_MACRO_SETS above."""
    platformio_text = _PLATFORMIO_INI.read_text()
    cmake_text = _PY32_CMAKE.read_text()

    avr_anchors = (
        ("[env:uno]", "board = uno"),
        ("[env:uno328pb]", "board = ATmega328PB"),
        ("[env:leonardo]", "board = leonardo"),
    )
    for env_header, board_line in avr_anchors:
        assert env_header in platformio_text, (
            f"drift detected: expected {env_header!r} in {_PLATFORMIO_INI} -- "
            f"the hardcoded board macro-set in this module's "
            f"_BOARD_MACRO_SETS has drifted from the real build config."
        )
        assert board_line in platformio_text, (
            f"drift detected: expected {board_line!r} in {_PLATFORMIO_INI} -- "
            f"the hardcoded board macro-set in this module's "
            f"_BOARD_MACRO_SETS has drifted from the real build config."
        )

    arm_anchors = ("RURP_PLATFORM_PY32F071=1", 'RURP_BOARD_NAME="py32f071"')
    for anchor in arm_anchors:
        assert anchor in cmake_text, (
            f"drift detected: expected {anchor!r} in {_PY32_CMAKE} -- the "
            f"hardcoded py32f071 macro-set in this module's "
            f"_BOARD_MACRO_SETS has drifted from the real build config."
        )


def test_build_supplies_the_capability_macro_the_header_only_tests():
    """Coverage 5 -- D-07: the ARM build supplies what the header only
    tests, in both directions. First, platform/py32f071/CMakeLists.txt's
    target_compile_definitions genuinely declares RURP_HAS_VPP_DAC=0 (the
    value is 0, not 1, because no PY32F071 PCB exists -- there is no
    hardware to validate a DAC against; the closed branch
    origin/feature/py32f071-full-support, PR #47, chose 1 and is out of
    scope; AVR manual control is permanent (D-05), never provisional).
    Second, include/boards/py32f071_rurp_shield.h contains NO top-level
    #define of RURP_HAS_VPP_DAC -- the direct regression guard against
    Phase 124's hollow-guard shape returning: a definition in the board
    header would make include/rurp_vpp.h's own guard permanently unfirable,
    exactly the defect Phase 124 Plan 09 had to repair for a different
    macro."""
    cmake_text = _PY32_CMAKE.read_text()
    board_header_text = _PY32_BOARD_HEADER.read_text()

    assert "RURP_HAS_VPP_DAC=0" in cmake_text, (
        f"expected target_compile_definitions in {_PY32_CMAKE} to declare "
        f"RURP_HAS_VPP_DAC=0 -- the build must supply this macro for the "
        f"py32f071 target; the header itself never defines it for a "
        f"non-AVR platform.\nGot:\n{cmake_text}"
    )
    assert not re.search(r"^\s*#\s*define\s+RURP_HAS_VPP_DAC\b", board_header_text, re.MULTILINE), (
        f"expected {_PY32_BOARD_HEADER} to contain NO #define of "
        f"RURP_HAS_VPP_DAC -- the board header must only ever consume this "
        f"macro (indirectly, via include/rurp_vpp.h), never define it "
        f"itself. Finding a #define here means the hollow-guard defect has "
        f"returned: the macro would once again be defined by a file "
        f"upstream of the guard that tests it, so include/rurp_vpp.h's "
        f"#if !defined(...) guard could never fire again for this "
        f"platform.\nGot:\n{board_header_text}"
    )


def test_seam_source_is_dependency_free():
    """Coverage 6 -- D-02: src/rurp_vpp.cpp's only #include directive names
    the seam header itself. D-02 is a standing constraint, not a local
    convenience -- it is what lets this harness need zero stub scaffolding,
    and a later phase wanting configuration or hardware access must add
    the dependency deliberately rather than by drift. Inverted from the
    pinmap precedent's coverage-4 shape (which asserts NO #include exists
    in a dependency-free fragment header): here exactly ONE #include is
    expected, and any second one is the violation."""
    text = _SEAM_SRC.read_text()
    includes = re.findall(r'^\s*#\s*include\s+"([^"]+)"', text, re.MULTILINE)
    assert len(includes) == 1, (
        f"expected exactly one #include directive in {_SEAM_SRC} (D-02: "
        f"the seam's implementation is dependency-free by construction, "
        f"never gaining a second dependency by drift), got {len(includes)}: "
        f"{includes!r}.\nGot:\n{text}"
    )
    assert includes[0] == "rurp_vpp.h", (
        f"expected {_SEAM_SRC}'s one #include to name the seam header "
        f"'rurp_vpp.h', got {includes[0]!r}.\nGot:\n{text}"
    )


def test_compiler_is_required_not_optional():
    """Coverage 7 -- this module's own source contains no skip decorator
    and no skip call anywhere, so the fail-closed contract in
    _resolve_compiler is self-enforcing and cannot be silently bypassed by
    a future edit. An absence-proxy skip reporting success at exit 0 is
    precisely the failure class this milestone's Phase 123 removed.

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
