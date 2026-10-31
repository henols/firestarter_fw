"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson
Permission is hereby granted under MIT license.

Shared helpers for the tests that compile production code with the host
compiler and run the result. Each test keeps its own compiler flags and its
own output parsing.

A missing host compiler FAILS the test, never skips it: build.yml runs
`pytest tests/` on a runner that has gcc and g++, before any embedded
toolchain is installed.
"""

import os
import shutil
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
INCLUDE = REPO_ROOT / "include"

# A minimal Arduino.h for a driver that compiles production code on the host.
FAKE_ARDUINO_H = """#pragma once
#include <stdint.h>
#include <stddef.h>
unsigned long millis();
void delay(unsigned long ms);
"""


def _resolve(env_var, default, kind):
    compiler = shutil.which(os.environ.get(env_var, default))
    assert compiler is not None, (
        f"host {kind} compiler not found on PATH (checked ${env_var}, falling "
        f"back to '{default}'). This must FAIL the suite, never be silently "
        "skipped."
    )
    return compiler


def host_cc():
    """The host C compiler ($CC, or gcc)."""
    return _resolve("CC", "gcc", "C")


def host_cxx():
    """The host C++ compiler ($CXX, or g++)."""
    return _resolve("CXX", "g++", "C++")


def run_compiler(argv, cwd=None):
    """Run a compiler argv (a list, never a shell) and return the result.
    Use it where the test expects the compile to fail."""
    return subprocess.run([str(a) for a in argv], cwd=cwd, capture_output=True, text=True)


def compile_ok(argv, cwd=None):
    """Compile and assert a clean build with zero bytes of diagnostic output."""
    result = run_compiler(argv, cwd)
    assert result.returncode == 0 and result.stderr == "", (
        "expected a clean, warning-free compile.\n"
        f"argv: {' '.join(str(a) for a in argv)}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    return result


def run_binary(path, cwd=None):
    """Run a compiled binary and capture its output. A result crosses on
    stdout, never on the exit code: 1 is also the exit code of a crash."""
    return subprocess.run([str(path)], cwd=cwd, capture_output=True, text=True)
