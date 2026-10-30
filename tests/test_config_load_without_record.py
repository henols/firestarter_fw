"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson
Permission is hereby granted under MIT license.

Compile and execute the production configuration policy layer against a fake
storage backend whose load reports "no record". Assert that rurp_load_config()
then resets every field, including the shield override, and saves the result.
"""


import os
import re
import shutil
import subprocess
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_INCLUDE = _REPO_ROOT / "include"
_SOURCES = (
    _REPO_ROOT / "src" / "rurp_config_utils.cpp",
    _REPO_ROOT / "src" / "rurp_config_migrate.cpp",
)

_DEFAULTS_RE = re.compile(r"^DEFAULTS r1=(\d+) r2=(\d+) bg=(\d+)$")
_BOOT_RE = re.compile(r"^BOOT rev=(\d+)$")
_RESULT_RE = re.compile(
    r"^RESULT version=(\S*) r1=(\d+) r2=(\d+) rev=(\d+) bg=(\d+) saves=(\d+)$"
)


def _resolve_compiler():
    """Resolve the host C++ compiler. A missing compiler fails the test."""
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    assert compiler is not None, "host C++ compiler not found on PATH ($CXX or g++)"
    return compiler


def _write_driver(tmp_path, load_result):
    """A fake storage backend plus a main that seeds the live configuration
    with a plausible record, runs rurp_load_config() and prints the result.

    The seed is deliberately a record that migration would keep as it is, so
    only the load result can make the fields change."""
    tu_path = tmp_path / "load_without_record_main.cpp"
    tu_path.write_text(
        '#include "rurp_shield.h"\n'
        '#include "rurp_config_storage.h"\n'
        '#include "rurp_voltage_math.h"\n'
        "#include <cstdio>\n"
        "#include <cstring>\n"
        "\n"
        "static int g_saves = 0;\n"
        "\n"
        "bool rurp_config_storage_load(void* blob, size_t len) {\n"
        "    (void)blob;\n"
        "    (void)len;\n"
        f"    return {load_result};\n"
        "}\n"
        "\n"
        "bool rurp_config_storage_save(const void* blob, size_t len) {\n"
        "    (void)blob;\n"
        "    (void)len;\n"
        "    g_saves++;\n"
        "    return true;\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    rurp_configuration_t* config = rurp_get_config();\n"
        '    printf("BOOT rev=%u\\n", (unsigned)config->hardware_revision);\n'
        "    strcpy(config->version, CONFIG_VERSION);\n"
        "    config->r1 = 300000;\n"
        "    config->r2 = 40000;\n"
        "    config->hardware_revision = REVISION_2_2;\n"
        "    config->bandgap_mv = 1050;\n"
        "    rurp_load_config();\n"
        '    printf("RESULT version=%s r1=%ld r2=%ld rev=%u bg=%u saves=%d\\n",\n'
        "           config->version, config->r1, config->r2,\n"
        "           (unsigned)config->hardware_revision,\n"
        "           (unsigned)config->bandgap_mv, g_saves);\n"
        '    printf("DEFAULTS r1=%ld r2=%ld bg=%u\\n", (long)VALUE_R1, (long)VALUE_R2,\n'
        "           (unsigned)RURP_BANDGAP_NOMINAL_MV);\n"
        "    return 0;\n"
        "}\n"
    )
    return tu_path


def _run(tmp_path, load_result):
    compiler = _resolve_compiler()
    driver = _write_driver(tmp_path, load_result)
    binary = tmp_path / f"load_{load_result}"
    argv = [
        compiler, "-std=gnu++17", "-Wall", "-Wextra", "-DHARDWARE_REVISION",
        "-I", str(_INCLUDE),
        *(str(s) for s in _SOURCES), str(driver),
        "-o", str(binary),
    ]
    compiled = subprocess.run(argv, capture_output=True, text=True)
    assert compiled.returncode == 0 and compiled.stderr == "", (
        f"expected a clean, warning-free compile.\nstderr:\n{compiled.stderr}"
    )
    ran = subprocess.run([str(binary)], capture_output=True, text=True)
    assert ran.returncode == 0, f"binary failed.\nstderr:\n{ran.stderr}"
    result = None
    defaults = None
    boot_rev = None
    for line in ran.stdout.splitlines():
        match = _BOOT_RE.match(line)
        if match:
            boot_rev = int(match.group(1))
        match = _RESULT_RE.match(line)
        if match:
            version, r1, r2, rev, bg, saves = match.groups()
            result = {
                "version": version, "r1": int(r1), "r2": int(r2),
                "rev": int(rev), "bg": int(bg), "saves": int(saves),
            }
        match = _DEFAULTS_RE.match(line)
        if match:
            defaults = tuple(int(g) for g in match.groups())
    assert result is not None and defaults is not None and boot_rev is not None, (
        f"expected BOOT, RESULT and DEFAULTS lines in output:\n{ran.stdout}"
    )
    return result, defaults, boot_rev


def test_a_load_without_a_record_resets_the_override_and_saves(tmp_path):
    result, defaults, _boot_rev = _run(tmp_path, "false")
    assert result["rev"] == 0xFF, (
        "a store with no record must not leave a shield override behind; "
        f"got {result!r}"
    )
    assert (result["r1"], result["r2"], result["bg"]) == defaults, (result, defaults)
    assert result["version"].startswith("VER"), result
    assert result["saves"] == 1, result


def test_a_load_with_a_record_keeps_it(tmp_path):
    # Non-vacuity: the same seed survives when the load reports a record.
    result, _defaults, _boot_rev = _run(tmp_path, "true")
    assert result["rev"] == 4, result
    assert (result["r1"], result["r2"], result["bg"]) == (300000, 40000, 1050), result
    assert result["saves"] == 0, result


def test_the_config_before_the_load_has_no_override(tmp_path):
    # Board setup writes the control register before rurp_load_config()
    # runs. Until then the shield revision must read as "no override" (0xFF),
    # not as a REVISION_0 override.
    _result, _defaults, boot_rev = _run(tmp_path, "true")
    assert boot_rev == 0xFF, boot_rev
