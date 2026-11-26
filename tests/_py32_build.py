"""Compile definitions from an actual, explicitly identified ARM build."""
import os

import pytest

from ._compile_db import COMPILE_COMMANDS_ENV, compile_defines
from ._host_build import INCLUDE, compile_ok, host_cc


def arm_defines(source):
    """The -D arguments that the ARM build passes for `source` (relative to
    the repository root), ready to append to a host compiler argv."""
    path = os.environ.get(COMPILE_COMMANDS_ENV)
    if not path:
        pytest.skip('requires compile_commands.json from an ARM CMake build')
    try:
        defines = ['-D' + d for d in compile_defines(path, source)]
    except LookupError as error:
        pytest.fail(str(error))
    assert '-DRURP_PLATFORM_PY32F071=1' in defines
    assert '-DRURP_PY32F071_PINMAP_CONFIGURED=1' in defines
    board = next(d.split('=', 1)[1].strip('"') for d in defines if d.startswith('-DRURP_BOARD_NAME='))
    assert '-DRURP_HAS_HOST_SESSIONS=1' in defines
    assert ('-DHARDWARE_REVISION=1' in defines) == (board == 'micropetal_uno')
    # The default build is provisional. Only the MicroPetal full build
    # (RURP_MICROPETAL_FULL=ON) is not.
    if is_full_build(defines):
        assert board == 'micropetal_uno'
    else:
        assert '-DRURP_PINMAP_PROVISIONAL=1' in defines
    return defines


def _define_value(defines, name):
    return next(d.split('=', 1)[1].strip('"') for d in defines if d.startswith(f'-D{name}='))


def board_name(defines):
    """The CMake RURP_BOARD. Use it only to mirror a CMake board gate."""
    return _define_value(defines, 'RURP_BOARD_NAME')


def pinmap_flag(defines, name):
    """The value of a capability flag in the pin map of the build."""
    header = _define_value(defines, 'RURP_PY32_PINMAP_HEADER')
    macros = compile_ok([host_cc(), '-E', '-dM', '-I', INCLUDE, '-include', header,
                         '-x', 'c', '/dev/null']).stdout
    values = [line.split()[2] for line in macros.splitlines()
              if line.startswith(f'#define {name} ')]
    assert values in (['0'], ['1']), f'{header} must define {name} as 0 or 1'
    return int(values[0])


def is_full_build(defines):
    return '-DRURP_PINMAP_PROVISIONAL=0' in defines


def with_define(defines, name, value):
    """Replace or add -D<name>=<value>."""
    return [d for d in defines if not d.startswith(f'-D{name}=')] + [f'-D{name}={value}']
