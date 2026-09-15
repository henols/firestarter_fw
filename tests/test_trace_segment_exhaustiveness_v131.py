"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 144 Plan 04 (TEST-06 / D-07) -- authors D-07's exhaustiveness gate: a
pytest module that parses BOTH v131 trace fixtures (the frozen pre-change
cadence and the new post-v1.31 cadence), walks each of the six arrays with a
structural state machine, partitions every entry into exactly one of six
named segments, and asserts that every segment present in either stream
carries a named attributing decision from Phases 140-143.

Requirements: TEST-06 (this plan does NOT mark it Complete -- 144-04-PLAN.md's
own scope note assigns all eight requirement flips of Phase 144 to plan
144-07; this module is evidence for that later flip, not the flip itself).

Defect class this closes: TEST-06 requires "every changed strobe attributable
to a named decision" -- "counts-plus-narrative" (a blanket snapshot update
wearing a paragraph) was explicitly rejected as satisfying this. A COUNT
check alone cannot prove attribution completeness: one entry deleted and one
duplicated leaves an array's length unchanged and must still be caught. This
module makes the attribution machine-checked for completeness -- 1001 entries
(620 pre-change + 381 new), every one landing in exactly one attributed
segment, an unattributed or unclassifiable entry failing loudly and
locatably.

**Honest boundary, stated once here and repeated in the per-test docstrings
below: this gate proves the attribution is COMPLETE (no segment is
unattributed, no entry is unsegmented) -- it does NOT prove that any single
attribution is itself CORRECT. Correctness of "which decision explains this
segment" is a human judgement recorded in the phase record (Phases 140-143);
completeness of "every entry lands somewhere named" is what is machine
checked here.**

Coverage:
  1. test_prechange_arrays_parse_to_the_recorded_lengths -- the frozen
     pre-change fixture's three arrays parse to exactly (198, 221, 201)
     entries, positional, first-divergence message naming index/expected/
     observed.
  2. test_new_arrays_parse_to_the_captured_lengths -- same shape against the
     new fixture's (131, 149, 101).
  3. test_every_entry_falls_in_exactly_one_segment -- D-07's core assertion:
     for each of the six arrays, the union of the six segment index sets
     equals set(range(len(entries))) AND the six sets are pairwise disjoint.
     Set equality and disjointness -- never a count sum. On failure, names
     the array, stream, every uncovered/duplicated index and its
     (kind,pin,value,us) tuple.
  4. test_every_present_segment_has_a_named_attribution -- every one of the
     six names in _SEGMENTS carries a non-empty _SEGMENT_ATTRIBUTION entry
     (checked unconditionally), and every segment with at least one
     attributed index in either stream is re-checked, naming the orphan
     segment on failure.
  5. test_pre_change_0x07_pulse_and_verify_counts_match_the_output_enable_toggles
     -- the F-07 known-answer self-test: on the pre-change 0x07 array the
     machine must find 7 pulse windows and 12 verify reads, and 7 + 12 must
     equal the independently-recounted total of pin==_PIN_OE strobes (19).
     This is what proves the state machine is not merely self-consistent.
  6. test_total_attributed_entry_count_matches_the_floor -- the six arrays' ATTRIBUTED
     totals (derived from _segment_indices, never the raw parsed length
     alone) sum to _TOTAL_ENTRY_FLOOR, with the 620/381 per-stream subtotals
     named in the message.
  7. test_scan_targets_are_non_vacuous -- self-protection, two halves. Part
     (a) recomputes both DEFAULT paths from _REPO_ROOT WITHOUT reading
     os.environ, asserting each is a non-empty file inside the repo. Part
     (b) runs the parser against whatever the two seams CURRENTLY resolve
     to, asserting three arrays each with a non-empty entry list, and a
     combined total >= _TOTAL_ENTRY_FLOOR.
  8. test_this_module_cannot_be_silently_skipped -- this module's own
     source contains no skip-bypass call, no skip-marker decorator, and no
     dependency-skip call anywhere, each needle concatenation-built.
  9. test_own_needles_do_not_appear_verbatim_in_this_module -- every
     concatenation-built needle from Coverage 8 appears NOWHERE verbatim in
     this module's own source.
  10. test_planted_unclassifiable_entry_is_located -- D-18 Plant A. One
      entry in a SCRATCH copy of the new fixture is mutated to an
      unclassifiable shape (pin 0x40, none of the five known pins), run
      through Coverage 3's own leg in a CHILD PROCESS (the seam binds at
      import; monkeypatch.setenv cannot reach it), and the RED transcript
      must name the array, the positional index, and the (kind,pin,value,us)
      tuple containing 0x40.
  11. test_planted_delete_and_duplicate_defeats_a_count_only_check -- D-18
      Plant B. One entry is deleted and a DIFFERENT entry is duplicated
      inside the SAME array of a scratch copy, so the array's length is
      UNCHANGED -- proving a count-only check would pass -- yet the
      partition leg still fails, and the failure is attributable to the
      set-equality/disjointness assertion, not to a length mismatch.

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover them,
mirroring tests/test_ack_layout_source_contract_v143.py's and
tests/test_requirement_case_mapping_v131.py's own convention)
  - FIRESTARTER_TRACE_SEGMENT_SCAN_NEW -- overrides the scanned PATH to the
    new (post-v1.31) fixture ONLY -- never a segment name, never an expected
    length, never the _TOTAL_ENTRY_FLOOR denominator. Binds at IMPORT time (the
    module-level `Path(os.environ.get(...))` expression below), so a
    planted-violation run must set it in a CHILD PROCESS environment before
    this module is imported, never via a post-import monkeypatch
    (monkeypatch.setenv has no effect on an already-bound module-level
    value). Coverage 10 and 11 are the two legs that set it, always through
    `_run_gate_in_subprocess`, never through monkeypatch.setenv (S6).
  - FIRESTARTER_TRACE_SEGMENT_SCAN_PRECHANGE -- overrides the scanned PATH
    to the frozen pre-change fixture ONLY, same path-only/import-time-bound
    rules as above. No planted leg in this module redirects this seam (both
    plants mutate the NEW stream only), but it is read on every import and
    documented here for the same discoverability reason.
  - FIRESTARTER_144_GATE_CHILD -- a child-process recursion guard only, read
    (never written by anything but `_run_gate_in_subprocess` itself) to
    refuse spawning a nested gate subprocess from inside one. It is not a
    behavioural seam: it carries no path, no floor and no segment name, and
    no test above reads it for any other purpose. Reused (not reinvented)
    from tests/test_requirement_case_mapping_v131.py's own
    FIRESTARTER_144_GATE_CHILD, itself copied structurally from
    tests/test_flash_path_record_sync.py's FIRESTARTER_129_GATE_CHILD.

