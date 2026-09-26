# `planted_cmake_manifest_excluded_no_reason/`

A miniature, self-contained firmware root proving `check_cmake_manifest.py` fails on an
unreasoned `PY32_EXCLUDED` allow-list entry — the entry that would otherwise let the
allow-list degrade into a silencer.

## What is planted

Every path named in `platform/py32f071/CMakeLists.txt` resolves in this tree (the
forward check is clean). The manifest carries:

```
# PY32_EXCLUDED: src/boards/uno_rurp_shield.cpp
```

— a `PY32_EXCLUDED:` line with a path but **no `--` reason segment**. This must be a
violation in its own right, distinct from (and in addition to) the fact that
`src/boards/uno_rurp_shield.cpp` is a tree file omitted from `FIRESTARTER_COMMON_SOURCES`
with no *valid* allow-list entry covering it.

## Expected gate behaviour

`FIRESTARTER_MANIFEST_ROOT=<this dir> python3 scripts/check_cmake_manifest.py`:

- Exit **non-zero**
- Names the unreasoned entry and states that its reason is missing (`reason` appears in
  the FAIL output)

## Not real firmware source

Every `.cpp` file under this tree is a short fixture stub with a header comment marking
it as fixture input, not a copy of the real firmware source of the same name.
