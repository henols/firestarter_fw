"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 204 Plan 02 -- FWCMD-04's own source-contract gate, the eighth member
of the house source-contract family. Phase 204 removes the standalone
verify and blank-check commands' wire ordinals from the firmware; this
module proves that removing a command surface did not remove the
in-algorithm verification that survives underneath it -- specifically, that
the shared final-pass verify function is still called from inside the
plus-final verify arm of the per-byte program loop, for the two 27C
protocols (0x07, 0x08) that ship that verify mode.

Phase 204 Plan 03 extended this module with the FWCMD-01 and FWCMD-03
absence legs (Coverage 7-10 below): proving neither retired ordinal
reappears in the dispatch switch, in the admission predicate, or in any of
the five protocol configure handlers, and that the firmware header records
a reserved-ordinal reason at both gaps. These legs' RED was observed
against the pre-deletion tree before plan 03's sweep landed -- see that
plan's SUMMARY for the captured transcript.

Phase 205 Plan 03 (task 1) extends this module again, with the FWBLANK-01
and FWBLANK-02 absence legs (Coverage 11-12 below), and MIGRATES Coverage 13
from test_blank_check_region_source_contract.py (Phase 201 Plan 05), which
retires in this same commit: that module's own Coverage 6 was the only
mechanical fence around the operation-end resolution point's definition, so
it moves here rather than being lost with the module that held it. Coverage
11-12's RED was observed against the pre-sweep tree before this plan's
source edits landed -- see 205-03-SUMMARY.md for the captured transcript.

Requirements: FWCMD-01, FWCMD-03, FWCMD-04, FWBLANK-01, FWBLANK-02

Defect class this closes: a call-site-EXISTENCE property. "Is this call
still made, from inside this specific arm, after ordinal 6 left the file?"
cannot be proven by running the firmware -- a native or bench run only ever
exercises the cases it was written for, and this property is about every
FUTURE edit, not one measured run. A source scan proves it mechanically, on
every commit, the same way the seven existing source-contract gates in this
tree prove their own call-site-existence and call-site-absence properties.
A behavioural claim -- "does this call site actually RAISE the right error
id on failure?" -- is a different kind of property and is NOT this module's
job; that is the amended FWCMD-05's job, proven by the native suite this
same plan adds under test/native/avr/test_verify_error_ids/.

