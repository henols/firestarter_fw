"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson

Permission is hereby granted under MIT license.

Paired test for scripts/check_test_filter_complete.py. Each test runs the
checker as a real subprocess. The planted tree has a suite directory that the
filter omits. The clean tree lists every suite. A synthetic tree adds a
dangling filter entry.
"""

import shutil
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_CHECKER = _REPO_ROOT / "scripts" / "check_test_filter_complete.py"
_FIXTURES = _HERE / "fixtures"


def _run(root=None):
    argv = [sys.executable, str(_CHECKER)]
    if root is not None:
        argv += ["--root", str(root)]
    result = subprocess.run(argv, cwd=str(_REPO_ROOT), capture_output=True, text=True)
    return result.returncode, result.stdout, result.stderr


def test_real_tree_lists_every_suite():
    returncode, out, err = _run()
    assert returncode == 0, f"stdout:\n{out}\nstderr:\n{err}"
    assert out.startswith("PASS:")


def test_planted_unregistered_suite_fails():
    returncode, out, err = _run(_FIXTURES / "planted_test_filter_complete")
    assert returncode != 0, f"stdout:\n{out}\nstderr:\n{err}"
    assert "test_beta" in out
    assert "test_alpha" not in out
    assert "PASS:" not in out


def test_clean_tree_passes():
    returncode, out, err = _run(_FIXTURES / "clean_test_filter_complete")
    assert returncode == 0, f"stdout:\n{out}\nstderr:\n{err}"
    assert out.startswith("PASS:")


def test_dangling_filter_entry_fails(tmp_path):
    root = tmp_path / "tree"
    shutil.copytree(_FIXTURES / "clean_test_filter_complete", root)
    ini = root / "platformio.ini"
    ini.write_text(ini.read_text() + "\tnative/avr/test_gone\n")
    returncode, out, err = _run(root)
    assert returncode != 0, f"stdout:\n{out}\nstderr:\n{err}"
    assert "test_gone" in out


def test_missing_platformio_ini_is_fail_closed(tmp_path):
    root = tmp_path / "tree"
    shutil.copytree(_FIXTURES / "clean_test_filter_complete", root)
    (root / "platformio.ini").unlink()
    returncode, out, err = _run(root)
    assert returncode == 2, f"stdout:\n{out}\nstderr:\n{err}"
    assert "PASS:" not in out
