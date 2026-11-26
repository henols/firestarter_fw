"""PY32 pin-map capability flags: board behaviour comes from required flags in
the board's pin-map header, never from the board name, and a header that
misses a flag fails to compile (henols/firestarter#185)."""
import pytest

from ._host_build import INCLUDE, host_cc, run_compiler

_FLAGS = ("RURP_PY32F071_HAS_USER_BUTTON", "RURP_PY32F071_HAS_REVISION_ADC",
          "RURP_PY32F071_SAFE_IDLE_AT_RESET", "RURP_PY32F071_HAS_ROM_TOUCH")


def _preprocess(tmp_path, pinmap_header, *extra):
    # The vendor HAL is not needed to evaluate the board-header guards.
    (tmp_path / "py32f0xx_hal.h").write_text("#pragma once\n")
    source = tmp_path / "unit.c"
    source.write_text('#include "boards/py32f071_rurp_shield.h"\n')
    return run_compiler([
        host_cc(), "-E", "-I", tmp_path, "-I", INCLUDE,
        "-DRURP_PLATFORM_PY32F071=1", "-DRURP_PY32F071_PINMAP_CONFIGURED=1",
        "-DRURP_PINMAP_PROVISIONAL=1", f'-DRURP_PY32_PINMAP_HEADER="{pinmap_header}"',
        *extra, source, "-o", tmp_path / "unit.i"])


@pytest.mark.parametrize("board,extra", [
    ("py32f071", ()),
    ("micropetal_uno", ("-DHARDWARE_REVISION=1", "-DRURP_MICROPETAL_ROM_TOUCH=1")),
])
def test_each_board_header_defines_every_flag(tmp_path, board, extra):
    result = _preprocess(tmp_path, f"boards/{board}_pinmap.h", *extra)
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize("missing", _FLAGS)
def test_a_pinmap_without_a_flag_fails_to_compile(tmp_path, missing):
    lines = (INCLUDE / "boards" / "micropetal_uno_pinmap.h").read_text().splitlines()
    kept = [line for line in lines if not line.startswith(f"#define {missing} ")]
    assert len(kept) == len(lines) - 1
    (tmp_path / "new_board_pinmap.h").write_text("\n".join(kept) + "\n")
    result = _preprocess(tmp_path, "new_board_pinmap.h", "-DHARDWARE_REVISION=1")
    assert result.returncode != 0
    assert "must define" in result.stderr and missing in result.stderr


def test_revision_detection_needs_a_revision_adc(tmp_path):
    result = _preprocess(tmp_path, "boards/py32f071_pinmap.h", "-DHARDWARE_REVISION=1")
    assert result.returncode != 0
    assert "needs a revision-detect ADC input" in result.stderr


def test_rom_touch_needs_a_rom_touch_board(tmp_path):
    result = _preprocess(tmp_path, "boards/py32f071_pinmap.h", "-DRURP_MICROPETAL_ROM_TOUCH=1")
    assert result.returncode != 0
    assert "needs a board with RURP_PY32F071_HAS_ROM_TOUCH 1" in result.stderr