Coverage:
  1. test_the_final_verify_pass_is_still_called_for_plus_final -- the
     shared final-pass verify call is brace-matched INSIDE the plus-final
     verify arm's body, never merely present somewhere in the file --
     moving the call out of the arm while leaving it in the enclosing
     function fails here exactly as loudly as deleting it. Containment is
     proven by finding the arm's opening brace and walking to its matching
     close, never by line proximity.
  2. test_the_plus_final_mode_is_still_shipped_by_the_two_protocol_rows --
     the positive counterpart to Coverage 1: the parameter table still
     ships the plus-final verify mode for the two protocol rows that use
     it (keyed positionally against the table's own lookup-key array, not
     against its comments), so a deleted or truncated parameter file
     cannot satisfy Coverage 1 vacuously.
  3. test_the_shared_verify_is_still_defined -- the shared final-pass
     verify function is still defined, exactly once, in the memory source
     file -- so a stub or forward declaration left behind after the real
     definition were deleted cannot satisfy Coverage 1 vacuously either.
  4. test_scan_targets_are_non_vacuous -- every DEFAULT scan target
     (recomputed fresh from the repository root, never the environment
     seam) exists, is non-empty, resolves inside this repository, and its
     comment-stripped text is non-empty. A missing or empty scan target
     must FAIL, never silently pass as if nothing needed checking.
  5. test_this_module_cannot_be_silently_skipped -- this module's own
     source contains no skip-bypass call, no skip-marker decorator and no
     dependency-skip call anywhere.
  6. test_own_needles_do_not_appear_verbatim_in_this_module -- none of the
     six concatenation-built needles (the shared verify call's own
     identifier, the plus-final enumerator's identifier as used in the
     containment arm's regex, the same enumerator's identifier as used in
     the parameter-table leg's regex, the two retired ordinals' own
     identifiers, and the reserved-ordinal marker phrase) appears verbatim
     anywhere in this module's own source, so this gate cannot match itself.
  7. test_neither_retired_ordinal_appears_in_the_dispatch_switch -- neither
     retired command's identifier appears anywhere inside the command
     dispatch switch's brace-matched body in src/firestarter.cpp.
  8. test_neither_retired_ordinal_appears_in_the_admission_predicate --
     neither retired command's identifier appears anywhere inside
     is_memory_cmd's brace-matched body in include/firestarter.h.
  9. test_no_protocol_handler_configures_a_retired_ordinal -- neither
     retired command's identifier appears ANYWHERE (a whole-file scan, not
     brace-matched) in any of the five protocol configure handlers.
  10. test_both_reserved_ordinal_gaps_carry_a_recorded_reason -- the
      firmware header carries the reserved-ordinal marker phrase at least
      twice, once per retired ordinal's gap in the CMD ladder.
  11. test_no_write_init_body_performs_a_blank_check (FWBLANK-01) -- none
      of the three write-init bodies (eprom.cpp, flash_intel.cpp,
      flash_nor_unlock.cpp) references the whole-device or the
      region-scoped blank-check function anymore, scanned over RAW text so
      a stray reference left in a comment fails exactly as loudly as one
      left in a call expression.
  12. test_the_erase_arm_assigns_no_operation_end_in_eprom_cpp
      (FWBLANK-02) -- configure_eprom's CMD_ERASE arm assigns no
      handle->firestarter_operation_end at all, contained to the arm via a
      brace-matched function lookup.
  13. test_operation_end_is_defined_exactly_once_and_reads_both_members --
      MIGRATED from test_blank_check_region_source_contract.py's own
      Coverage 6 (that module retires in this commit): the operation-end
      resolution point (plan 201-04's D-06 anchor) is defined exactly once
      and its body reads both the region-end member and the device-size
      member, so the fail-closed clamp cannot be silently dropped while the
      function stays defined and apparently intact.

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover this
one)
  - FIRESTARTER_VERIFY_SURVIVAL_SCAN_EPROM_SOURCE -- overrides the scanned
    src/proms/eprom.cpp path ONLY. The parameter-table target
    (eprom_params.cpp) and the memory-source target (memory.cpp) have NO
    override, matching the house convention that a single-file planted
    violation is proven the safer way -- directly in the real file,
    captured, then restored -- rather than by adding a second seam. Binds
    at IMPORT time (the module-level environment-variable read below), so a
    planted-violation run must set it in a CHILD PROCESS environment before
    this module is imported, never via a post-import monkeypatch. Coverage
    4, 5 and 6 deliberately never read it: 4 recomputes every default
    target directly from the repository root; 5 and 6 read only this
    module's own source. This phase's own non-vacuity proof plants directly
    into the real eprom.cpp file instead of using this seam, captures the
    run, and restores the file -- test_write_path_source_contract_v131.py's
    and test_blank_check_region_source_contract.py's own stated precedent
    for why a single-file target is proven that way.

This module is a standalone pytest module: it adds no shared pytest
configuration or fixture-registration file, and it imports nothing beyond
the Python standard library. It never imports, parametrizes against, or
edits any pre-existing gate module; the scanning logic below is its own
independent re-derivation of this plan's own gate specification, following
test_write_path_source_contract_v131.py's and
test_blank_check_region_source_contract.py's shape element for element.
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent

_EPROM_REL = "src/proms/eprom.cpp"
_PARAMS_REL = "src/proms/eprom_params.cpp"
_MEMORY_REL = "src/proms/memory.cpp"
_DISPATCH_REL = "src/firestarter.cpp"
_HEADER_REL = "include/firestarter.h"
_FLASH_NOR_UNLOCK_REL = "src/proms/flash_nor_unlock.cpp"
_FLASH_INTEL_REL = "src/proms/flash_intel.cpp"
_FLASH_5V_PAGE_REL = "src/proms/flash_5v_page.cpp"
_EEPROM_28C_REL = "src/proms/eeprom_28c.cpp"
_MEMORY_UTILS_REL = "include/memory_utils.h"

