"""
Project Name: Firestarter
Copyright (c) 2025 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 126 Plan 02 -- CFG-04's AVR regression test, authored against the
UNTOUCHED, pre-refactor `src/rurp_config_utils.cpp` (blob
6705fd46e07a2d359d161dc2e7728cb4e45f89c7 at the time this module was written).

Requirements: CFG-04
Decisions covered: D-01, D-04, D-06, D-07, D-08

This module is authored against the pre-refactor policy file ON PURPOSE. Its
own blob SHA is recorded in this phase's SUMMARY.md the moment it goes green,
so that Plan 126-03 -- which performs the CFG-03/D-07/D-08 split into a common
policy layer plus a per-platform two-function backend -- can re-hash exactly
that recorded SHA as its primary proof that the file did not change underneath
the split. A test written AFTER the refactor could only prove the new code is
self-consistent; it could never have observed the *before* state this module
is the sole witness to. This is D-04's two-commit discharge of ROADMAP
Criterion 3 ("behaviour identical to the pre-refactor code -- proven by an
empty git diff on the test file itself"): write and prove this module here
(commit 1, this plan); land the split later (commit 2, Plan 126-03); the proof
is that the recorded blob SHA re-hashes identical and this module is still
green against the post-refactor tree. A path-scoped `git diff --stat` is
corroboration only, never the primary proof -- see this phase's evidence
ledger for why (124-VERIFICATION.md records a live finding where exactly that
pipeline reported "(empty)" while a real change's trailer survived the grep).

This module executes in NO CI leg on this branch: `pytest tests/` runs only in
build.yml (push/PR to main) and beta-build.yml (push to beta); py32f071.yml
has no pytest step. The local run recorded in this phase's evidence artifact
is the only evidence this module's assertions were ever exercised.

Self-contained path resolution below -- NOT in conftest.py (firestarter/tests/
has no conftest.py anywhere in the repo, and none of pytest.ini,
pyproject.toml, setup.cfg or tox.ini exist either; a recorded house-rule
pattern decision, not an omission). Stdlib and pytest only -- no path under
the gitignored PlatformIO build-artifacts directory may appear anywhere in
this module (C-12): ArduinoFake ships an EEPROM.h, but only under that
gitignored per-env libdeps directory, a build artifact that exists solely
after a native build. Depending on it would make this module pass on a warm
tree and fail on a clean checkout, so the fake EEPROM.h below is
hand-written fresh into tmp_path on every call instead.

Coverage:
  1. test_pre_refactor_tu_compiles_and_runs_with_zero_warnings -- compiles
     and runs the resolved pre-refactor source(s) against the hand-written
     fake EEPROM.h, asserting a clean compile, zero bytes of -Wall -Wextra
     warning output, and a clean run.
  2. test_load_config_get_access_at_config_start_with_sizeof_length -- CFG-04:
     rurp_load_config() produces exactly one recorded get access, at index
     48, with a length equal to sizeof(rurp_configuration_t) as the compiled
     binary itself reports it -- never a Python integer literal (C-6).
  3. test_save_config_put_access_at_config_start_with_sizeof_length -- CFG-04:
     rurp_save_config() produces exactly one recorded put access, at index
     48, with the same symbolic length.
  4. test_validate_config_write_back_produces_put_on_version_mismatch --
     D-14's write-back at rurp_config_utils.cpp:38: rurp_validate_config() on
     a struct whose version does not match CONFIG_VERSION writes the
     defaults back and triggers exactly one put, at index 48.
  5. test_non_vacuity_source_resolved_and_access_recorded -- the two-way
     Discretion-default check: at least one candidate source path resolved
     AND rurp_load_config() recorded at least one access.
  6. test_module_references_no_pio_build_artifact_path -- C-12: this module's
     own text contains no reference to the gitignored PlatformIO
     build-artifacts path.
  7. test_compiler_is_required_not_optional -- this module's own source
     contains no skip decorator and no skip call, so the fail-closed
     contract in _resolve_compiler is self-enforcing and cannot be silently
     bypassed by a future edit (copied verbatim from
     tests/test_vpp_seam_manual_on_every_board.py:447-468).
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

_CANDIDATE_SOURCES = (
    _REPO_ROOT / "src" / "rurp_config_utils.cpp",
    _REPO_ROOT / "src" / "boards" / "rurp_config_storage_eeprom.cpp",
)
_RESOLVED_SOURCES = tuple(p for p in _CANDIDATE_SOURCES if p.is_file())

_CONFIG_START = 48

_ACCESS_RE = re.compile(r"^ACCESS (\w+) ([GP]) (\d+) (\d+)$")
_SIZEOF_RE = re.compile(r"^SIZEOF (\d+)$")


def _resolve_compiler():
    """Resolve the host C++ compiler, fail-closed.

    Deliberately never bypassed via any decorator or runtime call that would
    mark this outcome as skipped, anywhere in this module: doing so would
    recreate the exact BASE-02/BASE-03 absence-proxy failure class this
    milestone's Phase 123 removed. If $CXX (or 'g++') cannot be resolved via
    shutil.which, this raises via a plain assert, which the test runner
    reports as a FAILURE, never a skipped outcome. This module's harness
    runs in NO CI leg on this branch -- the local run is the only evidence,
    which makes the fail-closed contract here even more load-bearing than in
    the CI-covered precedent (tests/test_vpp_seam_manual_on_every_board.py).
    """
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    assert compiler is not None, (
        "host C++ compiler not found on PATH (checked $CXX, falling back to "
        "'g++'). This must FAIL the suite, never be silently skipped -- no "
        "embedded toolchain is invoked here."
    )
    return compiler


def _write_fake_eeprom_header(tmp_path):
    """Write a minimal fake EEPROM.h fresh into tmp_path, on every call --
    never a committed fixture header (C-12). Mirrors the real Arduino
    template signatures from
    framework-arduino-avr/libraries/EEPROM/src/EEPROM.h:130-142 (a `get`
    template returning T& and a `put` template returning const T&, both
    taking an int idx), records the pair (idx, sizeof(T)) per call plus
    whether it was a get or a put, and reproduces `put`'s per-byte
    `update()` semantics (read the byte, write only if it differs) so that
    the moved code's typed call keeps emitting an identical access -- a
    byte-loop reimplementation in the backend would be arguably equivalent
    but would not be a *move*.

    `g_eeprom_accesses` and `EEPROM` are declared `inline` (C++17) because
    both this file and the pre-refactor source(s) include this header, and
    an ordinary (non-inline) definition would violate the One Definition
    Rule the moment two translation units are linked together.

    `eeprom_fake_reset_log()` is a test-support addition with no analog in
    the real library -- used only by the driver TU below to isolate each of
    the three phases' recorded accesses from one another. It is never
    called by the code under test.
    """
    header_path = tmp_path / "EEPROM.h"
    header_path.write_text(
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstring>\n"
        "#include <vector>\n"
        "\n"
        "struct EepromAccess {\n"
        "    char op;      // 'G' for get, 'P' for put\n"
        "    int idx;\n"
        "    size_t size;\n"
        "};\n"
        "\n"
        "inline std::vector<EepromAccess> g_eeprom_accesses;\n"
        "inline unsigned char g_eeprom_storage[512];\n"
        "\n"
        "class EEPROMClass {\n"
        "public:\n"
        "    template <typename T>\n"
        "    T &get(int idx, T &t) {\n"
        "        g_eeprom_accesses.push_back(EepromAccess{'G', idx, sizeof(T)});\n"
        "        std::memcpy(&t, g_eeprom_storage + idx, sizeof(T));\n"
        "        return t;\n"
        "    }\n"
        "\n"
        "    template <typename T>\n"
        "    const T &put(int idx, const T &t) {\n"
        "        g_eeprom_accesses.push_back(EepromAccess{'P', idx, sizeof(T)});\n"
        "        const unsigned char *src = reinterpret_cast<const unsigned char *>(&t);\n"
        "        for (size_t i = 0; i < sizeof(T); ++i) {\n"
        "            if (g_eeprom_storage[idx + i] != src[i]) {\n"
        "                g_eeprom_storage[idx + i] = src[i];\n"
        "            }\n"
        "        }\n"
        "        return t;\n"
        "    }\n"
        "};\n"
        "\n"
        "inline EEPROMClass EEPROM;\n"
        "\n"
        "inline void eeprom_fake_reset_log() {\n"
        "    g_eeprom_accesses.clear();\n"
        "}\n"
    )
    return header_path


def _write_driver_tu(tmp_path):
    """Write a fresh driver main into tmp_path, on every call -- never a
    committed fixture translation unit. Exercises rurp_load_config(),
    rurp_save_config() and rurp_validate_config() against the policy layer
    in three isolated phases (the fake EEPROM.h's per-call log is reset
    between phases so each phase's printed lines report exactly the
    access(es) that one call produced), then prints one machine-parsable
    'ACCESS <phase> <op> <idx> <size>' line per recorded access plus one
    'SIZEOF <n>' summary line carrying sizeof(rurp_configuration_t) as this
    binary itself computed it. The result crosses on stdout, never as the
    process exit code -- 1 is also the exit code of a compile failure, a
    link failure and a crash, so returning a result through it would make
    the correct answer indistinguishable from every failure mode. main
    returns 0 unconditionally."""
    tu_path = tmp_path / "config_storage_regression_main.cpp"
    tu_path.write_text(
        '#include "rurp_shield.h"\n'
        "#include <EEPROM.h>\n"
        "#include <cstdio>\n"
        "#include <cstring>\n"
        "\n"
        "static void print_accesses(const char *phase) {\n"
        "    for (const auto &a : g_eeprom_accesses) {\n"
        '        printf("ACCESS %s %c %d %zu\\n", phase, a.op, a.idx, a.size);\n'
        "    }\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    rurp_configuration_t *config = rurp_get_config();\n"
        "\n"
        "    // Seed BOTH the fake's backing storage AND the live global\n"
        "    // config directly (never through EEPROM.get/put, so this\n"
        "    // setup step is not itself a recorded access) with a config\n"
        "    // whose version already matches CONFIG_VERSION. Seeding both\n"
        "    // copies means an unmutated rurp_load_config() overwrites the\n"
        "    // live global with an identical value read via EEPROM.get\n"
        "    // (one G access, no cascade), while a mutation that removes\n"
        "    // the get call leaves the live global's already-matching seed\n"
        "    // untouched -- so the internal validate call still does not\n"
        "    // cascade into a write-back, and the load phase's access list\n"
        "    // is genuinely EMPTY rather than masked by an indirect put.\n"
        "    // This is what makes the non-vacuity leg meaningful against\n"
        "    // the no-access mutation.\n"
        "    rurp_configuration_t seed;\n"
        "    memset(&seed, 0, sizeof(seed));\n"
        "    strcpy(seed.version, CONFIG_VERSION);\n"
        "    seed.r1 = VALUE_R1;\n"
        "    seed.r2 = VALUE_R2;\n"
        "    seed.hardware_revision = 0;\n"
        "    memcpy(g_eeprom_storage + 48, &seed, sizeof(seed));\n"
        "    memcpy(config, &seed, sizeof(seed));\n"
        "\n"
        "    // Phase 1: rurp_load_config() -- exactly one get, no put\n"
        "    // (the seed's version matches, so the internal validate call\n"
        "    // does not itself trigger a write-back).\n"
        "    eeprom_fake_reset_log();\n"
        "    rurp_load_config();\n"
        '    print_accesses("load");\n'
        "\n"
        "    // Phase 2: rurp_save_config() -- exactly one put.\n"
        "    eeprom_fake_reset_log();\n"
        "    rurp_save_config(config);\n"
        '    print_accesses("save");\n'
        "\n"
        "    // Phase 3: rurp_validate_config() on a struct whose version\n"
        "    // does not match CONFIG_VERSION -- must write the defaults\n"
        "    // back and trigger exactly one put (the D-14 write-back at\n"
        "    // rurp_config_utils.cpp:38).\n"
        "    rurp_configuration_t mismatched;\n"
        "    memset(&mismatched, 0, sizeof(mismatched));\n"
        '    strcpy(mismatched.version, "XXXXX");\n'
        "    eeprom_fake_reset_log();\n"
        "    rurp_validate_config(&mismatched);\n"
        '    print_accesses("validate");\n'
        "\n"
        '    printf("SIZEOF %zu\\n", sizeof(rurp_configuration_t));\n'
        "    return 0;\n"
        "}\n"
    )
    return tu_path


def _compile(compiler, sources, include_dirs, output_path):
    """Compile and link the given source files into a real binary at
    output_path. List argv only, never a shell; always passes
    -std=gnu++17 -Wall -Wextra and an -I entry for every directory in
    include_dirs (the repo include/ directory AND tmp_path, so the fake
    EEPROM.h shadows nothing real)."""
    argv = [compiler, "-std=gnu++17", "-Wall", "-Wextra", "-DARDUINO_AVR_UNO"]
    for include_dir in include_dirs:
        argv += ["-I", str(include_dir)]
    argv += [str(source) for source in sources]
    argv += ["-o", str(output_path)]
    return subprocess.run(argv, capture_output=True, text=True)


def _run_binary(binary_path):
    """Execute the compiled binary as a second subprocess, list argv,
    capturing output. The result crosses this process boundary on stdout,
    never this run's exit code (Trap 2): 1 is also the exit code of a
    compile failure, a link failure and a crash, so returning it from main
    would make the correct answer indistinguishable from every failure
    mode."""
    return subprocess.run([str(binary_path)], capture_output=True, text=True)


def _run_regression_harness(tmp_path):
    """Compile and run the resolved pre-refactor source(s) plus the fresh
    fake EEPROM.h and driver TU, and parse stdout into a dict of
    {"load": [...], "save": [...], "validate": [...], "sizeof": int}, each
    list holding (op, idx, length) tuples in the order printed. Returns
    (compile_result, run_result, parsed); run_result is None if the compile
    failed."""
    compiler = _resolve_compiler()
    _write_fake_eeprom_header(tmp_path)
    driver_tu = _write_driver_tu(tmp_path)
    binary_path = tmp_path / "config_storage_regression"

    compile_result = _compile(
        compiler,
        (*_RESOLVED_SOURCES, driver_tu),
        (_INCLUDE, tmp_path),
        binary_path,
    )

    run_result = None
    parsed = {"load": [], "save": [], "validate": [], "sizeof": None}
    if compile_result.returncode == 0:
        run_result = _run_binary(binary_path)
        for line in run_result.stdout.splitlines():
            match = _ACCESS_RE.match(line)
            if match:
                phase, op, idx, length = match.groups()
                parsed.setdefault(phase, []).append((op, int(idx), int(length)))
                continue
            match = _SIZEOF_RE.match(line)
            if match:
                parsed["sizeof"] = int(match.group(1))

    return compile_result, run_result, parsed


def test_pre_refactor_tu_compiles_and_runs_with_zero_warnings(tmp_path):
    """Coverage 1 -- compiles and runs the resolved pre-refactor source(s)
    against the hand-written fake EEPROM.h, asserting a clean compile, zero
    bytes of -Wall -Wextra warning output (not merely exit 0), and a clean
    run."""
    compile_result, run_result, _parsed = _run_regression_harness(tmp_path)
    assert compile_result.returncode == 0, (
        f"expected a clean compile of {_RESOLVED_SOURCES!r}.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    assert compile_result.stderr == "", (
        "expected zero bytes of -Wall -Wextra warning output.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    assert run_result is not None and run_result.returncode == 0, (
        "expected the compiled binary to run cleanly.\n"
        f"stdout:\n{getattr(run_result, 'stdout', None)}\n"
        f"stderr:\n{getattr(run_result, 'stderr', None)}"
    )


def test_load_config_get_access_at_config_start_with_sizeof_length(tmp_path):
    """Coverage 2 -- CFG-04: rurp_load_config() produces exactly one
    recorded get access, at index 48, with a length equal to
    sizeof(rurp_configuration_t) as the compiled binary itself reported it.
    Compared against the binary's own reported sizeof, never against a
    Python integer literal -- host `long` is 8 bytes so the value is 32
    here, 15 under avr-g++ and 20 on ARM (C-6)."""
    compile_result, run_result, parsed = _run_regression_harness(tmp_path)
    assert compile_result.returncode == 0 and compile_result.stderr == "", (
        f"expected a clean, warning-free compile.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    sizeof_value = parsed["sizeof"]
    assert sizeof_value is not None, (
        f"expected a 'SIZEOF <n>' line in stdout.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )
    assert parsed["load"] == [("G", _CONFIG_START, sizeof_value)], (
        f"expected exactly one get access at index {_CONFIG_START} with "
        f"length sizeof(rurp_configuration_t)={sizeof_value} from "
        f"rurp_load_config(), got {parsed['load']!r}.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )


def test_save_config_put_access_at_config_start_with_sizeof_length(tmp_path):
    """Coverage 3 -- CFG-04: rurp_save_config() produces exactly one
    recorded put access, at index 48, with the same symbolic length."""
    compile_result, run_result, parsed = _run_regression_harness(tmp_path)
    assert compile_result.returncode == 0 and compile_result.stderr == "", (
        f"expected a clean, warning-free compile.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    sizeof_value = parsed["sizeof"]
    assert sizeof_value is not None, (
        f"expected a 'SIZEOF <n>' line in stdout.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )
    assert parsed["save"] == [("P", _CONFIG_START, sizeof_value)], (
        f"expected exactly one put access at index {_CONFIG_START} with "
        f"length sizeof(rurp_configuration_t)={sizeof_value} from "
        f"rurp_save_config(), got {parsed['save']!r}.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )


def test_validate_config_write_back_produces_put_on_version_mismatch(tmp_path):
    """Coverage 4 -- the D-14 write-back at rurp_config_utils.cpp:38:
    rurp_validate_config() on a struct whose version does not match
    CONFIG_VERSION writes the defaults back and triggers exactly one put,
    at index 48 with the same symbolic length -- this must behave
    identically after the split."""
    compile_result, run_result, parsed = _run_regression_harness(tmp_path)
    assert compile_result.returncode == 0 and compile_result.stderr == "", (
        f"expected a clean, warning-free compile.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    sizeof_value = parsed["sizeof"]
    assert sizeof_value is not None, (
        f"expected a 'SIZEOF <n>' line in stdout.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )
    assert parsed["validate"] == [("P", _CONFIG_START, sizeof_value)], (
        f"expected exactly one put access at index {_CONFIG_START} with "
        f"length sizeof(rurp_configuration_t)={sizeof_value} from "
        f"rurp_validate_config() on a version mismatch, got "
        f"{parsed['validate']!r}.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )


def test_non_vacuity_source_resolved_and_access_recorded(tmp_path):
    """Coverage 5 -- the two-way Discretion-default check: at least one
    candidate source path resolved AND rurp_load_config() recorded at
    least one access. Deliberately scoped to the load phase alone rather
    than a sum across all three phases: the driver seeds BOTH the fake's
    backing storage and the live global config with matching data, so an
    unmutated rurp_load_config() records exactly one get and a mutation
    that removes the get call leaves the load phase's list genuinely
    EMPTY (the seeded global already matches, so the internal validate
    call has nothing to write back) -- rather than being masked by an
    indirect write-back put, which is what would happen were this scoped
    to a sum across all phases (the save and validate phases run their
    own, unrelated calls regardless of what this phase's mutation does).
    This is the leg that catches a silently-absent access -- an
    exit-code-only test would report a no-op harness as green."""
    assert len(_RESOLVED_SOURCES) >= 1, (
        f"expected at least one of {_CANDIDATE_SOURCES!r} to resolve at "
        f"collection time; resolved none."
    )
    compile_result, run_result, parsed = _run_regression_harness(tmp_path)
    assert compile_result.returncode == 0 and compile_result.stderr == "", (
        f"expected a clean, warning-free compile.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    assert len(parsed["load"]) > 0, (
        "expected rurp_load_config() to record at least one EEPROM "
        "access; recorded none -- a test that passed here would be "
        "exactly the silently-absent-access shape this leg exists to "
        "catch.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )


def test_module_references_no_pio_build_artifact_path():
    """Coverage 6 -- C-12: this module's own text contains no reference to
    the gitignored PlatformIO build-artifacts path. The needle string below
    is built via concatenation (not written verbatim) so this test's own
    assertion text does not trip its own check -- see
    test_compiler_is_required_not_optional for the same trick applied to
    the skip-call needles."""
    own_text = Path(__file__).read_text()
    forbidden_path_fragment = "." + "pio" + "/"
    assert forbidden_path_fragment not in own_text, (
        "expected no reference to a " + forbidden_path_fragment + " build "
        "artifact path anywhere in this module (C-12) -- depending on "
        "ArduinoFake's gitignored EEPROM.h under that path would make this "
        "module pass on a warm tree and fail on a clean checkout."
    )


def test_compiler_is_required_not_optional():
    """Coverage 7 -- this module's own source contains no skip decorator
    and no skip call anywhere, so the fail-closed contract in
    _resolve_compiler is self-enforcing and cannot be silently bypassed by
    a future edit. An absence-proxy skip reporting success at exit 0 is
    precisely the failure class this milestone's Phase 123 removed.
    Copied verbatim (docstring aside) from
    tests/test_vpp_seam_manual_on_every_board.py:447-468.

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