This module is a standalone pytest module: it is not named check_*.py (see
tests/test_checker_convention.py's FLOOR=6 / FIXTURE_FLOOR=15, both at
exactly the current counts -- authoring a scripts/check_*.py checker here
would obligate raising both in the same commit, which this plan does not
do), it adds no shared pytest configuration or fixture-registration file
anywhere (firestarter/tests/ has none, by house convention, and this module
does not introduce one), and it imports nothing beyond the Python standard
library: os, re and pathlib for the parse/segmentation/self-protection legs
(Coverage 1-9); subprocess, shutil and sys additionally for the D-18
planted-violation legs' child-process runs and fail-closed git resolution
(Coverage 10-11). It never imports _parse_arrays, _ARRAY_DECL_RE or any
other helper defined in test_golden_trace_identity_eprom_v131.py -- that
module's own docstring requires two INDEPENDENT readings, so the parse
technique below is re-implemented, never imported. It never edits src/
(D-04) and
never modifies either fixture header -- both are read-only inputs here;
every planted mutation below is a SCRATCH copy under tmp_path.

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo; a recorded house-rule
pattern, not an omission). Stdlib and pytest only.
"""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_NEW_REL = "test/native/avr/_shared/eprom_v131_expected.h"
_PRECHANGE_REL = "test/native/avr/_shared/eprom_v131_expected_prechange.h"

# Environment seams -- bind at IMPORT time. See the module docstring's
# "Environment seams" section above. Each overrides a PATH ONLY (S5).
_SCAN_NEW = Path(
    os.environ.get("FIRESTARTER_TRACE_SEGMENT_SCAN_NEW", str(_REPO_ROOT / _NEW_REL))
)
_SCAN_PRECHANGE = Path(
    os.environ.get("FIRESTARTER_TRACE_SEGMENT_SCAN_PRECHANGE", str(_REPO_ROOT / _PRECHANGE_REL))
)

# The two fixtures' REAL sources -- resolved via _REPO_ROOT directly (never
# via the seams above), because these paths name the ONE true copy the S2
# ceremony in the planted-violation tests below must prove untouched,
# regardless of whatever scratch path a planted run happens to point a seam
# at.
_REAL_NEW_PATH = _REPO_ROOT / _NEW_REL
_REAL_PRECHANGE_PATH = _REPO_ROOT / _PRECHANGE_REL

# Entry kind/pin vocabulary -- from the fixture's own macros and
# include/rurp_shield.h:53-57 (verified this session).
_KIND_STROBE_DATA = 1
_KIND_STROBE_PIN = 2
_KIND_DELAY_US = 3
_KIND_DELAY_MS = 4

_PIN_LSB = 0x01   # LEAST_SIGNIFICANT_BYTE
_PIN_MSB = 0x02   # MOST_SIGNIFICANT_BYTE
_PIN_OE = 0x04    # OUTPUT_ENABLE -- the program-vs-verify discriminator
_PIN_CTRL = 0x08  # CONTROL_REGISTER -- HV route / top-address latch
_PIN_CE = 0x20    # CHIP_ENABLE -- the pulse strobe AND the verify-read strobe

# Parse -- re-implemented (never imported) from
# test_golden_trace_identity_eprom_v131.py's own _ARRAY_DECL_RE/_parse_arrays
# technique (:131-144), extended to capture all four fields per entry
# (verified by RESEARCH against all 620 pre-change entries with zero
# misses). Comment-stripping happens BEFORE matching entries, which is what
# makes the new capture's `/* N */` positional-index comments harmless --
# and is also why a comment-KEYED classifier is impossible here: the new
# stream carries no other comment content at all, so any rule keyed on
# comment text would classify zero of its 381 entries.
_ARRAY_DECL_RE = re.compile(
    r"static const v131_trace_entry_t\s+(\w+)\[\]\s*=\s*\{(.*?)\};",
    re.DOTALL,
)
_ENTRY_FIELDS_RE = re.compile(
    r"\{\s*(\d+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(\d+)UL\s*\}"
)


def _strip_comments(body):
    """Strip `/* ... */` block comments then `//` line comments from an
    array body, block first -- copied structurally from
    test_golden_trace_identity_eprom_v131.py:140-141's own two-step order."""
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", "", body)
    return body


def _parse_arrays_with_fields(path):
    """Re-derive the ordered [(name, [(kind, pin, value, us), ...]), ...]
    list from a trace fixture's raw text, independently of any other
    module's parse. Returns an empty list for a missing/unreadable path
    rather than raising -- the non-vacuity legs below (Coverage 7, and the
    length legs 1/2) are what turn that emptiness into a locating,
    fail-closed AssertionError."""
    path = Path(path)
    if not path.is_file():
        return []
    text = path.read_text()
    arrays = []
    for m in _ARRAY_DECL_RE.finditer(text):
        name = m.group(1)
        body = _strip_comments(m.group(2))
        entries = []
        for em in _ENTRY_FIELDS_RE.finditer(body):
            kind = int(em.group(1))
            pin = int(em.group(2), 16)
            value = int(em.group(3), 16)
            us = int(em.group(4))
            entries.append((kind, pin, value, us))
        arrays.append((name, entries))
    return arrays


def _format_entry(entry):
    """Render a (kind, pin, value, us) tuple in the same field order the
    fixture's own C literal uses, for a locating failure message."""
    kind, pin, value, us = entry
    return f"(kind={kind}, pin=0x{pin:02X}, value=0x{value:02X}, us={us})"


_SEGMENTS = ("init", "route_assert", "address_set", "pulse", "verify_read", "teardown")

# Restated from the module docstring: this map proves attribution is
# COMPLETE (every present segment is named), not that any one citation is
# CORRECT -- correctness of "why" is the phase record's own judgement.
_SEGMENT_ATTRIBUTION = {
    "init": (
        "Phase 140 (eprom_params_t.vpp_path column, eprom_params.cpp): supplies "
        "which HV route this one-time VPP-regulator-enable group's "
        "CONTROL_REGISTER value asserts.",
        "Phase 142 D-05/VPP-01/VPP-03 (eprom_hv_route_mask()): resolves that "
        "value through one shared function rather than a duplicated "
        "protocol== predicate.",
    ),
    "route_assert": (
        "Phase 142 D-01/D-02 (as amended): the HV route mask now survives "
        "every set_address() call in the block, so this CONTROL_REGISTER "
        "group latches once (lazily, on first use) instead of once per pass "
        "-- the reason this segment's entry count falls sharply between the "
        "two streams (31/54/30 pre-change vs 4/28/0 new).",
        "Phase 142 D-05/VPP-01/VPP-03 (eprom_hv_route_mask()): resolves the "
        "asserted CONTROL_REGISTER value from the row's vpp_path column "
        "rather than a duplicated protocol== predicate. This segment covers "
        "every CONTROL_REGISTER latch group that still has data/CE activity "
        "after it -- both the initial assert AND any later release that is "
        "not the stream's final group -- per D-07's own naming.",
    ),
    "address_set": (
        "Phase 141 D-01 (the shared per-byte pulse-to-verify loop): each "
        "byte's address is latched once per byte-visit rather than once per "
        "old-cadence pass, which is why this segment's count falls by "
        "roughly a factor of three between streams (three old passes "
        "collapse to one new per-byte visit).",
    ),
    "pulse": (
        "Phase 140 (eprom_params_t per-row pulse width, eprom_params.cpp): "
        "the pulse width is a DATABASE datum, not a protocol constant, and "
        "stays FIXED across attempts for a given byte -- no more adaptive "
        "per-retry growth.",
        "Phase 141 D-01/D-02 (the shared per-byte pulse-to-verify loop): "
        "fixed-width pulse, verify, repeat -- replacing the old pass-based "
        "program-everything-then-verify-everything cadence.",
    ),
    "verify_read": (
        "Phase 140 (eprom_params_t.verify_mode column): VERIFY_PER_PULSE_"
        "PLUS_FINAL / VERIFY_PER_PULSE determines whether one additional "
        "full-array verify pass runs after every byte has converged.",
        "Phase 141 D-01/D-06 (the per-byte loop's per-pulse verify, and the "
        "FF-rule's own final-pass carve-out -- "
        "test_loop06_the_ff_rule_does_not_suppress_the_final_verify_pass): "
        "an FF-target byte is skipped by the per-byte loop itself but is "
        "still verified once during the trailing final pass.",
    ),
    "teardown": (
        "Phase 143 D-09/D-10 (as amended): a SUCCESSFUL block deliberately "
        "leaves the HV route energised rather than disabling it (only an "
        "ERROR exit disables every route, through a single-exit wrapper), "
        "so this segment contributes ZERO entries to either stream captured "
        "here -- both captures are of a successful synthetic 4-byte block. "
        "Recorded explicitly as zero, never omitted from this map.",
    ),
}

