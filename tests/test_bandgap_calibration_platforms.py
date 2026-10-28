"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson
Permission is hereby granted under MIT license.

Compile and execute the production config-frame parser for an AVR-class
target and for the PY32F071 target. Assert that the PY32 build refuses a
bandgap calibration request (vcc or bg), because that port never reads the
stored bandgap, and that the AVR build accepts the same frames.
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
_JSMN = _REPO_ROOT / "lib" / "jsmn" / "src"
_SOURCES = (_REPO_ROOT / "src" / "json_parser.c", _JSMN / "jsmn.c")

_FRAMES = (
    '{"state":14,"vcc":5000}',
    '{"state":14,"bg":1100}',
    '{"state":14,"r1":270000}',
)
_RESULT_RE = re.compile(r"^FRAME (\d+) (-?\d+)$")


def _resolve_compiler():
    """Resolve the host C compiler. A missing compiler fails the test."""
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    assert compiler is not None, "host C compiler not found on PATH ($CC or gcc)"
    return compiler


def _c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _write_driver(tmp_path):
    frames = ", ".join(_c_string(f) for f in _FRAMES)
    tu_path = tmp_path / "config_frames_main.c"
    tu_path.write_text(
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        '#include "json_parser.h"\n'
        '#include "jsmn.h"\n'
        "\n"
        "int main(void) {\n"
        f"    const char* frames[] = {{{frames}}};\n"
        "    for (unsigned i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {\n"
        "        jsmntok_t tokens[NUMBER_JSNM_TOKENS];\n"
        "        jsmn_parser parser;\n"
        "        jsmn_init(&parser);\n"
        "        int count = jsmn_parse(&parser, frames[i], strlen(frames[i]), tokens,\n"
        "                               NUMBER_JSNM_TOKENS);\n"
        "        rurp_configuration_t config;\n"
        "        memset(&config, 0, sizeof(config));\n"
        "        firestarter_handle_t handle;\n"
        "        memset(&handle, 0, sizeof(handle));\n"
        '        printf("FRAME %u %d\\n", i,\n'
        "               json_parse_config(frames[i], tokens, count, &config, &handle));\n"
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
        "-I", str(_INCLUDE), "-I", str(_JSMN),
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


@pytest.mark.parametrize("frame", _FRAMES[:2])
def test_py32_refuses_a_bandgap_calibration_frame(tmp_path, frame):
    results = _parse_results(tmp_path, ["-DRURP_PLATFORM_PY32F071=1"])
    assert results[frame] < 0, (
        f"the PY32 port never reads the stored bandgap, so {frame} must be "
        f"refused; got {results[frame]}"
    )


def test_py32_still_accepts_a_divider_frame(tmp_path):
    results = _parse_results(tmp_path, ["-DRURP_PLATFORM_PY32F071=1"])
    assert results[_FRAMES[2]] > 0, results


@pytest.mark.parametrize("frame", _FRAMES)
def test_avr_accepts_every_calibration_frame(tmp_path, frame):
    # Non-vacuity: the same frames parse where calibration exists.
    results = _parse_results(tmp_path, [])
    assert results[frame] > 0, results