# Environment seam -- binds at IMPORT time. See the module docstring's
# "Environment seams" section above. Only this one target is overridable.
_SCAN_EPROM = Path(
    os.environ.get(
        "FIRESTARTER_VERIFY_SURVIVAL_SCAN_EPROM_SOURCE",
        str(_REPO_ROOT / _EPROM_REL),
    )
)
# The remaining targets have no override -- see the docstring section
# above for why. A stray environment value pointed at the eprom.cpp seam
# cannot make Coverage 2, 3, 9 or 10 vacuous.
_SCAN_PARAMS = _REPO_ROOT / _PARAMS_REL
_SCAN_MEMORY = _REPO_ROOT / _MEMORY_REL
_SCAN_DISPATCH = _REPO_ROOT / _DISPATCH_REL
_SCAN_HEADER = _REPO_ROOT / _HEADER_REL
_SCAN_FLASH_NOR_UNLOCK = _REPO_ROOT / _FLASH_NOR_UNLOCK_REL
_SCAN_FLASH_INTEL = _REPO_ROOT / _FLASH_INTEL_REL
_SCAN_FLASH_5V_PAGE = _REPO_ROOT / _FLASH_5V_PAGE_REL
_SCAN_EEPROM_28C = _REPO_ROOT / _EEPROM_28C_REL
_SCAN_MEMORY_UTILS = _REPO_ROOT / _MEMORY_UTILS_REL

# The five protocol configure handlers Coverage 9 scans in full.
_PROTOCOL_RELS = (
    (_EPROM_REL, _SCAN_EPROM),
    (_FLASH_NOR_UNLOCK_REL, _SCAN_FLASH_NOR_UNLOCK),
    (_FLASH_INTEL_REL, _SCAN_FLASH_INTEL),
    (_FLASH_5V_PAGE_REL, _SCAN_FLASH_5V_PAGE),
    (_EEPROM_28C_REL, _SCAN_EEPROM_28C),
)

# Concatenation-built needles. Coverage 6 asserts none of these appears
# verbatim anywhere in this module's own source -- see the module docstring
# and the plan's own warning: a gate that quotes its own forbidden or
# required tokens verbatim can end up matching itself.
_NEEDLE_CALL = "memory_verify_exec" + "ute"
_NEEDLE_MODE = "VERIFY_PER_PULSE_PLUS" + "_FINAL"
_NEEDLE_DEFINITION = "VERIFY_PER_PULSE_PLUS_FI" + "NAL"
_NEEDLE_RETIRED_VERIFY = "CMD_" + "VERIFY"
_NEEDLE_RETIRED_BLANK = "CMD_BLANK" + "_CHECK"
_NEEDLE_RESERVED_MARKER = "retired in " + "3.1.0"

# FWBLANK-01/02/03 (Phase 205 Plan 03) -- concatenation-built needles for the
# blank-check absence legs and for the operation-end survivor fence migrated
# from test_blank_check_region_source_contract.py, which retires in the same
# commit. Split strictly inside each identifier's own name, matching that
# module's own convention, so neither fragment nor the full needle value
# ever appears as a contiguous run of characters anywhere else in this file.
_NEEDLE_BLANK_CHECK_FN = "mem_util_blank_c" + "heck"
_NEEDLE_BLANK_CHECK_REGION_FN = "mem_util_blank_che" + "ck_region"
_NEEDLE_BLANK_CHECK_CURSOR = "blank_check_sa" + "ved_address"
_NEEDLE_BLANK_CHECK_CHUNK = "BLANK_CHECK_CHU" + "NK_SIZE"
_NEEDLE_UINT32_TO_BYTES = "uint32_to_by" + "tes"

_ALL_SELF_CHECK_NEEDLES = (
    ("the shared final-pass verify call's identifier", _NEEDLE_CALL),
    ("the plus-final enumerator's identifier (containment arm)", _NEEDLE_MODE),
    ("the plus-final enumerator's identifier (parameter-table leg)", _NEEDLE_DEFINITION),
    ("the verify command's retired identifier", _NEEDLE_RETIRED_VERIFY),
    ("the blank-check command's retired identifier", _NEEDLE_RETIRED_BLANK),
    ("the reserved-ordinal marker phrase", _NEEDLE_RESERVED_MARKER),
    ("the whole-device blank-check function's identifier", _NEEDLE_BLANK_CHECK_FN),
    ("the region-scoped blank-check function's identifier", _NEEDLE_BLANK_CHECK_REGION_FN),
    ("the blank-check saved-address cursor's identifier", _NEEDLE_BLANK_CHECK_CURSOR),
    ("the blank-check chunk-size constant's identifier", _NEEDLE_BLANK_CHECK_CHUNK),
    ("the orphaned byte-packing helper's identifier", _NEEDLE_UINT32_TO_BYTES),
)

_ARM_RE = re.compile(r"if\s*\(\s*verify_mode\s*==\s*" + _NEEDLE_MODE + r"\s*\)\s*\{")
_CALL_RE = re.compile(r"\b" + _NEEDLE_CALL + r"\s*\(\s*handle\s*\)\s*;")

