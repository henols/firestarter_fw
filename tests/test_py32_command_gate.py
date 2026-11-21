"""Check refusal with the real ARM command-handler compile definitions."""
import json
import os
import shlex
from pathlib import Path

import pytest

from ._host_build import INCLUDE, compile_ok, host_cxx, run_binary


def test_real_arm_command_handler_refuses_socket_commands(tmp_path):
    path = os.environ.get('FIRESTARTER_PY32_COMPILE_COMMANDS')
    if not path:
        pytest.skip('requires compile_commands.json from an ARM CMake build')
    entries = json.loads(Path(path).read_text())
    entry = next(e for e in entries if e['file'].endswith('/src/proms/memory.cpp'))
    tokens = entry.get('arguments') or shlex.split(entry['command'])
    defines = [t for t in tokens if t.startswith('-D')]
    driver = tmp_path / 'gate.cpp'
    # No board header: shared command handlers must receive the flag from
    # their build configuration, not accidentally from hardware includes.
    driver.write_text('''
#include <cassert>
#include "rurp_pinmap_guard.h"
int main() {
    for (unsigned cmd = 0; cmd < 256; ++cmd)
        assert(rurp_pinmap_refuses(cmd) == is_memory_cmd(cmd));
}
''')
    binary = tmp_path / 'gate'
    # NDEBUG comes from the Release configuration. Remove it so asserts run.
    compile_ok([host_cxx(), '-std=c++17', *defines, '-UNDEBUG', '-I', INCLUDE,
                driver, '-o', binary])
    result = run_binary(binary)
    assert result.returncode == 0, result.stderr
