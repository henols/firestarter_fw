# `planted_orphan_provisional_macro/`

A miniature, self-contained firmware root proving `check_orphan_provisional.py`'s
core check: a `RURP_*_PROVISIONAL`-style flag with zero consumers outside its own
definition must fail the gate, naming the macro and its defining file and line.

## What is planted

`include/fixture_provisional.h` defines **two** fixture-scoped provisional macros:

- `RURP_FIXTURE_ORPHAN_PROVISIONAL` — defined, and referenced **nowhere else** in
  this tree. This is the planted defect.
- `RURP_FIXTURE_CONSUMED_PROVISIONAL` — defined, and referenced by a real
  preprocessor conditional in `src/fixture_consumer.cpp`.

Both macros exist in the same fixture on purpose: a tree where every macro is
orphaned would not distinguish a working gate from one that fails
unconditionally. The expected violation count is exactly **1**
(`RURP_FIXTURE_ORPHAN_PROVISIONAL`), and `RURP_FIXTURE_CONSUMED_PROVISIONAL` must
be absent from the violation bucket.

`platform/py32f071/CMakeLists.txt` is the D-07 arming key — its mere presence as a
directory is what arms `check_orphan_provisional.py`, matching
`check_cmake_manifest.py`'s identical coarse-key idiom. Its contents are not
otherwise read by this checker.

## Expected gate behaviour

`FIRESTARTER_PROVISIONAL_ROOT=<this dir> python3 scripts/check_orphan_provisional.py`:

- Exit **non-zero**
- Reports exactly **1** violation
- Names `RURP_FIXTURE_ORPHAN_PROVISIONAL` in the `FAIL:` output
- Does NOT name `RURP_FIXTURE_CONSUMED_PROVISIONAL` in the `FAIL:` output

## Not real firmware source

Every file under `include/` and `src/` here is a short fixture stub carrying a
header comment marking it as fixture input — none is a copy of any real
firmware file.
