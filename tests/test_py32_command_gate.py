"""Check refusal with the real ARM command-handler compile definitions."""
from ._py32_build import arm_defines, is_full_build
from ._host_build import INCLUDE, compile_ok, host_cxx, run_binary


def test_real_arm_command_handler_refuses_socket_commands(tmp_path):
    defines = arm_defines('src/proms/memory.cpp')
    # A provisional build refuses every memory command. The full build
    # refuses none.
    expect_refuse = 0 if is_full_build(defines) else 1
    driver = tmp_path / 'gate.cpp'
    # No board header: shared command handlers must receive the flag from
    # their build configuration, not accidentally from hardware includes.
    driver.write_text('''
#include <cassert>
#include "rurp_pinmap_guard.h"
int main() {
    for (unsigned cmd = 0; cmd < 256; ++cmd)
        assert(rurp_pinmap_refuses(cmd) == (EXPECT_REFUSE && is_memory_cmd(cmd)));
}
''')
    binary = tmp_path / 'gate'
    # NDEBUG comes from the Release configuration. Remove it so asserts run.
    compile_ok([host_cxx(), '-std=c++17', *defines, f'-DEXPECT_REFUSE={expect_refuse}',
                '-UNDEBUG', '-I', INCLUDE,
                driver, '-o', binary])
    result = run_binary(binary)
    assert result.returncode == 0, result.stderr
