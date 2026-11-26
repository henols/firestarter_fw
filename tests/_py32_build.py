"""Compile definitions from an actual, explicitly identified ARM build."""
import os

import pytest

from ._compile_db import COMPILE_COMMANDS_ENV, compile_defines


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
    assert '-DRURP_PINMAP_PROVISIONAL=1' in defines
    assert '-DRURP_PY32F071_PINMAP_CONFIGURED=1' in defines
    board = next(d.split('=', 1)[1].strip('"') for d in defines if d.startswith('-DRURP_BOARD_NAME='))
    assert '-DRURP_HAS_HOST_SESSIONS=1' in defines
    assert ('-DHARDWARE_REVISION=1' in defines) == (board == 'micropetal_uno')
    return defines
