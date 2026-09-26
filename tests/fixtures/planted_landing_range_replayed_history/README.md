# `planted_landing_range_replayed_history/`

A **recipe stub**, not a tree. `check_landing_range.py`'s subject matter is
git *history shape* -- a real nested `.git` directory cannot be committed
inside this repository (it would either be ignored, or worse, misread as a
submodule), so unlike every sibling `planted_*`/`clean_*` fixture under
`tests/fixtures/`, this entry does not carry the commits it describes.
Instead, the paired pytest (`tests/test_check_landing_range.py`) builds the
two shapes below **from scratch in `tmp_path`**, via `git init` plus a small
number of scripted commits, on every test run.

This directory exists so that
`tests/test_checker_convention.py::test_every_checker_has_planted_fixture`
finds a `planted_landing_range*` entry for `check_landing_range.py` — that
meta-test is what REQUIRES this stub to exist at all; it is satisfied by
this directory's mere presence, not by its (empty) contents.

## The two shapes this fixture describes

- **`squashed`** — ONE commit that adds the two portability marker files
  (`include/rurp_platform_compat.h`, `include/avr/pgmspace.h`) AND
  `platform/py32f071/` together, in the same commit. `check_landing_range.py`
  must report **0 violations** against a range built this way.
- **`replayed`** — TWO commits: the first adds only the two marker files;
  the second adds `platform/py32f071/` afterward. `check_landing_range.py`
  must report **exactly 1 violation**, naming the first (marker-only)
  commit, and must NEVER name the second (completing) commit.

Both shapes are built by `_make_repo(tmp_path, shape)` in
`tests/test_check_landing_range.py`, which `git init`s a throwaway
repository, commits a `README.md` as the fork point, records that SHA, then
applies the shape above.
