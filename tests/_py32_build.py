"""Compile definitions from an actual, explicitly identified ARM build."""
import json
import os
import shlex
from pathlib import Path

import pytest


def arm_defines(source):
    path = os.environ.get('FIRESTARTER_PY32_COMPILE_COMMANDS')
    if not path:
        pytest.skip('requires compile_commands.json from an ARM CMake build')
    entries = json.loads(Path(path).read_text())
    entry = next(e for e in entries if e['file'].endswith('/' + source))
    tokens = entry.get('arguments') or shlex.split(entry['command'])
    defines = [t for t in tokens if t.startswith('-D')]
    assert '-DRURP_PLATFORM_PY32F071=1' in defines
    assert '-DRURP_PY32F071_PINMAP_CONFIGURED=1' in defines
    board = next(d.split('=', 1)[1].strip('"') for d in defines if d.startswith('-DRURP_BOARD_NAME='))
    assert ('-DRURP_BOARD_MICROPETAL_UNO=1' in defines) == (board == 'micropetal_uno')
    # The default build is provisional. Only the MicroPetal full build
    # (RURP_MICROPETAL_FULL=ON) is not.
    if is_full_build(defines):
        assert board == 'micropetal_uno'
    else:
        assert '-DRURP_PINMAP_PROVISIONAL=1' in defines
    return defines


def is_full_build(defines):
    return '-DRURP_PINMAP_PROVISIONAL=0' in defines


def with_define(defines, name, value):
    """Replace or add -D<name>=<value>."""
    return [d for d in defines if not d.startswith(f'-D{name}=')] + [f'-D{name}={value}']