# RE-ANCHORED AGAIN by debug session w27c512-write-slow-3x, from 981 / 361
# (121+148+92) to 1001 / 381 (131+149+101). That session replaced the per-BYTE
# program loop with a PASS-BATCHED one -- the program-voltage route is now
# asserted once per PASS rather than once per programmed byte, and the scan
# pass that decides the next pass contributes its own read-back traffic where
# the per-byte loop interleaved a verify read with each pulse. The per-byte
# cadence the previous literals described WAS the ~3.2x write-speed regression
# that session fixed (105.89 s -> 33.51 s for a 64 KiB W27C512 on a leonardo,
# byte-exact), so this growth is expected work for that change and a
# regression for any later one. Both subtotals were re-derived from the
# fixtures by the same parse this module uses, not hand-counted. The
# PRE-CHANGE subtotal is STILL untouched at 620: that fixture stays frozen and
# was not re-captured by this session either.
_PRECHANGE_EXPECTED = (198, 221, 201)
_NEW_EXPECTED = (131, 149, 101)
_TOTAL_ENTRY_FLOOR = 1001  # 620 (pre-change) + 381 (new)


# The six-segment state machine.
#
# Design note (why a "match, don't consume-and-raise" shape): entries are
# classified by testing, at each position, whether one of a small number of
# known GROUP idioms (a 4-entry latch group, a 5-entry pulse-payload group,
# or a 3-entry chip-enable window) matches STARTING there. A recognised
# idiom's indices are claimed for its segment; a single OUTPUT_ENABLE toggle
# or CHIP_ENABLE window is claimed via the last-latched OE value (_PIN_CE
# alone cannot separate `pulse` from `verify_read` -- it appears in both, so
# OUTPUT_ENABLE is the only valid discriminator -- a per-entry field lookup
# is provably insufficient here). An entry whose (kind, pin) is not even in
# the five-pin/four-kind vocabulary at all (Plant A's shape) is a hard,
# located AssertionError raised immediately by _validate_known_primitive
# below. An entry that IS in-vocabulary but does not complete any
# recognised GROUP shape (which is what a delete-and-duplicate mutation
# produces, Plant B's shape) is left OUT of every segment's set rather than
# raised -- so the exhaustiveness assertion in
# test_every_entry_falls_in_exactly_one_segment (a genuine, non-tautological
# check on the RETURNED sets) is what catches it, distinctly from Plant A's
# immediate raise.


def _validate_known_primitive(entry, index, array_name):
    """Raise a locating AssertionError if `entry`'s (kind, pin) is not one
    of the four recognised kinds / five recognised pins at all. This is the
    ONLY raise path in this state machine -- see the design note above."""
    kind, pin, value, us = entry
    if kind == _KIND_STROBE_DATA:
        return
    if kind == _KIND_STROBE_PIN:
        if pin in (_PIN_LSB, _PIN_MSB, _PIN_OE, _PIN_CTRL, _PIN_CE):
            return
        raise AssertionError(
            f"{array_name}: index {index} is unclassifiable -- "
            f"{_format_entry(entry)} matches none of the five known pins "
            f"(LSB=0x{_PIN_LSB:02X}, MSB=0x{_PIN_MSB:02X}, OE=0x{_PIN_OE:02X}, "
            f"CTRL=0x{_PIN_CTRL:02X}, CE=0x{_PIN_CE:02X})."
        )
    if kind in (_KIND_DELAY_US, _KIND_DELAY_MS):
        return
    raise AssertionError(
        f"{array_name}: index {index} is unclassifiable -- "
        f"{_format_entry(entry)} has an unrecognized kind "
        f"(known kinds: STROBE_DATA={_KIND_STROBE_DATA}, "
        f"STROBE_PIN={_KIND_STROBE_PIN}, DELAY_US={_KIND_DELAY_US}, "
        f"DELAY_MS={_KIND_DELAY_MS})."
    )


def _matches_latch_group(entries, i, target_pins):
    """Does the 4-entry idiom DATA / LE-high(pin, 1) / 1us settle /
    LE-low(pin, 0) match starting at index i, for pin in target_pins?"""
    n = len(entries)
    if i + 3 >= n:
        return False
    a, b, c, d = entries[i], entries[i + 1], entries[i + 2], entries[i + 3]
    if a[0] != _KIND_STROBE_DATA:
        return False
    if not (b[0] == _KIND_STROBE_PIN and b[1] in target_pins and b[2] == 1):
        return False
    if not (c[0] == _KIND_DELAY_US and c[1] == 0 and c[3] == 1):
        return False
    if not (d[0] == _KIND_STROBE_PIN and d[1] == b[1] and d[2] == 0):
        return False
    return True


def _matches_payload_group(entries, i):
    """Does the 5-entry idiom DATA(payload) / settle / CE-low / DELAY_US
    (width) / CE-high match starting at index i?"""
    n = len(entries)
    if i + 4 >= n:
        return False
    data, settle, ce_lo, width, ce_hi = (
        entries[i], entries[i + 1], entries[i + 2], entries[i + 3], entries[i + 4]
    )
    if data[0] != _KIND_STROBE_DATA:
        return False
    if settle[0] != _KIND_DELAY_US or settle[1] != 0:
        return False
    if not (ce_lo[0] == _KIND_STROBE_PIN and ce_lo[1] == _PIN_CE and ce_lo[2] == 0):
        return False
    if width[0] != _KIND_DELAY_US or width[1] != 0:
        return False
    if not (ce_hi[0] == _KIND_STROBE_PIN and ce_hi[1] == _PIN_CE and ce_hi[2] == 1):
        return False
    return True


def _matches_ce_window(entries, i):
    """Does the bare 3-entry idiom CE-low / DELAY_US(access) / CE-high
    match starting at index i (the verify-read shape, with no address
    change and no payload write)?"""
    n = len(entries)
    if i + 2 >= n:
        return False
    ce_lo, width, ce_hi = entries[i], entries[i + 1], entries[i + 2]
    if not (ce_lo[0] == _KIND_STROBE_PIN and ce_lo[1] == _PIN_CE and ce_lo[2] == 0):
        return False
    if width[0] != _KIND_DELAY_US or width[1] != 0:
        return False
    if not (ce_hi[0] == _KIND_STROBE_PIN and ce_hi[1] == _PIN_CE and ce_hi[2] == 1):
        return False
    return True