_MEMORY_VERIFY_DEF_RE = re.compile(
    r"\bvoid\s+" + _NEEDLE_CALL + r"\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)

_KEYS_RE = re.compile(r"EPROM_PARAM_KEYS\s*\[\s*\]\s*PROGMEM\s*=\s*\{([^}]*)\}")
_PARAMS_BODY_RE = re.compile(r"EPROM_PARAMS\s*\[\s*\]\s*PROGMEM\s*=\s*\{(.*?)\}\s*;", re.S)
_ROW_RE = re.compile(r"\{[^{}]*\}")
_DEFINITION_TOKEN_RE = re.compile(r"\b" + _NEEDLE_DEFINITION + r"\b")

_DISPATCH_SWITCH_RE = re.compile(r"switch\s*\(\s*handle\.cmd\s*\)\s*\{")
_ADMISSION_FUNC_RE = re.compile(r"\bis_memory_cmd\s*\(\s*uint8_t\s+cmd\s*\)\s*\{")

# FWBLANK-01/02 (Phase 205 Plan 03) -- the CMD_ERASE arm's own containment
# regex, scoped to configure_eprom so this leg proves absence inside the
# right arm rather than merely somewhere in the file.
_CONFIGURE_EPROM_DEF_RE = re.compile(
    r"\bvoid\s+configure_eprom\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)
_ERASE_ARM_RE = re.compile(r"case\s+CMD_ERASE\s*:(.*?)break\s*;", re.S)

# Migrated from test_blank_check_region_source_contract.py (Phase 201 Plan
# 05), which retires in this commit -- plan 201-04's D-06 anchor. This is
# the only mechanical fence around mem_util_operation_end's definition, so
# it moves here rather than being dropped with the module that held it.
_OP_END_DEF_RE = re.compile(
    r"\buint32_t\s+mem_util_operation_end\s*\(\s*const\s+firestarter_handle_t\s*\*"
    r"\s*handle\s*\)\s*\{"
)
_OP_END_READS_REGION_END_RE = re.compile(r"handle\s*->\s*region_end\b")
_OP_END_READS_MEM_SIZE_RE = re.compile(r"handle\s*->\s*mem_size\b")


def _strip_comments(text):
    """Strip `//` line comments and `/* ... */` block comments, replacing
    each stripped span with whitespace of the SAME SHAPE (a newline stays a
    newline, everything else becomes a single space) so every line number
    in the result matches the original file exactly -- the same technique
    test_write_path_source_contract_v131.py and
    test_blank_check_region_source_contract.py both use. Neither scan
    target contains a string or character literal anywhere outside a
    comment or an #include directive (confirmed by inspection at authoring
    time), so literal-stripping is not needed here."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            out.append("  ")
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append("  ")
                i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _find_matching_brace(text, open_idx):
    """Brace-matched (not flat-regex) scan for the `}` closing the `{` at
    `open_idx` -- a containment test ("is this call inside that arm's
    body?") based on line proximity would be a guess. This is not."""
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def _function_body_span(stripped, def_re):
    """Return (open_brace_idx, close_brace_idx) for the first match of
    def_re (a pattern ending in the arm's or function's opening brace)
    against stripped, or None if def_re does not match."""
    m = def_re.search(stripped)
    if not m:
        return None
    open_idx = m.end() - 1
    close_idx = _find_matching_brace(stripped, open_idx)
    return (open_idx, close_idx)


def _read_stripped(path):
    return _strip_comments(path.read_text())


# Tests


def test_the_final_verify_pass_is_still_called_for_plus_final():
    """Coverage 1 -- the shared final-pass verify call is brace-matched
    INSIDE the plus-final verify arm's body. FWCMD-04's whole point is that
    removing the command surface did NOT remove this call; a leg based on
    line proximity would be a guess, so containment is proven by finding
    the arm's own opening brace and walking to its matching close."""
    stripped = _read_stripped(_SCAN_EPROM)
    span = _function_body_span(stripped, _ARM_RE)
    assert span is not None, (
        f"the plus-final verify arm is gone from {_EPROM_REL} -- FWCMD-04's "
        "shared final-pass verify call can no longer be proven contained "
        "anywhere."
    )
    body = stripped[span[0] : span[1] + 1]
    assert _CALL_RE.search(body), (
        "the plus-final verify arm no longer calls the shared final-pass "
        "verify function -- FWCMD-04's whole point is that removing the "
        "command surface did NOT remove this call.\n"
        f"Got arm body:\n{body}"
    )


def test_the_plus_final_mode_is_still_shipped_by_the_two_protocol_rows():
    """Coverage 2 -- the positive counterpart to Coverage 1: without this
    leg, deleting or truncating the parameter table would satisfy Coverage
    1 vacuously (nothing left to key the arm on at all). This leg keys rows
    to protocols POSITIONALLY against the table's own lookup-key array --
    never against a row's own comment, which comment-stripping removes
    anyway -- so the right two rows are pinned rather than merely a count
    of two rows somewhere in the file."""
    stripped = _read_stripped(_SCAN_PARAMS)

    keys_match = _KEYS_RE.search(stripped)
    assert keys_match is not None, (
        f"could not locate the lookup-key array in {_PARAMS_REL} -- the "
        "parameter table's protocol keys must be readable for this leg to "
        "key rows positionally."
    )
    keys = [k.strip() for k in keys_match.group(1).split(",") if k.strip()]
    assert keys, f"the lookup-key array in {_PARAMS_REL} is empty"

    body_match = _PARAMS_BODY_RE.search(stripped)
    assert body_match is not None, (
        f"could not locate the parameter table's row body in {_PARAMS_REL}."
    )
    rows = _ROW_RE.findall(body_match.group(1))
    assert len(rows) == len(keys), (
        f"expected {len(keys)} parameter rows (one per lookup key) in "
        f"{_PARAMS_REL}, found {len(rows)} -- rows and keys must stay "
        "positionally parallel for this leg to key the right two.\n"
        f"Keys: {keys}\nRows:\n" + "\n".join(rows)
    )

    plus_final_protocols = {"0x07", "0x08"}
    missing = []
    for key, row in zip(keys, rows):
        if key in plus_final_protocols:
            if not _DEFINITION_TOKEN_RE.search(row):
                missing.append(f"{key}: {row}")
    assert missing == [], (
        "expected the plus-final verify mode still present in the "
        f"parameter row for each of {sorted(plus_final_protocols)} in "
        f"{_PARAMS_REL}, but it is missing from:\n" + "\n".join(missing)
    )


def test_the_shared_verify_is_still_defined():
    """Coverage 3 -- the shared final-pass verify function is still
    defined, exactly once, in the memory source file. Without this leg, a
    stub or a forward declaration left behind after the real definition
    were deleted could satisfy Coverage 1 vacuously (the call site would
    still compile against a declaration alone)."""
    stripped = _read_stripped(_SCAN_MEMORY)
    matches = _MEMORY_VERIFY_DEF_RE.findall(stripped)
    assert len(matches) == 1, (
        "expected exactly 1 definition of the shared final-pass verify "
        f"function in {_MEMORY_REL}, found {len(matches)}.\n"
        f"Got (comment-stripped {_MEMORY_REL}):\n{stripped}"
    )


def test_neither_retired_ordinal_appears_in_the_dispatch_switch():
    """Coverage 7 (FWCMD-01) -- neither retired command's identifier appears
    anywhere inside the command dispatch switch's brace-matched body in
    src/firestarter.cpp. Containment is proven by finding the switch's own
    opening brace and walking to its matching close, never by line
    proximity -- the whole-file scan for the five protocol handlers is a
    separate leg below (Coverage 9), because a dispatch switch and a
    configure handler are different failure classes: one selects a
    callback, the other configures hardware."""
    stripped = _read_stripped(_SCAN_DISPATCH)
    span = _function_body_span(stripped, _DISPATCH_SWITCH_RE)
    assert span is not None, (
        f"the command dispatch switch is gone from {_DISPATCH_REL} -- this "
        "leg has nothing left to scan."
    )
    body = stripped[span[0] : span[1] + 1]
    for label, needle in (
        ("the verify command's retired identifier", _NEEDLE_RETIRED_VERIFY),
        ("the blank-check command's retired identifier", _NEEDLE_RETIRED_BLANK),
    ):
        assert needle not in body, (
            f"{label} still appears inside the dispatch switch's body in "
            f"{_DISPATCH_REL} -- both retired commands' wire ordinals must "
            "route through the switch's default: arm only, never a named "
            f"case.\nGot switch body:\n{body}"
        )


def test_neither_retired_ordinal_appears_in_the_admission_predicate():
    """Coverage 8 (FWCMD-01) -- neither retired command's identifier appears
    anywhere inside is_memory_cmd's brace-matched body in
    include/firestarter.h, the access-control gate that decides which
    commands may reach configure_memory() -- and so the 12V VPP boost
    regulator -- at all."""
    stripped = _read_stripped(_SCAN_HEADER)
    span = _function_body_span(stripped, _ADMISSION_FUNC_RE)
    assert span is not None, (
        f"the admission predicate is gone from {_HEADER_REL} -- this leg "
        "has nothing left to scan."
    )
    body = stripped[span[0] : span[1] + 1]
    for label, needle in (
        ("the verify command's retired identifier", _NEEDLE_RETIRED_VERIFY),
        ("the blank-check command's retired identifier", _NEEDLE_RETIRED_BLANK),
    ):
        assert needle not in body, (
            f"{label} still appears inside the admission predicate's body "
            f"in {_HEADER_REL} -- a retired ordinal must never be admitted "
            f"to configure_memory() again.\nGot predicate body:\n{body}"
        )


def test_no_protocol_handler_configures_a_retired_ordinal():
    """Coverage 9 (FWCMD-01) -- neither retired command's identifier appears
    ANYWHERE (not merely in a case label) in any of the five protocol
    configure handlers. A whole-file scan, not a brace-matched one: this
    leg exists to catch a stray reference left in a comment exactly as
    loudly as one left in a case label."""
    hits = []
    for rel, path in _PROTOCOL_RELS:
        stripped = _read_stripped(path)
        for label, needle in (
            ("the verify command's retired identifier", _NEEDLE_RETIRED_VERIFY),
            ("the blank-check command's retired identifier", _NEEDLE_RETIRED_BLANK),
        ):
            if needle in stripped:
                hits.append(f"{rel}: {label}")
    assert hits == [], (
        "found a retired command identifier surviving in a protocol "
        "configure handler -- both wire ordinals must be gone from every "
        "one of the five handlers, not merely from their dispatch "
        "switches.\nGot:\n" + "\n".join(hits)
    )


def test_both_reserved_ordinal_gaps_carry_a_recorded_reason():
    """Coverage 10 (FWCMD-03) -- the firmware header carries a
    reserved-ordinal record at BOTH gaps in the CMD ladder, naming the
    release that retired the ordinal and stating the never-reuse reason --
    the place a future author scanning the ladder for a free slot will
    actually read. Counts occurrences of the shared marker phrase rather
    than parsing prose, so a rewritten sentence that keeps the marker still
    passes and a note deleted outright still fails."""
    raw = _SCAN_HEADER.read_text()
    count = raw.count(_NEEDLE_RESERVED_MARKER)
    assert count >= 2, (
        f"expected the reserved-ordinal marker to appear at least twice in "
        f"{_HEADER_REL} (once per retired ordinal), found {count} -- a "
        "reserved-ordinal record is missing at one of the two gaps."
    )


def test_no_write_init_body_performs_a_blank_check():
    """Coverage 11 (FWBLANK-01) -- none of the three write-init bodies
    (eprom.cpp's internal write-init helper, flash_intel.cpp's write-init,
    flash_nor_unlock.cpp's write-init) calls the whole-device or the
    region-scoped blank-check function anymore. The pre-write refusal moved
    to the host in Phase 203; this leg proves the firmware's own duplicate
    pre-flight is gone from all three write-init bodies. A whole-file scan
    over RAW (not comment-stripped) text, matching Coverage 9's own stated
    intent above -- these absence legs exist to catch a stray reference
    left in a comment exactly as loudly as one left in a call expression,
    so stripping comments first would defeat the point."""
    hits = []
    for rel, path in (
        (_EPROM_REL, _SCAN_EPROM),
        (_FLASH_INTEL_REL, _SCAN_FLASH_INTEL),
        (_FLASH_NOR_UNLOCK_REL, _SCAN_FLASH_NOR_UNLOCK),
    ):
        raw = path.read_text()
        for label, needle in (
            ("the whole-device blank-check function", _NEEDLE_BLANK_CHECK_FN),
            ("the region-scoped blank-check function", _NEEDLE_BLANK_CHECK_REGION_FN),
        ):
            if needle in raw:
                hits.append(f"{rel}: {label}")
    assert hits == [], (
        "found a write-init body still referencing a blank-check function "
        "-- FWBLANK-01 removes the firmware's pre-write blank-check "
        "pre-flight from every write-init body; the host now owns this "
        "refusal.\nGot:\n" + "\n".join(hits)
    )


def test_the_erase_arm_assigns_no_operation_end_in_eprom_cpp():
    """Coverage 12 (FWBLANK-02) -- configure_eprom's CMD_ERASE arm assigns
    no handle->firestarter_operation_end at all -- not a different one, not
    a conditional one -- so the UV handler's erase has no end-op and the
    host owns the post-erase verdict. Contained to the arm via a
    brace-matched function lookup followed by a case/break slice, never by
    line proximity."""
    stripped = _read_stripped(_SCAN_EPROM)
    func_span = _function_body_span(stripped, _CONFIGURE_EPROM_DEF_RE)
    assert func_span is not None, (
        f"configure_eprom is gone from {_EPROM_REL} -- this leg has "
        "nothing left to scan."
    )
    func_body = stripped[func_span[0] : func_span[1] + 1]
    arm_match = _ERASE_ARM_RE.search(func_body)
    assert arm_match is not None, (
        f"the CMD_ERASE arm is gone from configure_eprom in {_EPROM_REL} -- "
        "this leg has nothing left to scan."
    )
    arm_body = arm_match.group(1)
    assert "firestarter_operation_end" not in arm_body, (
        "configure_eprom's CMD_ERASE arm in "
        f"{_EPROM_REL} still assigns handle->firestarter_operation_end -- "
        "FWBLANK-02 removes the post-erase blank check; the arm must "
        f"assign no end-op at all.\nGot arm body:\n{arm_body}"
    )


def test_operation_end_is_defined_exactly_once_and_reads_both_members():
    """Coverage 13 -- MIGRATED from test_blank_check_region_source_contract.py
    (that module's own Coverage 6), which retires in this same commit.
    Plan 201-04's D-06 anchor: the operation-end resolution point (D-04's
    0=absent=whole-device fallback plus the fail-closed clamp) is defined
    exactly once and its body reads both the region-end member and the
    device-size member, so the fail-closed clamp cannot be silently
    dropped while the function stays defined and apparently intact. This
    is the only mechanical fence around mem_util_operation_end's
    definition, so it moves here rather than being lost with the module
    that held it."""
    stripped = _read_stripped(_SCAN_MEMORY)
    def_matches = list(_OP_END_DEF_RE.finditer(stripped))
    assert len(def_matches) == 1, (
        "expected exactly 1 definition of the operation-end resolution "
        f"point in {_MEMORY_REL}, found {len(def_matches)}.\n"
        f"Got (comment-stripped {_MEMORY_REL}):\n{stripped}"
    )
    body_span = _function_body_span(stripped, _OP_END_DEF_RE)
    assert body_span is not None and body_span[1] > body_span[0], (
        f"could not brace-match the operation-end resolution point's body in {_MEMORY_REL}."
    )
    body_text = stripped[body_span[0] : body_span[1] + 1]
    assert _OP_END_READS_REGION_END_RE.search(body_text), (
        "expected the operation-end resolution point's body to read "
        f"handle->region_end in {_MEMORY_REL}.\nGot body:\n{body_text}"
    )
    assert _OP_END_READS_MEM_SIZE_RE.search(body_text), (
        "expected the operation-end resolution point's body to read "
        f"handle->mem_size in {_MEMORY_REL}.\nGot body:\n{body_text}"
    )


def test_the_blank_check_machinery_is_absent():
    """Coverage 14 (FWBLANK-03) -- a five-symbol probe over memory.cpp and
    memory_utils.h: the whole-device and region-scoped blank-check
    functions, the blank-check saved-address cursor, the blank-check
    chunk-size constant, and the byte-packing helper orphaned by the sweep
    (its only two callers sat inside the deleted region form's
    RAW_DATA_PROGRESS branch) are gone from both files with no caller, no
    declaration and no doc comment left behind. A whole-file scan over RAW
    (not comment-stripped) text -- these absence legs exist to catch a
    stray reference left in a comment exactly as loudly as one left in a
    case label, so stripping comments first would defeat the point."""
    hits = []
    for rel, path in (
        (_MEMORY_REL, _SCAN_MEMORY),
        (_MEMORY_UTILS_REL, _SCAN_MEMORY_UTILS),
    ):
        raw = path.read_text()
        for label, needle in (
            ("the whole-device blank-check function", _NEEDLE_BLANK_CHECK_FN),
            ("the region-scoped blank-check function", _NEEDLE_BLANK_CHECK_REGION_FN),
            ("the blank-check saved-address cursor", _NEEDLE_BLANK_CHECK_CURSOR),
            ("the blank-check chunk-size constant", _NEEDLE_BLANK_CHECK_CHUNK),
            ("the orphaned byte-packing helper", _NEEDLE_UINT32_TO_BYTES),
        ):
            if needle in raw:
                hits.append(f"{rel}: {label}")
    assert hits == [], (
        "found blank-check machinery surviving in memory.cpp or "
        "memory_utils.h -- FWBLANK-03 deletes all five symbols with no "
        "caller, declaration or doc comment left anywhere.\n"
        "Got:\n" + "\n".join(hits)
    )


def test_scan_targets_are_non_vacuous():
    """Coverage 4 -- structural self-check, never reads the environment
    seam: every DEFAULT scan target (recomputed fresh from _REPO_ROOT --
    the check_permitted_claims.py _HERE-resolves-to-the-wrong-directory
    landmine, closed here by construction) exists, is non-empty, resolves
    inside this repository, and its comment-stripped text is non-empty. A
    missing or empty scan target must FAIL, never silently pass as if
    nothing needed checking. Extended in Phase 204 Plan 03 to cover the
    dispatch source, the header, and the four protocol handlers this
    module did not previously scan (eprom.cpp was already covered)."""
    default_targets = (
        (_EPROM_REL, _REPO_ROOT / _EPROM_REL),
        (_PARAMS_REL, _REPO_ROOT / _PARAMS_REL),
        (_MEMORY_REL, _REPO_ROOT / _MEMORY_REL),
        (_DISPATCH_REL, _REPO_ROOT / _DISPATCH_REL),
        (_HEADER_REL, _REPO_ROOT / _HEADER_REL),
        (_FLASH_NOR_UNLOCK_REL, _REPO_ROOT / _FLASH_NOR_UNLOCK_REL),
        (_FLASH_INTEL_REL, _REPO_ROOT / _FLASH_INTEL_REL),
        (_FLASH_5V_PAGE_REL, _REPO_ROOT / _FLASH_5V_PAGE_REL),
        (_EEPROM_28C_REL, _REPO_ROOT / _EEPROM_28C_REL),
        (_MEMORY_UTILS_REL, _REPO_ROOT / _MEMORY_UTILS_REL),
    )
    for label, p in default_targets:
        assert p.is_file(), (
            f"default {label} scan target {p} does not exist on disk -- a "
            "missing scan target must FAIL, never silently pass."
        )
        assert p.stat().st_size > 0, f"default {label} scan target {p} is empty"
        assert p.resolve().is_relative_to(_REPO_ROOT), (
            f"default {label} scan target {p} resolves outside _REPO_ROOT "
            f"({_REPO_ROOT}) -- a naive future copy of this module into "
            "another directory must fail loudly here, not scan nothing "
            "and exit 0."
        )
        stripped = _strip_comments(p.read_text())
        assert stripped.strip() != "", (
            f"comment-stripped {label} is empty -- nothing would ever be "
            "scanned by this module's other legs."
        )


def test_this_module_cannot_be_silently_skipped():
    """Coverage 5 -- this module's own source contains no runtime
    skip-bypass call, no conditional skip-marker decorator, and no
    dependency-skip call anywhere, so the fail-closed contract this module
    documents is self-enforcing rather than merely stated. The needle
    strings below are built via concatenation so this test's own source and
    its own failure messages cannot match its own check."""
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skip_cond_marker = "mark" + "." + "ski" + "pif"
    dependency_skip_call = "importor" + "skip"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- a "
        "missing or empty scan target must FAIL, never SKIP."
    )
    assert skip_cond_marker not in own_text, (
        "expected no @pytest." + skip_cond_marker + " decorator anywhere "
        "in this module -- a missing or empty scan target must FAIL, "
        "never SKIP."
    )
    assert ("pytest." + dependency_skip_call) not in own_text, (
        "expected no pytest." + dependency_skip_call + " call anywhere in "
        "this module -- a missing dependency must FAIL, never SKIP."
    )


def test_own_needles_do_not_appear_verbatim_in_this_module():
    """Coverage 6 -- the concatenated-needle self-check: none of the three
    concatenation-built needles above may appear verbatim anywhere in this
    module's own source, including inside this very test's failure
    messages. Without this leg, a future edit could silently
    un-concatenate one of them (for example while "simplifying" the code)
    and this gate would keep passing against the real source files while
    having quietly stopped being able to fail against itself."""
    own_text = Path(__file__).read_text()
    for label, needle in _ALL_SELF_CHECK_NEEDLES:
        assert needle not in own_text, (
            f"the concatenation-built needle for {label} appears verbatim "
            "in this module's own source -- rebuild it from at least two "
            "literal pieces so this gate cannot match itself."
        )
