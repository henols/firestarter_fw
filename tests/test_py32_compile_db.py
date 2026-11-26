"""The shared compile-database reader, and the single source of the
provisional pin-map flag in a real ARM build."""
import json
import os

import pytest

from ._compile_db import COMPILE_COMMANDS_ENV, compile_defines, defines_from_argv, firmware_defines
from ._host_build import REPO_ROOT


def test_defines_from_argv_reads_both_forms():
    argv = ["cc", "-DA=1", "-D", "B=2", "-I", "inc", "-D", "C", "-Dq", "-o", "x.o", "-D"]
    assert defines_from_argv(argv) == ["A=1", "B=2", "C", "q"]


def test_compile_defines_matches_entry_and_names_a_missing_one(tmp_path):
    database = tmp_path / "compile_commands.json"
    database.write_text(json.dumps([
        {"directory": str(tmp_path), "file": "other.c", "command": "cc -DX=1 -c other.c"},
        {"directory": str(REPO_ROOT / "src"), "file": "rurp_vpp.cpp",
         "arguments": ["c++", "-D", "SPACED=1", "-DJOINED", "-c", "rurp_vpp.cpp"]},
    ]))
    assert compile_defines(database, "src/rurp_vpp.cpp") == ["SPACED=1", "JOINED"]
    with pytest.raises(LookupError) as error:
        compile_defines(database, "src/firestarter.cpp")
    assert str(database) in str(error.value) and "src/firestarter.cpp" in str(error.value)


def test_provisional_flag_has_one_value_in_every_firmware_unit():
    """henols/firestarter#187: socket GPIO (the board backend) and the
    memory-command refusal (shared handlers) must read the same flag value,
    so it must reach every translation unit exactly once, from the build."""
    database = os.environ.get(COMPILE_COMMANDS_ENV)
    if not database:
        pytest.skip("requires compile_commands.json from an ARM CMake build")
    units = firmware_defines(database)
    assert any(source == "src/proms/memory.cpp" for source, _ in units)
    assert any(source.endswith("py32f071_rurp_shield.cpp") for source, _ in units)
    values = {}
    for source, defines in units:
        flags = [d for d in defines if d.split("=", 1)[0] == "RURP_PINMAP_PROVISIONAL"]
        assert len(flags) == 1, f"{source}: {flags}"
        values[source] = flags[0]
    assert len(set(values.values())) == 1, values