def _segment_indices(entries, array_name="<array>"):
    """The state machine. Returns a dict mapping each of the six
    _SEGMENTS names to a set of entry indices. Raises a locating
    AssertionError (naming `array_name`, the positional index, and the
    (kind,pin,value,us) tuple) for any entry whose (kind, pin) is not in
    the recognised vocabulary at all -- see the design note above this
    function for why an in-vocabulary entry that fails to complete a group
    shape is instead left uncovered rather than raised here."""
    n = len(entries)
    segs = {name: set() for name in _SEGMENTS}

    # Vocabulary pass -- raises immediately on a truly foreign (kind, pin).
    for i, e in enumerate(entries):
        _validate_known_primitive(e, i, array_name)

    # init -- from index 0 through the first DELAY_MS whose us is 500: the
    # one-time VPP-regulator enable latch group plus its settle.
    init_end = None
    for idx, e in enumerate(entries):
        if e[0] == _KIND_DELAY_MS and e[3] == 500:
            init_end = idx
            break
    if init_end is None:
        raise AssertionError(
            f"{array_name}: no init boundary found -- expected a "
            "(kind=DELAY_MS, us=500) entry marking the end of the one-time "
            "VPP-regulator enable group, found none in "
            f"{len(entries)} entries."
        )
    for idx in range(0, init_end + 1):
        segs["init"].add(idx)

    # last_activity_idx -- the highest index carrying a DATA write or a CE
    # strobe. A CONTROL_REGISTER latch group ending at or before this index
    # still has data/CE activity after it (route_assert); one ending after
    # it does not (teardown). Precomputed once over the whole array.
    last_activity_idx = -1
    for idx, e in enumerate(entries):
        if e[0] == _KIND_STROBE_DATA or (e[0] == _KIND_STROBE_PIN and e[1] == _PIN_CE):
            last_activity_idx = idx

    i = init_end + 1
    last_oe = None
    while i < n:
        e = entries[i]
        matched = False

        if _matches_latch_group(entries, i, (_PIN_LSB, _PIN_MSB)):
            for gi in range(i, i + 4):
                segs["address_set"].add(gi)
            i += 4
            matched = True
        elif _matches_latch_group(entries, i, (_PIN_CTRL,)):
            group_end = i + 3
            seg_name = "route_assert" if last_activity_idx > group_end else "teardown"
            for gi in range(i, i + 4):
                segs[seg_name].add(gi)
            i += 4
            # Optional trailing bare settle (e.g. the 10ms post-assert
            # settle, or the 4us P1-route settle) -- absorbed into the SAME
            # segment as the group it settles, only when present.
            if i < n and entries[i][0] in (_KIND_DELAY_US, _KIND_DELAY_MS) and entries[i][1] == 0:
                segs[seg_name].add(i)
                i += 1
            matched = True
        elif (
            e[0] in (_KIND_DELAY_US, _KIND_DELAY_MS)
            and e[1] == 0
            and _matches_latch_group(entries, i + 1, (_PIN_CTRL,))
        ):
            # A bare settle that LEADS a CONTROL latch group rather than
            # trailing one (debug session w27c512-program-fail-byte0). The
            # arm above already absorbs a trailing settle -- the route
            # ASSERT's EPROM_VPP_SETUP_US, and before it the old 10 ms
            # post-assert settle and the 4 us P1-route settle. The route
            # RELEASE has the mirror shape: EPROM_VPP_HOLD_US holds the
            # program voltage valid for the datasheet's TVS after /CE rises,
            # and only THEN is the route lowered, so the settle sits before
            # its group, not after it. Without this arm those entries are
            # in-vocabulary but complete no recognised group and fall out
            # uncovered -- which is exactly what Coverage 3 caught, and it
            # is a real gap in the attribution rather than a new entry kind.
            group_end = i + 4
            seg_name = "route_assert" if last_activity_idx > group_end else "teardown"
            for gi in range(i, i + 5):
                segs[seg_name].add(gi)
            i += 5
            # The same optional trailing settle the arm above allows -- a
            # group can be both led and trailed by one.
            if i < n and entries[i][0] in (_KIND_DELAY_US, _KIND_DELAY_MS) and entries[i][1] == 0:
                segs[seg_name].add(i)
                i += 1
            matched = True
        elif e[0] == _KIND_STROBE_PIN and e[1] == _PIN_OE and e[2] == 1:
            segs["pulse"].add(i)
            last_oe = 1
            i += 1
            matched = True
        elif e[0] == _KIND_STROBE_PIN and e[1] == _PIN_OE and e[2] == 0:
            segs["verify_read"].add(i)
            last_oe = 0
            i += 1
            matched = True
        elif e[0] == _KIND_STROBE_DATA and _matches_payload_group(entries, i):
            for gi in range(i, i + 5):
                segs["pulse"].add(gi)
            i += 5
            matched = True
        elif (
            e[0] == _KIND_STROBE_PIN
            and e[1] == _PIN_CE
            and e[2] == 0
            and _matches_ce_window(entries, i)
        ):
            seg_name = "pulse" if last_oe == 1 else "verify_read"
            for gi in range(i, i + 3):
                segs[seg_name].add(gi)
            i += 3
            matched = True

        if not matched:
            # In-vocabulary but does not complete any recognised group
            # shape here -- leave uncovered (see design note above) and
            # advance by exactly one so the scan can resynchronise.
            i += 1

    return segs


# Tests -- parse lengths (Coverage 1-2).


def test_prechange_arrays_parse_to_the_recorded_lengths():
    """Coverage 1 -- the frozen pre-change fixture's three arrays parse to
    exactly (198, 221, 201) entries, positional, first-divergence message
    shape copied structurally from
    test_golden_trace_identity_eprom_v131.py:177-195."""
    arrays = _parse_arrays_with_fields(_SCAN_PRECHANGE)
    names = [name for name, _entries in arrays]
    observed = tuple(len(entries) for _name, entries in arrays)

    n = min(len(_PRECHANGE_EXPECTED), len(observed))
    for i in range(n):
        if observed[i] != _PRECHANGE_EXPECTED[i]:
            raise AssertionError(
                f"first divergence at index {i} -- "
                f"array={names[i]!r} expected={_PRECHANGE_EXPECTED[i]} "
                f"observed={observed[i]}"
            )
    assert len(observed) == len(_PRECHANGE_EXPECTED), (
        f"array count diverged after {n} matching entries -- "
        f"expected {len(_PRECHANGE_EXPECTED)} arrays {_PRECHANGE_EXPECTED}, "
        f"observed {len(observed)} {observed} (names={names})"
    )


def test_new_arrays_parse_to_the_captured_lengths():
    """Coverage 2 -- the new post-v1.31 fixture's three arrays parse to
    exactly (131, 149, 101) entries, positional, same first-divergence shape
    as Coverage 1."""
    arrays = _parse_arrays_with_fields(_SCAN_NEW)
    names = [name for name, _entries in arrays]
    observed = tuple(len(entries) for _name, entries in arrays)

    n = min(len(_NEW_EXPECTED), len(observed))
    for i in range(n):
        if observed[i] != _NEW_EXPECTED[i]:
            raise AssertionError(
                f"first divergence at index {i} -- "
                f"array={names[i]!r} expected={_NEW_EXPECTED[i]} "
                f"observed={observed[i]}"
            )
    assert len(observed) == len(_NEW_EXPECTED), (
        f"array count diverged after {n} matching entries -- "
        f"expected {len(_NEW_EXPECTED)} arrays {_NEW_EXPECTED}, "
        f"observed {len(observed)} {observed} (names={names})"
    )


# Tests -- the exhaustiveness gate itself (Coverage 3-6).


