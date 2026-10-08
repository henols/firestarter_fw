# `tests/fixtures/` -- naming discipline

This directory holds committed inputs for the checker scripts in `scripts/` and their
tests. Every entry uses one of two prefixes:

- **`planted_*`** -- a deliberate violation. It proves a checker fails closed instead of
  passing vacuously.
- **`clean_*`** -- a control that must pass a checker. It proves the checker does not fire
  on legitimate input.

An entry can be a file or a directory tree. `tests/test_checker_convention.py` enforces
that each `scripts/check_<X>.py` has a `planted_<X>*` entry. It does not keep a per-file
list here.

PlatformIO does not see this directory. Its test discovery reads `test/`, and the native
source filter in `platformio.ini` names no path under `tests/`. A `.cpp` file here cannot
reach any build.

Check that a fixture is committed with `git ls-files tests/fixtures/`. Do not use the exit
code of `git add`: git silently stages nothing for a path that contains a `.git` component.
