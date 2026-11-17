"""
Project Name: Firestarter
Copyright (c) 2025 Henrik Olsson
Permission is hereby granted under MIT license.

Compile and execute production configuration-persistence code with a fake EEPROM. Assert load/save/validation behavior, including byte offsets, record lengths and write-back.
"""


import copy
import re

import pytest

from ._host_build import INCLUDE, REPO_ROOT, host_cxx, run_binary, run_compiler

_SOURCES = (
    REPO_ROOT / "src" / "rurp_config_utils.cpp",
    REPO_ROOT / "src" / "boards" / "rurp_config_storage_eeprom.cpp",
    # rurp_validate_config delegates its per-field migration here (v1.43), so
    # the link set needs it. The migration was extracted precisely so it could
    # be covered by behaviour in test/native/avr/test_config_migrate rather
    # than by scanning this file's text.
    REPO_ROOT / "src" / "rurp_config_migrate.cpp",
)

_CONFIG_START = 48

_ACCESS_RE = re.compile(r"^ACCESS (\w+) ([GP]) (\d+) (\d+)$")
_SIZEOF_RE = re.compile(r"^SIZEOF (\d+)$")


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


def _run_regression_harness(tmp_path):
    """Compile and run the source(s) plus the fresh fake EEPROM.h and driver
    TU, and parse stdout into a dict of
    {"load": [...], "save": [...], "validate": [...], "sizeof": int}, each
    list holding (op, idx, length) tuples in the order printed. Returns
    (compile_result, run_result, parsed); run_result is None if the compile
    failed. The -I entries are the repo include/ directory AND tmp_path, so
    the fake EEPROM.h shadows nothing real."""
    _write_fake_eeprom_header(tmp_path)
    driver_tu = _write_driver_tu(tmp_path)
    binary_path = tmp_path / "config_storage_regression"

    compile_result = run_compiler([
        host_cxx(), "-std=gnu++17", "-Wall", "-Wextra", "-DARDUINO_AVR_UNO",
        "-I", str(INCLUDE), "-I", str(tmp_path),
        *(str(source) for source in _SOURCES), str(driver_tu),
        "-o", str(binary_path),
    ])

    run_result = None
    parsed = {"load": [], "save": [], "validate": [], "sizeof": None}
    if compile_result.returncode == 0:
        run_result = run_binary(binary_path)
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


@pytest.fixture(scope="module")
def _harness_once(tmp_path_factory):
    return _run_regression_harness(tmp_path_factory.mktemp("eeprom_regression"))


@pytest.fixture
def harness(_harness_once):
    """The one compile-and-run of this module. Each test gets its own copy
    of the parsed accesses."""
    compile_result, run_result, parsed = _harness_once
    return compile_result, run_result, copy.deepcopy(parsed)


def test_pre_refactor_tu_compiles_and_runs_with_zero_warnings(harness):
    """Coverage 1 -- compiles and runs the source(s)
    against the hand-written fake EEPROM.h, asserting a clean compile, zero
    bytes of -Wall -Wextra warning output (not merely exit 0), and a clean
    run."""
    compile_result, run_result, _parsed = harness
    assert compile_result.returncode == 0, (
        f"expected a clean compile of {_SOURCES!r}.\n"
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


def test_load_config_get_access_at_config_start_with_sizeof_length(harness):
    """Coverage 2 -- CFG-04: rurp_load_config() reads once at index 48 with a
    length equal to sizeof(rurp_configuration_t) as the compiled binary itself
    reported it. Compared against the binary's own reported sizeof, never
    against a Python integer literal -- host `long` is 8 bytes (C-6).

    The read may be followed by a write-back. That is v1.43's per-field
    migration doing its job: this harness starts from an unwritten config, so
    r1/r2/bandgap are out of band and get corrected, and the correction is
    persisted. It is the behaviour that fixes the stranding defect, where the
    old version-gated wipe could never reach a stale value whose version
    string already matched.

    What must stay true is that the write-back is NOT unconditional -- a board
    whose stored config is already good must not rewrite its EEPROM on every
    boot. That property is proved by behaviour in
    test/native/avr/test_config_migrate (test_a_current_config_is_left_alone),
    which runs the real migrator rather than inspecting access traces."""
    compile_result, run_result, parsed = harness
    assert compile_result.returncode == 0 and compile_result.stderr == "", (
        f"expected a clean, warning-free compile.\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    sizeof_value = parsed["sizeof"]
    assert sizeof_value is not None, (
        f"expected a 'SIZEOF <n>' line in stdout.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )
    load_accesses = parsed["load"]
    assert load_accesses and load_accesses[0] == ("G", _CONFIG_START, sizeof_value), (
        f"expected rurp_load_config() to READ FIRST at index {_CONFIG_START} "
        f"with length sizeof(rurp_configuration_t)={sizeof_value}, got "
        f"{load_accesses!r}.\n"
        f"stdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )
    assert all(a[1] == _CONFIG_START and a[2] == sizeof_value for a in load_accesses), (
        f"every access from rurp_load_config() must use index {_CONFIG_START} "
        f"and length {sizeof_value}; got {load_accesses!r}"
    )
    assert [a[0] for a in load_accesses] in (["G"], ["G", "P"]), (
        f"rurp_load_config() must read, and may write back exactly once when "
        f"the migration corrects a field; got {load_accesses!r}"
    )


def test_save_config_put_access_at_config_start_with_sizeof_length(harness):
    """Coverage 3 -- CFG-04: rurp_save_config() produces exactly one
    recorded put access, at index 48, with the same symbolic length."""
    compile_result, run_result, parsed = harness
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


def test_validate_config_write_back_produces_put_on_version_mismatch(harness):
    """Coverage 4 -- the D-14 write-back at rurp_config_utils.cpp:38:
    rurp_validate_config() on a struct whose version does not match
    CONFIG_VERSION writes the defaults back and triggers exactly one put,
    at index 48 with the same symbolic length -- this must behave
    identically after the split."""
    compile_result, run_result, parsed = harness
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


def test_non_vacuity_source_resolved_and_access_recorded(harness):
    """Coverage 5 -- rurp_load_config() recorded at least one access. A
    missing source file fails the compile, so the sources need no separate
    check. Deliberately scoped to the load phase alone rather
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
    compile_result, run_result, parsed = harness
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