def test_every_entry_falls_in_exactly_one_segment():
    """Coverage 3 -- D-07's core assertion. For each of the six arrays
    (three pre-change, three new), assert the union of the six segment
    index sets equals set(range(len(entries))) AND that the six sets are
    pairwise disjoint. Set equality and disjointness -- never a count sum:
    a count match alone can hide a double-count paired with a drop (F-07).
    On failure, names the array, the stream, every uncovered/duplicated
    index, and each one's (kind,pin,value,us) tuple via _format_entry."""
    streams = (("prechange", _SCAN_PRECHANGE), ("new", _SCAN_NEW))
    for stream_label, path in streams:
        for array_name, entries in _parse_arrays_with_fields(path):
            label = f"{array_name} ({stream_label})"
            segs = _segment_indices(entries, array_name=label)
            full_range = set(range(len(entries)))
            union = set()
            for seg_name in _SEGMENTS:
                union |= segs[seg_name]

            missing = sorted(full_range - union)
            if missing:
                detail = ", ".join(
                    f"index {idx} {_format_entry(entries[idx])}" for idx in missing
                )
                raise AssertionError(
                    f"{label}: the union of the six segment index sets does "
                    f"NOT equal set(range(len(entries))) -- {len(missing)} "
                    f"index(es) uncovered by any segment: {detail}. A "
                    "count-only check (comparing totals) would NOT have "
                    "caught this -- coverage, not count, is what this "
                    "assertion proves."
                )

            for i, seg_a in enumerate(_SEGMENTS):
                for seg_b in _SEGMENTS[i + 1:]:
                    overlap = sorted(segs[seg_a] & segs[seg_b])
                    if overlap:
                        detail = ", ".join(
                            f"index {idx} {_format_entry(entries[idx])}" for idx in overlap
                        )
                        raise AssertionError(
                            f"{label}: segments {seg_a!r} and {seg_b!r} are "
                            f"NOT disjoint -- {len(overlap)} index(es) "
                            f"claimed by both: {detail}. A count-only check "
                            "would NOT have caught this double-count."
                        )


def test_every_present_segment_has_a_named_attribution():
    """Coverage 4 -- every one of the six names in _SEGMENTS carries a
    non-empty _SEGMENT_ATTRIBUTION entry, checked UNCONDITIONALLY
    (teardown's own entry is present with a zero attributed-index count in
    every array, per Phase 143 D-09/D-10, and must still carry a citation).
    Then, for every segment that has at least one attributed index in
    EITHER stream, re-assert its attribution is non-empty, naming the
    orphan segment on failure."""
    for seg_name in _SEGMENTS:
        attribution = _SEGMENT_ATTRIBUTION.get(seg_name)
        assert attribution, (
            f"segment {seg_name!r} (one of the six names in _SEGMENTS) has "
            "no _SEGMENT_ATTRIBUTION entry, or an empty one -- every "
            "segment name must carry a non-empty tuple, even a segment "
            "that is never populated (recorded explicitly as zero, never "
            "omitted)."
        )

    present = set()
    for stream_label, path in (("prechange", _SCAN_PRECHANGE), ("new", _SCAN_NEW)):
        for array_name, entries in _parse_arrays_with_fields(path):
            segs = _segment_indices(entries, array_name=f"{array_name} ({stream_label})")
            for seg_name in _SEGMENTS:
                if segs[seg_name]:
                    present.add(seg_name)

    for seg_name in present:
        assert _SEGMENT_ATTRIBUTION.get(seg_name), (
            f"segment {seg_name!r} has at least one attributed index in "
            "one of the six arrays but carries no named attribution -- "
            "every present segment must cite a phase and decision."
        )


def test_pre_change_0x07_pulse_and_verify_counts_match_the_output_enable_toggles():
    """Coverage 5 -- the F-07 known-answer self-test. On the pre-change
    0x07 array the machine must find 7 pulse windows and 12 verify reads,
    and 7 + 12 must equal an INDEPENDENTLY recounted total of pin==_PIN_OE
    strobes (19, computed directly from the raw entries, never via segs).
    This is what proves the state machine is not merely self-consistent --
    a classifier that silently mis-bucketed entries could still partition
    exhaustively without ever being checked against a real, independently
    known answer."""
    arrays = dict(_parse_arrays_with_fields(_SCAN_PRECHANGE))
    array_name = "EPROM_V131_TRACE_PROTO_07"
    entries = arrays[array_name]
    segs = _segment_indices(entries, array_name=f"{array_name} (prechange)")

    pulse_windows = sum(
        1
        for idx in segs["pulse"]
        if entries[idx][0] == _KIND_STROBE_PIN and entries[idx][1] == _PIN_OE and entries[idx][2] == 1
    )
    verify_windows = sum(
        1
        for idx in segs["verify_read"]
        if entries[idx][0] == _KIND_STROBE_PIN and entries[idx][1] == _PIN_OE and entries[idx][2] == 0
    )
    # Independent recount -- does NOT use segs at all.
    total_oe_strobes = sum(1 for e in entries if e[0] == _KIND_STROBE_PIN and e[1] == _PIN_OE)

    assert pulse_windows == 7, (
        f"expected 7 pulse windows on the pre-change 0x07 array, found "
        f"{pulse_windows} (verify_windows={verify_windows}, "
        f"total_oe_strobes={total_oe_strobes})"
    )
    assert verify_windows == 12, (
        f"expected 12 verify-read windows on the pre-change 0x07 array, "
        f"found {verify_windows} (pulse_windows={pulse_windows}, "
        f"total_oe_strobes={total_oe_strobes})"
    )
    assert pulse_windows + verify_windows == total_oe_strobes, (
        f"pulse_windows + verify_windows ({pulse_windows} + {verify_windows} "
        f"= {pulse_windows + verify_windows}) does not equal the "
        f"independently recounted total OUTPUT_ENABLE strobe count "
        f"({total_oe_strobes}) -- the state machine is not correctly "
        "accounting for every OE toggle."
    )
    assert total_oe_strobes == 19, (
        f"expected 19 total OUTPUT_ENABLE strobes on the pre-change 0x07 "
        f"array, found {total_oe_strobes}"
    )


def test_total_attributed_entry_count_matches_the_floor():
    """Coverage 6 -- the six arrays' ATTRIBUTED totals (sum of
    len(segs[s]) for s in _SEGMENTS, derived from _segment_indices -- never
    the raw parsed length alone) sum to _TOTAL_ENTRY_FLOOR (1001), with the
    two per-stream subtotals (620 pre-change, 381 new) named in the
    message."""
    prechange_total = 0
    for array_name, entries in _parse_arrays_with_fields(_SCAN_PRECHANGE):
        segs = _segment_indices(entries, array_name=f"{array_name} (prechange)")
        prechange_total += sum(len(segs[s]) for s in _SEGMENTS)

    new_total = 0
    for array_name, entries in _parse_arrays_with_fields(_SCAN_NEW):
        segs = _segment_indices(entries, array_name=f"{array_name} (new)")
        new_total += sum(len(segs[s]) for s in _SEGMENTS)

    grand_total = prechange_total + new_total
    assert grand_total == _TOTAL_ENTRY_FLOOR, (
        f"attributed grand total is {grand_total}, expected "
        f"{_TOTAL_ENTRY_FLOOR} -- prechange subtotal={prechange_total} "
        f"(expected 620), new subtotal={new_total} (expected 381)"
    )
    assert prechange_total == 620, (
        f"prechange attributed subtotal is {prechange_total}, expected 620"
    )
    # 361 -> 381 (debug session w27c512-write-slow-3x): the pass-batched
    # program loop's re-frozen golden. Anchor only -- the assertion still
    # requires the ATTRIBUTED subtotal to equal the fixture's own parsed
    # length, so an unattributed entry still fails here.
    assert new_total == 381, f"new attributed subtotal is {new_total}, expected 381"


