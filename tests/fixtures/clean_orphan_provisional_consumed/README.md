# `clean_orphan_provisional_consumed/`

The discriminating control for `check_orphan_provisional.py`: the same
`RURP_*_PROVISIONAL` macro shape as `planted_orphan_provisional_macro/`, but WITH
a genuine consumer, proving the gate can pass — a gate that only ever fails
would look correct on the planted fixture alone.

## What is here

`include/fixture_provisional.h` defines `RURP_FIXTURE_CONSUMED_PROVISIONAL`.
`src/fixture_consumer.cpp` references it in a real preprocessor conditional
(`#if RURP_FIXTURE_CONSUMED_PROVISIONAL`), not merely a comment — the gate must
be exercised on a genuine consumer.

`platform/py32f071/CMakeLists.txt` is the D-07 arming key, identical in role to
the sibling fixture and to `check_cmake_manifest.py`'s own fixtures.

## Expected gate behaviour

`FIRESTARTER_PROVISIONAL_ROOT=<this dir> python3 scripts/check_orphan_provisional.py`:

- Exit **0**
- `PASS:` line names `RURP_FIXTURE_CONSUMED_PROVISIONAL` with a non-zero consumer count

## Not real firmware source

Every file under `include/` and `src/` here is a short fixture stub carrying a
header comment marking it as fixture input — none is a copy of any real
firmware file.
