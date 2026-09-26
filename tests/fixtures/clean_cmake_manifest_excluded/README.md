# `clean_cmake_manifest_excluded/`

The armed-but-clean control: proves `check_cmake_manifest.py` distinguishes a
*deliberate, reasoned* omission from rename damage — ROADMAP criterion 3's second half.

## What is present

Every path named in `platform/py32f071/CMakeLists.txt` resolves in this tree (the
forward check is clean, same as the real, un-defective state Phase 124's fix produces).
`src/boards/uno_rurp_shield.cpp` exists in the tree but is deliberately NOT named in
`FIRESTARTER_COMMON_SOURCES`; it is instead covered by a well-formed allow-list line:

```
# PY32_EXCLUDED: src/boards/uno_rurp_shield.cpp -- AVR board impl, no ARM analogue
```

## Expected gate behaviour

`FIRESTARTER_MANIFEST_ROOT=<this dir> python3 scripts/check_cmake_manifest.py`:

- Exit **0**
- `PASS:` line names the allow-listed omission (`src/boards/uno_rurp_shield.cpp`)

## Not real firmware source

Every `.cpp` file under this tree is a short fixture stub with a header comment marking
it as fixture input, not a copy of the real firmware source of the same name.