# Tests -- self-protection (Coverage 7-9).


def test_scan_targets_are_non_vacuous():
    """Coverage 7 -- structural self-check, two halves.

    Part (a): the DEFAULT scan targets are recomputed fresh from
    _REPO_ROOT WITHOUT reading os.environ at all (the
    check_permitted_claims.py `_HERE`-resolves-to-the-wrong-directory
    landmine, closed here by construction): each exists, is non-empty, and
    resolves inside this repository.

    Part (b): the parser, run against whatever _SCAN_NEW/_SCAN_PRECHANGE
    CURRENTLY resolve to (the SAME seam-aware targets every leg above
    scans), must yield three arrays each with a non-empty entry list per
    fixture, and a combined total >= _TOTAL_ENTRY_FLOOR. This is the half a
    planted EMPTY or misdirected scratch file turns RED, proving that
    scenario fails closed here instead of making every assertion above pass
    vacuously over an empty parse."""
    for rel in (_NEW_REL, _PRECHANGE_REL):
        default_target = _REPO_ROOT / rel
        assert default_target.is_file(), (
            f"default scan target {default_target} does not exist on disk "
            "-- a missing scan target must FAIL, never silently pass."
        )
        assert default_target.stat().st_size > 0, (
            f"default scan target {default_target} is empty."
        )
        assert default_target.resolve().is_relative_to(_REPO_ROOT), (
            f"default scan target {default_target} resolves outside "
            f"_REPO_ROOT ({_REPO_ROOT}) -- a naive future copy of this "
            "module into another directory must fail loudly here, not "
            "scan nothing and exit 0."
        )

    combined_total = 0
    for label, path in (("new", _SCAN_NEW), ("prechange", _SCAN_PRECHANGE)):
        arrays = _parse_arrays_with_fields(path)
        assert len(arrays) == 3, (
            f"{label} scan target {path} parsed to {len(arrays)} array(s), "
            "expected 3 -- an emptied or misdirected scan target must fail "
            "HERE, not make every downstream assertion pass vacuously over "
            "an empty set."
        )
        for name, entries in arrays:
            assert len(entries) > 0, (
                f"{label} array {name!r} (from {path}) parsed to an EMPTY "
                "entry list."
            )
            combined_total += len(entries)

    assert combined_total >= _TOTAL_ENTRY_FLOOR, (
        f"combined entry total across both CURRENT scan targets is "
        f"{combined_total}, expected >= {_TOTAL_ENTRY_FLOOR} -- an emptied "
        "or misdirected scan target must fail the non-vacuity leg, not make "
        "every partition/attribution check above pass vacuously over an "
        "empty set."
    )


# Concatenation-built needles -- Coverage 9 asserts none of these appear
# verbatim anywhere in this module's own source. See the module docstring:
# a gate that quotes its forbidden tokens verbatim matches itself and can
# never pass.
_NEEDLE_SKIP_CALL = "pytest" + ".skip"
_NEEDLE_SKIPIF_MARKER = "mark" + ".skipif"
_NEEDLE_DEPENDENCY_SKIP_CALL = "importor" + "skip"

_ALL_SELF_CHECK_NEEDLES = (
    ("a pytest skip call", _NEEDLE_SKIP_CALL),
    ("a pytest skipif marker", _NEEDLE_SKIPIF_MARKER),
    ("a pytest dependency-skip call", _NEEDLE_DEPENDENCY_SKIP_CALL),
)


def test_this_module_cannot_be_silently_skipped():
    """Coverage 8 -- this module's own source contains no runtime
    skip-bypass call, no skip-marker decorator, and no dependency-skip call
    anywhere, so the fail-closed contract this module documents is
    self-enforcing rather than merely stated. All three needles are built
    via concatenation so this test's own source and its own failure
    messages cannot match its own check."""
    own_text = Path(__file__).read_text()
    assert _NEEDLE_SKIP_CALL not in own_text, (
        "expected no " + _NEEDLE_SKIP_CALL + " call anywhere in this "
        "module -- a missing or empty scan target must FAIL, never SKIP."
    )
    assert _NEEDLE_SKIPIF_MARKER not in own_text, (
        "expected no @pytest." + _NEEDLE_SKIPIF_MARKER + " decorator "
        "anywhere in this module -- a missing or empty scan target must "
        "FAIL, never SKIP."
    )
    assert ("pytest." + _NEEDLE_DEPENDENCY_SKIP_CALL) not in own_text, (
        "expected no pytest." + _NEEDLE_DEPENDENCY_SKIP_CALL + " call "
        "anywhere in this module -- a missing dependency must FAIL, never "
        "SKIP."
    )


def test_own_needles_do_not_appear_verbatim_in_this_module():
    """Coverage 9 -- the concatenated-needle self-check: each of the three
    needles built by concatenation above (Coverage 8) must appear NOWHERE
    verbatim in this module's own source, including inside this very
    test's failure messages. Without this leg, a future edit could
    silently un-concatenate one of the needles (for example while
    "simplifying" the code) and this discipline would quietly stop being
    machine-checked."""
    own_text = Path(__file__).read_text()
    for label, needle in _ALL_SELF_CHECK_NEEDLES:
        assert needle not in own_text, (
            f"the concatenation-built needle for {label} appears verbatim "
            "in this module's own source -- rebuild it from at least two "
            "literal pieces so this gate cannot match itself."
        )


# A CHILD PROCESS is mandatory for both plants: _SCAN_NEW/_SCAN_PRECHANGE
# bind at IMPORT time, and monkeypatch.setenv cannot reach an already-
# imported module-level value (S6). Copied structurally from
# tests/test_requirement_case_mapping_v131.py:651-682's own
# `_run_gate_in_subprocess` and its FIRESTARTER_144_GATE_CHILD recursion
# guard (itself copied from tests/test_flash_path_record_sync.py's
# FIRESTARTER_129_GATE_CHILD), extended here to accept more than one node
# id at once (Plant B scopes to two legs in a single child run).


def _resolve_git():
    """Resolve the `git` binary, fail-closed -- copied structurally from
    test_golden_trace_identity_eprom_v131.py's own `_resolve_git` via
    test_requirement_case_mapping_v131.py's copy. Deliberately never
    bypassed via any decorator or runtime call that would mark this outcome
    as skipped: a missing `git` must FAIL this suite, never be silently
    skipped, or the S2 ceremony below could never prove the real fixtures
    are untouched."""
    git_bin = shutil.which(os.environ.get("GIT", "git"))
    assert git_bin is not None, (
        "git not found on PATH (checked $GIT, falling back to 'git'). This "
        "must FAIL the suite, never be silently skipped -- a missing git "
        "would otherwise turn the S2 real-fixtures-untouched ceremony into "
        "a silent no-op."
    )
    return git_bin


