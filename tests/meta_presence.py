"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 129 Plan 01 -- D-03's meta-repo presence probe for the flash-path
record sync gate.

Requirements: PCB-01, PCB-02, PCB-03, PCB-04, PCB-05 (presence-probe slice
only -- this module does NOT close any of them, and no requirement is
marked complete in REQUIREMENTS.md from this plan).

Decisions covered: D-03, D-04

**Direction warning, unique to this module.** The relationship here is
*parent*, not sibling: `firestarter` is a **submodule of** the meta repo, so
the meta root is `Path(__file__).resolve().parent.parent.parent` -- one
level further up than `firestarter_app/tests/fw_presence.py`'s sibling
arithmetic (that module's app repo and the firmware repo are siblings under
a common parent; this repo's meta root IS that common parent). Under
`actions/checkout` of `henols/firestarter_fw` in isolation, that parent
directory is simply the runner's work directory and `.planning/` does not
exist there at all (RESEARCH F-14) -- an entirely ordinary absent-repo case,
not an error.

**`.git` is probed with `.exists()`, never a directory-only check.** A
submodule checkout stores `.git` as a *file* (a gitlink pointer into the
superproject's `.git/modules/...`); the superproject's own `.git` is a
*directory*. The marker probed here is the superproject's (meta repo)
marker, so under this project's real checkout it normally IS a directory --
but the code must not depend on that, because the exact same probe logic
runs against synthetic `tmp_path` fixtures in
`test_flash_path_record_sync.py`, some of which create `.git` as a plain
file to prove the probe does not silently require a directory.

**Import-time binding -- read this before writing a test against this
module.** `META_ROOT`, `META_MARKER`, `META_PRESENT`, `META_ABSENT_REASON`
and `requires_meta` are all evaluated once, at import (the
`FIRESTARTER_META_ROOT` env seam below is read at module scope, and
`pytest.mark.skipif` binds at collection). `monkeypatch.setenv` runs *after*
import and collection have already happened, so it has **no effect** on any
of these names. A test that needs a different `META_ROOT` must invoke
pytest in a **subprocess**, with `FIRESTARTER_META_ROOT` set in the child
process's environment -- never an in-process monkeypatch, never a direct
import of this module under a patched environment.

**CI coverage, stated honestly.** `pytest tests/ -v` at the `build.yml` step read
this session DOES fire on this branch -- that workflow's trigger was widened to
`push: branches: ['**', '!beta']`, documented in `build.yml`'s own header comment,
so it runs on every branch except `beta`; `beta-build.yml` runs the sibling leg on
`push: branches: [beta]`; and `py32f071.yml` still has no pytest step at all. Never
imply CI coverage where none exists, and never imply its ABSENCE where it does --
this paragraph exists because an earlier reading of this module made exactly that
inverse error.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# The one seam: only the ROOT path is overridable, never the marker name.
# ---------------------------------------------------------------------------
#
# Layout: firestarter/tests/meta_presence.py -> two parents up is the
# firmware repo root; its PARENT is the meta repo root (the firmware repo is
# a submodule OF the meta repo, not a sibling of it -- one level further up
# than firestarter_app/tests/fw_presence.py's sibling arithmetic). The read
# below is the ONLY environment lookup in this module -- the marker name
# stays hardcoded as `.git` on purpose. Making the marker name overridable
# too would be one more knob that can be set wrong in a real run.
_FW_REPO_ROOT = Path(__file__).resolve().parent.parent
_DEFAULT_META_ROOT = _FW_REPO_ROOT.parent

META_ROOT: Path = Path(os.environ.get("FIRESTARTER_META_ROOT", str(_DEFAULT_META_ROOT)))

# The meta-repo-presence marker. Probed with .exists() only -- never a
# directory-only check -- see the module docstring for why that distinction
# is load-bearing here.
META_MARKER: Path = META_ROOT / ".git"

META_PRESENT: bool = META_MARKER.exists()

# ONE canonical reason string, shared by every caller. Specific enough to be
# unambiguous in a `pytest -rs` report (names the exact marker path probed);
# the resolved path is interpolated so the claim is auditable by a reader --
# never a bare "meta repo absent" with nothing to check it against.
META_ABSENT_REASON: str = (
    f"meta repo checkout absent (no {META_MARKER} marker)"
)

# The ONLY skip marker any cross-repo test in this gate module may use.
requires_meta = pytest.mark.skipif(not META_PRESENT, reason=META_ABSENT_REASON)


class MissingScanTargetError(Exception):
    """Raised when the meta repo IS present but a named path under it is
    not.

    This is the hard-failure half of the split: under a present repo, a
    missing scan target means the scan target (or the cross-repo scan-path
    inventory) needs to be updated to match a real rename -- it must never
    be silently downgraded to a skip, because that is exactly the fail-open
    behaviour research finding A-7 measured (five gate legs flipping
    PASS -> SKIP at exit 0 with a false reason) and this milestone exists to
    remove.
    """


def meta_path(*parts: str) -> Path:
    """Join `parts` onto `META_ROOT` and return the resolved path.

    - If the meta repo is present (`META_PRESENT`) and the resulting path
      does not exist, raises `MissingScanTargetError` naming the resolved
      absolute path, the marker that proved the repo present, and the
      instruction to update the path (or the cross-repo scan-path
      inventory) rather than deleting the gate.
    - If the meta repo is absent, returns the path WITHOUT raising: the
      caller is expected to be behind `requires_meta` (or an equivalent
      absence check) already, and raising here too would turn an honest
      skip into a collection-time error.
    """
    resolved = META_ROOT.joinpath(*parts)
    if META_PRESENT and not resolved.exists():
        raise MissingScanTargetError(
            f"{resolved} does not exist, but the meta repo IS present "
            f"(marker found at {META_MARKER}). This scan target was "
            "renamed or moved -- update this path (or the cross-repo "
            "scan-path inventory) rather than removing or bypassing this "
            "gate."
        )
    return resolved
