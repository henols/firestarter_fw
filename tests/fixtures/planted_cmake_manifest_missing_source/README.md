# `planted_cmake_manifest_missing_source/`

A miniature, self-contained firmware root proving `check_cmake_manifest.py`'s forward
check: a `FIRESTARTER_COMMON_SOURCES` entry that does not resolve in the tree.

## What is planted

`platform/py32f071/CMakeLists.txt` names three `FIRESTARTER_COMMON_SOURCES` entries:

- `${REPOSITORY_ROOT}/src/firestarter.cpp` — exists
- `${REPOSITORY_ROOT}/src/proms/eeprom_28c.cpp` — exists
- `${REPOSITORY_ROOT}/src/proms/flash_type_3.cpp` — **does NOT exist**

The missing entry is named after the real, confirmed v1.19 Phase 104 rename
(`flash_type_3.cpp` → `flash_nor_unlock.cpp`) that this gate exists to catch on the real
`platform/py32f071/CMakeLists.txt` once Phase 124 lands it (MERGE-02).

`PY32_SDK_SOURCES` names two `${PY32_SDK_ROOT}`-prefixed paths that also do not exist —
these must NOT be counted, because `PY32_SDK_SOURCES` is structurally exempt
(FetchContent). This fixture's tree contains exactly the two `src/` files named above
(nothing else under `src/`), so the reverse-omission check contributes zero additional
violations: the only violation this fixture should ever report is the one planted
missing-source entry.

## Expected gate behaviour

`FIRESTARTER_MANIFEST_ROOT=<this dir> python3 scripts/check_cmake_manifest.py`:

- Exit **non-zero**
- Reports exactly **1** violation (never 3 — the two SDK entries are exempt, not counted)
- Names `flash_type_3.cpp` in the FAIL output
- Does NOT mention `PY32_SDK_ROOT` in the FAIL output

## Not real firmware source

`src/firestarter.cpp` and `src/proms/eeprom_28c.cpp` here are short fixture stub files
carrying a header comment marking them as fixture input — neither is a copy of the real
firmware source of the same name.