def _git_hash_object(path):
    """Resolve git fail-closed and hash-object `path` (list-form argv,
    shell=False)."""
    git_bin = _resolve_git()
    result = subprocess.run(
        [git_bin, "hash-object", str(path)],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def _git_porcelain(path):
    """Resolve git fail-closed and return `git status --porcelain` for
    `path` (list-form argv, shell=False). Empty output means a clean
    tree."""
    git_bin = _resolve_git()
    result = subprocess.run(
        [git_bin, "-C", str(path), "status", "--porcelain"],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout


def _run_gate_in_subprocess(env_overrides, node_ids=None):
    """Run `[sys.executable, "-m", "pytest", <target(s)>, "-q", "-rs"]`
    with cwd=_REPO_ROOT and os.environ merged with env_overrides, capturing
    text output, and return the CompletedProcess.

    A child process is MANDATORY here: _SCAN_NEW/_SCAN_PRECHANGE bind at
    IMPORT time, and monkeypatch.setenv cannot reach an already-imported
    module-level value (S6). `-rs` is passed so a skipped outcome (rather
    than a failure) would be visible in the captured output.

    `node_ids`, if given, may be a single node-id string or an iterable of
    them -- each becomes its own `<module_path>::<node_id>` target
    argument, so pytest scopes the run to exactly those tests (Plant B
    scopes to two: the partition leg and the parse-lengths leg).

    Guards against infinite recursion by refusing to run (a plain assert,
    never a skip) when FIRESTARTER_144_GATE_CHILD is already set in the
    CURRENT process, and sets that variable in the child's environment.
    """
    assert os.environ.get("FIRESTARTER_144_GATE_CHILD") is None, (
        "refusing to spawn a nested gate subprocess -- "
        "FIRESTARTER_144_GATE_CHILD is already set in this process; this "
        "would recurse indefinitely if it were allowed to proceed."
    )
    module_path = str(_HERE / "test_trace_segment_exhaustiveness_v131.py")
    if node_ids is None:
        targets = [module_path]
    elif isinstance(node_ids, str):
        targets = [f"{module_path}::{node_ids}"]
    else:
        targets = [f"{module_path}::{node_id}" for node_id in node_ids]

    env = dict(os.environ)
    env.update(env_overrides)
    env["FIRESTARTER_144_GATE_CHILD"] = "1"
    return subprocess.run(
        [sys.executable, "-m", "pytest", *targets, "-q", "-rs"],
        cwd=str(_REPO_ROOT),
        env=env,
        capture_output=True,
        text=True,
    )


def test_planted_unclassifiable_entry_is_located(tmp_path):
    """Coverage 10 -- D-18 Plant A. Copy the real NEW fixture's text
    verbatim, mutate exactly ONE entry -- PROTO_07's OUTPUT_ENABLE-assert
    toggle at its own positional index 34 -- to an unclassifiable shape by
    replacing its pin with 0x40 (none of the five known pins), assert the
    mutated text differs from the real text (a silently-unmatched
    replacement would be a vacuous plant), write it under tmp_path (never
    the real tree), and run test_every_entry_falls_in_exactly_one_segment
    against the mutated copy in a CHILD PROCESS via
    FIRESTARTER_TRACE_SEGMENT_SCAN_NEW.

    The RED transcript must name the array, the positional index, and the
    (kind,pin,value,us) tuple containing 0x40 -- a message that only
    reports a count is a defect; an unattributed entry must be LOCATABLE.

    S2 ceremony: both real fixtures are git-hash-object'd before and after,
    asserted unchanged, and the whole firmware repo's porcelain is asserted
    clean at the end."""
    before_new = _git_hash_object(_REAL_NEW_PATH)
    before_prechange = _git_hash_object(_REAL_PRECHANGE_PATH)
    real_text = _REAL_NEW_PATH.read_text()

    # Re-pointed by debug session w27c512-program-fail-byte0: the restored
    # program-voltage route assert shifted every PROTO_07 index at or after
    # the first pulse, so this plant's old anchor (positional comment 21)
    # no longer names an OE strobe. Same ENTRY as before -- the first
    # {2, 0x04, 0x01} (OE high, entering a program pulse) in PROTO_07 --
    # then at positional comment 26.
    #
    # RE-POINTED AGAIN by debug session w27c512-write-slow-3x, for the same
    # reason one generation on: the pass-batched program loop moved every
    # PROTO_07 index again, and the SAME entry -- still the first
    # {2, 0x04, 0x01} in PROTO_07 -- now sits at positional comment 34. Only
    # the locator moved; the plant is the identical mutation of the identical
    # entry to the identical unclassifiable pin (0x40), so what this test
    # PROVES is unchanged. The re-pointing was derived by parsing the new
    # fixture for that tuple's first occurrence, not by hand-counting, and the
    # plant was re-observed RED before being accepted (a plant that no longer
    # fails proves nothing).
    mutate_target = "{2, 0x04, 0x01, 0UL}, /* 34 */"
    replacement = "{2, 0x40, 0x01, 0UL}, /* 34 */"
    assert real_text.count(mutate_target) >= 1, (
        f"plant target {mutate_target!r} not found in the real NEW fixture "
        "-- the fixture may have changed since this plant was authored."
    )
    mutated_text = real_text.replace(mutate_target, replacement, 1)
    assert mutated_text != real_text, (
        "planted mutation did not actually change the text -- the "
        f"replacement target {mutate_target!r} was not found (the fixture's "
        "wording may have changed) -- a silently-unmatched replacement is a "
        "vacuous plant."
    )

    scratch_path = tmp_path / "planted_unclassifiable_eprom_v131_expected.h"
    scratch_path.write_text(mutated_text)

    result = _run_gate_in_subprocess(
        {"FIRESTARTER_TRACE_SEGMENT_SCAN_NEW": str(scratch_path)},
        node_ids="test_every_entry_falls_in_exactly_one_segment",
    )
    output = result.stdout + result.stderr

    assert result.returncode != 0, (
        "expected the partition leg to FAIL against a scan target with an "
        f"unclassifiable entry, got returncode=0.\nOutput:\n{output}"
    )
    assert "EPROM_V131_TRACE_PROTO_07" in output, (
        "expected the RED output to name the array "
        f"EPROM_V131_TRACE_PROTO_07.\nOutput:\n{output}"
    )
    # 26 -> 34 with the plant's own locator (same entry, moved by the
    # pass-batched golden). This literal must track mutate_target's index or
    # the leg silently stops proving the index is LOCATABLE.
    assert "index 34" in output, (
        f"expected the RED output to name the positional index 34.\n"
        f"Output:\n{output}"
    )
    assert "0x40" in output, (
        "expected the RED output to show the mutated pin value 0x40 as "
        f"part of the located entry's (kind,pin,value,us) tuple.\n"
        f"Output:\n{output}"
    )
    assert "0x01" in output, (
        "expected the RED output to show the entry's value field (0x01) "
        f"as part of the located tuple.\nOutput:\n{output}"
    )

    after_new = _git_hash_object(_REAL_NEW_PATH)
    after_prechange = _git_hash_object(_REAL_PRECHANGE_PATH)
    assert after_new == before_new, (
        "the planted mutation touched the REAL new fixture -- it must only "
        "ever be written under tmp_path, never the real tree."
    )
    assert after_prechange == before_prechange, (
        "the planted mutation touched the REAL pre-change fixture -- it "
        "must only ever be written under tmp_path, never the real tree."
    )
    assert _git_porcelain(_REPO_ROOT) == "", (
        "the firmware repo's working tree is no longer clean after the "
        "planted-unclassifiable-entry test."
    )


def test_planted_delete_and_duplicate_defeats_a_count_only_check(tmp_path):
    """Coverage 11 -- D-18 Plant B. Copy the real NEW fixture's text
    verbatim, DELETE one entry (PROTO_07's payload data write at positional
    index 39) and, in the SAME edit, insert a DUPLICATE of a different
    entry (PROTO_07's index 10, a CE-low strobe) into that vacated slot --
    so PROTO_07's array length is UNCHANGED (131 stays 131: that equality is
    what makes this plant meaningful, since it proves a COUNT-ONLY check
    would have passed it). Assert the mutated text differs from the real
    text, write it under tmp_path, and run BOTH the partition leg and the
    parse-lengths leg against the mutated copy in a CHILD PROCESS.

    The overall run must FAIL, and the failure must be attributable to the
    set-equality/disjointness assertion specifically -- NOT to a length
    mismatch (the parse-lengths leg must itself PASS, proving the length
    really is unchanged).

    S2 ceremony: both real fixtures are git-hash-object'd before and after,
    asserted unchanged, and the whole firmware repo's porcelain is asserted
    clean at the end."""
    before_new = _git_hash_object(_REAL_NEW_PATH)
    before_prechange = _git_hash_object(_REAL_PRECHANGE_PATH)
    real_text = _REAL_NEW_PATH.read_text()

    # Re-pointed by debug session w27c512-program-fail-byte0, same reason
    # and same entry as Plant A above: the first PROTO_07 payload write of
    # 0x55 moved from positional comment 22 to 27. The duplicate SOURCE
    # (positional comment 10, the first verify read's /CE fall) sits above
    # the first pulse and did NOT move.
    #
    # RE-POINTED AGAIN by debug session w27c512-write-slow-3x: the
    # pass-batched loop moved that same payload write from 27 to 39. The
    # duplicate SOURCE is STILL positional comment 10 -- verified against the
    # new fixture, not assumed: the pass-batched loop's scan pass reads the
    # block before any pulse, so everything above the first pulse kept its
    # index. Only the delete locator moved; the mutation's SHAPE (delete one
    # entry, insert a duplicate of another into the vacated slot, leaving the
    # array length unchanged) is identical, so what this test proves -- that a
    # COUNT-ONLY check would have passed it -- is unchanged, and it was
    # re-observed RED before being accepted.
    delete_target = "{1, 0x00, 0x55, 0UL}, /* 39 */"
    duplicate_source = "{2, 0x20, 0x00, 0UL}, /* 10 */"
    assert real_text.count(delete_target) >= 1, (
        f"plant delete-target {delete_target!r} not found in the real NEW "
        "fixture -- the fixture may have changed since this plant was "
        "authored."
    )
    assert real_text.count(duplicate_source) >= 1, (
        f"plant duplicate-source {duplicate_source!r} not found in the "
        "real NEW fixture -- the fixture may have changed since this plant "
        "was authored."
    )
    replacement = "{2, 0x20, 0x00, 0UL}, /* 39 (planted duplicate of index 10) */"
    mutated_text = real_text.replace(delete_target, replacement, 1)
    assert mutated_text != real_text, (
        "planted delete+duplicate did not actually change the text -- the "
        f"delete-target {delete_target!r} was not found (the fixture's "
        "wording may have changed) -- a silently-unmatched replacement is a "
        "vacuous plant."
    )

    real_arrays = dict(_parse_arrays_with_fields(_REAL_NEW_PATH))
    real_proto07_len = len(real_arrays["EPROM_V131_TRACE_PROTO_07"])

    scratch_path = tmp_path / "planted_delete_dup_eprom_v131_expected.h"
    scratch_path.write_text(mutated_text)
    mutated_arrays = dict(_parse_arrays_with_fields(scratch_path))
    mutated_proto07_len = len(mutated_arrays["EPROM_V131_TRACE_PROTO_07"])

    assert mutated_proto07_len == real_proto07_len == 131, (
        "expected the mutated PROTO_07 array's entry count to equal the "
        f"real count -- real={real_proto07_len} mutated={mutated_proto07_len} "
        "(both expected to be 91). This equality is what makes the plant "
        "meaningful: it proves a COUNT-ONLY check would have PASSED this "
        "exact mutation."
    )

    result = _run_gate_in_subprocess(
        {"FIRESTARTER_TRACE_SEGMENT_SCAN_NEW": str(scratch_path)},
        node_ids=(
            "test_every_entry_falls_in_exactly_one_segment",
            "test_new_arrays_parse_to_the_captured_lengths",
        ),
    )
    output = result.stdout + result.stderr

    assert result.returncode != 0, (
        "expected the partition leg to FAIL against a length-preserving "
        f"delete+duplicate mutation, got returncode=0.\nOutput:\n{output}"
    )
    assert "EPROM_V131_TRACE_PROTO_07" in output, (
        f"expected the RED output to name the array.\nOutput:\n{output}"
    )
    assert "does NOT equal set(range(len(entries)))" in output, (
        "expected the failure to be attributable to the set-equality "
        f"assertion specifically.\nOutput:\n{output}"
    )
    # 27 -> 39 with the plant's own delete_target, same reason as Plant A.
    assert "index 39" in output, (
        f"expected the RED output to name index 39 as uncovered by any "
        f"segment.\nOutput:\n{output}"
    )
    assert "count-only check" in output, (
        "expected the RED output to state that a count-only check would "
        f"NOT have caught this.\nOutput:\n{output}"
    )
    assert "1 passed" in output, (
        "expected the parse-lengths leg to PASS (proving the array length "
        f"is genuinely unchanged at 91) -- not a length mismatch.\n"
        f"Output:\n{output}"
    )
    assert "1 failed" in output, (
        "expected exactly the partition leg to FAIL.\n"
        f"Output:\n{output}"
    )

    after_new = _git_hash_object(_REAL_NEW_PATH)
    after_prechange = _git_hash_object(_REAL_PRECHANGE_PATH)
    assert after_new == before_new, (
        "the planted mutation touched the REAL new fixture -- it must only "
        "ever be written under tmp_path, never the real tree."
    )
    assert after_prechange == before_prechange, (
        "the planted mutation touched the REAL pre-change fixture -- it "
        "must only ever be written under tmp_path, never the real tree."
    )
    assert _git_porcelain(_REPO_ROOT) == "", (
        "the firmware repo's working tree is no longer clean after the "
        "planted-delete-and-duplicate test."
    )



def _protocol_suffix(array_name):
    prefix = "EPROM_V131_TRACE_PROTO_"
    return array_name[len(prefix):] if array_name.startswith(prefix) else array_name


def _print_segment_table():
    prechange_arrays = dict(_parse_arrays_with_fields(_SCAN_PRECHANGE))
    new_arrays = dict(_parse_arrays_with_fields(_SCAN_NEW))

    for array_name in prechange_arrays:
        suffix = _protocol_suffix(array_name)
        pre_entries = prechange_arrays[array_name]
        new_entries = new_arrays.get(array_name, [])
        pre_segs = _segment_indices(pre_entries, array_name=f"{array_name} (prechange)")
        new_segs = (
            _segment_indices(new_entries, array_name=f"{array_name} (new)")
            if new_entries
            else {s: set() for s in _SEGMENTS}
        )

        print(f"\n===== Protocol 0x{suffix} ({array_name}) =====")
        print(f"{'segment':<16}{'pre-change':>12}{'new':>8}{'delta':>8}   attribution (first citation)")
        for seg_name in _SEGMENTS:
            pre_count = len(pre_segs[seg_name])
            new_count = len(new_segs[seg_name])
            delta = new_count - pre_count
            citation = _SEGMENT_ATTRIBUTION[seg_name][0]
            print(
                f"{seg_name:<16}{pre_count:>12}{new_count:>8}{delta:>+8}   {citation[:88]}"
            )
        pre_total = sum(len(v) for v in pre_segs.values())
        new_total = sum(len(v) for v in new_segs.values())
        print(f"{'TOTAL':<16}{pre_total:>12}{new_total:>8}{new_total - pre_total:>+8}")


if __name__ == "__main__":
    _print_segment_table()
