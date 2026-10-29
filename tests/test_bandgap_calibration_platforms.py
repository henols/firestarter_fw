"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson
Permission is hereby granted under MIT license.

Compile and execute the production command-frame decoder for an AVR-class
target and for the PY32F071 target. Assert that the PY32 build refuses a
bandgap write (bg), because that port never reads the stored bandgap, and that
the AVR build accepts it. Both builds refuse the retired vcc bit (0x08): the
host now back-solves the bandgap and sends it as bg.
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
_SOURCES = (_REPO_ROOT / "src" / "command_decoder.c",)

# The config field each CMD_CONFIG frame carries.
_FRAMES = ("vcc", "bg", "r1")
_RESULT_RE = re.compile(r"^FRAME (\d+) (\d+)$")


def _resolve_compiler():
    """Resolve the host C compiler. A missing compiler fails the test."""
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    assert compiler is not None, "host C compiler not found on PATH ($CC or gcc)"
    return compiler


def _write_driver(tmp_path):
    tu_path = tmp_path / "config_frames_main.c"
    tu_path.write_text(
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        '#include "command_decoder.h"\n'
        "\n"
        "static const uint8_t present[] = {\n"
        "    0x08 /* retired vcc */, CF_CONFIG_PRESENT_BG, CF_CONFIG_PRESENT_R1};\n"
        "\n"
        "int main(void) {\n"
        "    for (unsigned i = 0; i < sizeof(present); i++) {\n"
        "        firestarter_handle_t handle;\n"
        "        memset(&handle, 0, sizeof(handle));\n"
        "        uint8_t* f = (uint8_t*)handle.data_buffer;\n"
        "        f[CF_OFF_MAGIC] = CF_MAGIC;\n"
        "        f[CF_OFF_VERSION] = CF_VERSION;\n"
        "        f[CF_OFF_CMD] = CMD_CONFIG;\n"
        "        f[CF_OFF_LENGTH + 1] = CF_CONFIG_SIZE;\n"
        "        uint8_t* p = f + CF_HEADER_SIZE;\n"
        "        p[CF_CONFIG_OFF_PRESENT] = present[i];\n"
        "        p[CF_CONFIG_OFF_R1 + 1] = 0x04;  /* 270000 = 0x00041EB0 */\n"
        "        p[CF_CONFIG_OFF_R1 + 2] = 0x1E;\n"
        "        p[CF_CONFIG_OFF_R1 + 3] = 0xB0;\n"
        "        p[CF_CONFIG_OFF_BG] = 0x04;      /* 1100 */\n"
        "        p[CF_CONFIG_OFF_BG + 1] = 0x4C;\n"
        "        handle.data_size = CF_HEADER_SIZE + CF_CONFIG_SIZE;\n"
        '        printf("FRAME %u %u\\n", i, (unsigned)cmd_decode(&handle));\n'
        "    }\n"
        "    return 0;\n"
        "}\n"
    )
    return tu_path


def _parse_results(tmp_path, defines):
    compiler = _resolve_compiler()
    driver = _write_driver(tmp_path)
    binary = tmp_path / "config_frames"
    argv = [
        compiler, "-std=gnu11", "-Wall", "-Wextra", "-DHARDWARE_REVISION",
        *defines,
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
    results = {}
    for line in ran.stdout.splitlines():
        match = _RESULT_RE.match(line)
        if match:
            results[_FRAMES[int(match.group(1))]] = int(match.group(2))
    assert set(results) == set(_FRAMES), f"missing frames in:\n{ran.stdout}"
    return results


_DECODE_OK = 0
_REFUSE_CONFIG_FIELD = 9  # CF_REFUSE_CONFIG_FIELD in include/command_frame.h


@pytest.mark.parametrize("frame", _FRAMES[:2])
def test_py32_refuses_a_bandgap_calibration_frame(tmp_path, frame):
    results = _parse_results(tmp_path, ["-DRURP_PLATFORM_PY32F071=1"])
    assert results[frame] == _REFUSE_CONFIG_FIELD, (
        f"the PY32 port never reads the stored bandgap, so {frame} must be "
        f"refused; got {results[frame]}"
    )


def test_py32_still_accepts_a_divider_frame(tmp_path):
    results = _parse_results(tmp_path, ["-DRURP_PLATFORM_PY32F071=1"])
    assert results[_FRAMES[2]] == _DECODE_OK, results


@pytest.mark.parametrize("frame", _FRAMES[1:])
def test_avr_accepts_a_bandgap_and_a_divider_frame(tmp_path, frame):
    # Non-vacuity: the bg frame decodes where calibration exists.
    results = _parse_results(tmp_path, [])
    assert results[frame] == _DECODE_OK, results


def test_avr_refuses_the_retired_vcc_bit(tmp_path):
    results = _parse_results(tmp_path, [])
    assert results["vcc"] == _REFUSE_CONFIG_FIELD, results
