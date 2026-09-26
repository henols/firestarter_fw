# `clean_unarmed_tree/`

A generic "no ARM port present" firmware root: no `platform/` directory at all, just a
`src/` with one file. This is the D-07 UNARMED control — a miniature stand-in for the
real firmware tree's current state (before Phase 124 lands `platform/py32f071/`).

**Shared across plans:** this fixture is deliberately generic (not `_cmake_manifest_`
scoped) because plan 123-05 needs the identical UNARMED case for its own coarse-key
arming gate. Do not scope its name or contents to any one checker.

## Expected gate behaviour

`FIRESTARTER_MANIFEST_ROOT=<this dir> python3 scripts/check_cmake_manifest.py`:

- Exit **0**
- Prints a line beginning `UNARMED:`

## Not real firmware source

`src/placeholder.cpp` is a one-line fixture stub, not a copy of any real firmware file.
