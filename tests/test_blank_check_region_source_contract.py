"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 201 Plan 05 -- D-15.3's own source-contract gate. Plan 201-03 split the
write-init blank check into a region-scoped scan body and a one-line
whole-device wrapper over it; plan 201-04 bounded write and verify on the
same operation-end resolution point. Neither plan touched dispatch: the
region-scoped scan body still has exactly one caller (the write-init path),
and the whole-device wrapper still has every other caller it always had.
This module pins that shape mechanically, so a future edit cannot quietly
repoint one more caller at the region-scoped body and widen the relaxation
onto silicon this phase never benched.

Requirements: BLANK-02

Defect class this closes: a relaxation that is correct today but has no
mechanical fence against later drifting wider. D-10 authorises the
region-scoped body at exactly one call site (the write-init path in
eprom.cpp); every other reference in the tree -- three direct calls to the
whole-device wrapper and six function-pointer assignments of it -- is
supposed to stay on the whole-device form. A caller census done once, by
hand, at authoring time proves nothing about the NEXT commit. This module
is that proof, re-run on every commit.

Caller census (RESEARCH.md "Caller census -- 9 reference sites, not 4"):
this module enumerates all nine, not the four an identity-only reading of
CONTEXT.md's D-09 would suggest -- three direct calls
(eprom.cpp's write-init body, flash_intel.cpp, flash_nor_unlock.cpp) and six
function-pointer assignments (two in eprom.cpp, one each in
flash_nor_unlock.cpp, flash_5v_page.cpp, eeprom_28c.cpp and flash_intel.cpp).
A gate that counted only the three direct calls would be blind to exactly
the shape a future widening would take: repointing one of the six
function-pointer assignments at the region-scoped body instead of the
wrapper.

Coverage:
  1. test_region_form_is_defined_exactly_once_with_three_parameters --
     the region-scoped scan body is defined exactly once, in memory.cpp,
     with a three-parameter signature (handle, start, end).
  2. test_the_whole_device_wrapper_is_defined_exactly_once_and_passes_zero_and_device_size --
     the positive counterpart to Coverage 4: the whole-device wrapper is
     defined exactly once and its brace-matched body calls the region form
     with 0 and the device-size member -- without this leg, deleting every
     caller of the wrapper would satisfy Coverage 4 vacuously.
  3. test_eprom_cpp_calls_the_region_form_exactly_once_inside_write_init_body --
     eprom.cpp contains exactly one call to the region form, and it lies
     inside the brace-matched body of the write-init path's internal
     helper.
  4. test_out_of_scope_protocol_files_contain_zero_region_form_calls --
     zero calls to the region form in the four out-of-scope protocol
     files D-10 puts out of scope for this phase.
  5. test_exactly_six_whole_device_function_pointer_assignments_and_zero_region_form_ones --
     exactly six assignments of the whole-device wrapper to
     handle->firestarter_operation_main or handle->firestarter_operation_end
     across the five protocol source files that reference either form, and
     zero assignments of the region form to either pointer.
  6. test_operation_end_is_defined_exactly_once_and_reads_both_members --
     the operation-end resolution point (plan 201-04's D-06 anchor) is
     defined exactly once and its body reads both the region-end member
     and the device-size member, so the fail-closed clamp cannot be
     silently dropped while leaving the function apparently intact.
  7. test_scan_targets_are_non_vacuous -- every scan target (the
     overridable one and the five fixed ones) exists, is non-empty,
     resolves inside this repository, and its comment-stripped text is
     non-empty. A missing or empty scan target must FAIL, never silently
     pass as if nothing needed checking.
  8. test_this_module_cannot_be_silently_skipped -- this module's own
     source contains no skip-bypass call, no skip-marker decorator and no
     dependency-skip call anywhere.
  9. test_own_needles_do_not_appear_verbatim_in_this_module -- the two
     concatenation-built needles (the region form's name and the wrapper's
     name) appear nowhere verbatim in this module's own source, so this
     gate cannot match itself.

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover this
one)
  - FIRESTARTER_BLANK_CHECK_REGION_SCAN_EPROM_SOURCE -- overrides the
    scanned src/proms/eprom.cpp path ONLY. The other five scan targets
    (memory.cpp and the four out-of-scope protocol files) have NO
    override -- the seam exists only so a planted violation can be
    scanned against a temporary copy of eprom.cpp without touching the
    real file, and the five non-overridable targets are what stop a
    stray environment value from making this gate vacuous: even with the
    seam pointed at an empty file, Coverage 1, 2, 4 (for its four fixed
    files) and 6 still scan the real tree. Binds at IMPORT time (the
    module-level Path(os.environ.get(...)) expression below), so a
    planted-violation run must set it in a CHILD PROCESS environment
    before this module is imported, never via a post-import monkeypatch.
    This phase's own non-vacuity proof plants directly into the real
    eprom.cpp / flash_intel.cpp files instead of using this seam, captures
    the run, and restores the file -- the safer way for a single-file
    target, matching test_write_path_source_contract_v131.py's own stated
    precedent for why it does the same for its single-file targets.

This module is a standalone pytest module: it adds no shared pytest
configuration or fixture-registration file, and it imports nothing beyond
the Python standard library. It never imports, parametrizes against, or
edits any pre-existing gate module; the scanning logic below is its own
independent re-derivation of this plan's own gate specification, following
test_write_path_source_contract_v131.py's shape element for element.
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent

_EPROM_REL = "src/proms/eprom.cpp"
_MEMORY_REL = "src/proms/memory.cpp"
_FLASH_INTEL_REL = "src/proms/flash_intel.cpp"
_FLASH_NOR_UNLOCK_REL = "src/proms/flash_nor_unlock.cpp"
_FLASH_5V_PAGE_REL = "src/proms/flash_5v_page.cpp"
_EEPROM_28C_REL = "src/proms/eeprom_28c.cpp"

# Environment seam -- binds at IMPORT time. See the module docstring's
# "Environment seams" section above. Only this one target is overridable.
_SCAN_EPROM = Path(
    os.environ.get(
        "FIRESTARTER_BLANK_CHECK_REGION_SCAN_EPROM_SOURCE",
        str(_REPO_ROOT / _EPROM_REL),
    )
)
# The remaining five targets have no override -- see the docstring section
# above for why. A stray environment value pointed at the eprom.cpp seam
# cannot make Coverage 1, 2, 4 (for these four files) or 6 vacuous.
_SCAN_MEMORY = _REPO_ROOT / _MEMORY_REL
_SCAN_FLASH_INTEL = _REPO_ROOT / _FLASH_INTEL_REL
_SCAN_FLASH_NOR_UNLOCK = _REPO_ROOT / _FLASH_NOR_UNLOCK_REL
_SCAN_FLASH_5V_PAGE = _REPO_ROOT / _FLASH_5V_PAGE_REL
_SCAN_EEPROM_28C = _REPO_ROOT / _EEPROM_28C_REL

_OUT_OF_SCOPE_TARGETS = (
    (_FLASH_INTEL_REL, _SCAN_FLASH_INTEL),
    (_FLASH_NOR_UNLOCK_REL, _SCAN_FLASH_NOR_UNLOCK),
    (_FLASH_5V_PAGE_REL, _SCAN_FLASH_5V_PAGE),
    (_EEPROM_28C_REL, _SCAN_EEPROM_28C),
)

# Every reference-site scan target, the region-scoped source (eprom.cpp)
# plus the four out-of-scope protocol files, for the six-assignment leg.
_ASSIGNMENT_SCAN_TARGETS = ((_EPROM_REL, _SCAN_EPROM),) + _OUT_OF_SCOPE_TARGETS

# Concatenation-built needles. Coverage 9 asserts neither appears verbatim
# anywhere in this module's own source -- see the module docstring and the
# plan's own warning: a gate that quotes its own forbidden or required
# tokens verbatim can end up matching itself. Each is split at a point
# strictly inside the wrapper's own name, so neither fragment alone --
# nor either full needle value -- ever appears as a contiguous run of
# characters anywhere else in this file.
_NEEDLE_WRAPPER = "mem_util_blank_c" + "heck"
_NEEDLE_REGION = "mem_util_blank_che" + "ck_region"

_ALL_SELF_CHECK_NEEDLES = (
    ("the whole-device wrapper", _NEEDLE_WRAPPER),
    ("the region-scoped scan body", _NEEDLE_REGION),
)

_REGION_DEF_RE = re.compile(
    r"\bvoid\s+"
    + _NEEDLE_REGION
    + r"\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*,\s*uint32_t\s+start\s*,"
    r"\s*uint32_t\s+end\s*\)\s*\{"
)
_REGION_CALL_RE = re.compile(r"\b" + _NEEDLE_REGION + r"\s*\(")
_REGION_CALL_ARGS_ZERO_AND_MEMSIZE_RE = re.compile(
    _NEEDLE_REGION
    + r"\s*\(\s*handle\s*,\s*0\s*,\s*handle\s*->\s*mem_size\s*\)"
)

_WRAPPER_DEF_RE = re.compile(
    r"\bvoid\s+" + _NEEDLE_WRAPPER + r"\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)

_WRITE_INIT_BODY_DEF_RE = re.compile(
    r"\bstatic\s+void\s+eprom_internal_write_init_body\s*\("
    r"\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)

_ASSIGN_RE = re.compile(
    r"handle\s*->\s*firestarter_operation_(?:main|end)\s*=\s*(\w+)\s*;"
)

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
    test_protocol_branch_inventory.py both use. Neither scan target
    contains a string or character literal anywhere outside a comment or
    an #include directive (confirmed by inspection at authoring time), so
    literal-stripping is not needed here."""
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
    `open_idx` -- a containment test ("is this call inside that function's
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
    def_re (a pattern ending in the function's opening brace) against
    stripped, or None if def_re does not match."""
    m = def_re.search(stripped)
    if not m:
        return None
    open_idx = m.end() - 1
    close_idx = _find_matching_brace(stripped, open_idx)
    return (open_idx, close_idx)


def _read_stripped(path):
    return _strip_comments(path.read_text())


# Tests


def test_region_form_is_defined_exactly_once_with_three_parameters():
    """Coverage 1 -- the region-scoped scan body is defined exactly once,
    in memory.cpp, with a three-parameter signature (handle, start, end).
    D-10 authorises exactly one such definition; a second definition
    elsewhere in the tree would be a second, unaudited scan body."""
    stripped = _read_stripped(_SCAN_MEMORY)
    matches = _REGION_DEF_RE.findall(stripped)
    assert len(matches) == 1, (
        "expected exactly 1 definition of the region-scoped scan body "
        f"(three-parameter signature) in {_MEMORY_REL}, found "
        f"{len(matches)}.\nGot (comment-stripped {_MEMORY_REL}):\n{stripped}"
    )


def test_the_whole_device_wrapper_is_defined_exactly_once_and_passes_zero_and_device_size():
    """Coverage 2 -- the positive counterpart to Coverage 4 (the
    out-of-scope negative). Without this leg, deleting every caller of the
    whole-device wrapper would satisfy Coverage 4 trivially -- there would
    be nothing left to call the region form from anywhere, so "zero calls
    in the out-of-scope files" would hold vacuously. This leg requires the
    wrapper to actually exist, exactly once, and to call the region form
    with 0 and the device-size member -- the whole-device operation
    expressed as a region."""
    stripped = _read_stripped(_SCAN_MEMORY)
    def_matches = list(_WRAPPER_DEF_RE.finditer(stripped))
    assert len(def_matches) == 1, (
        "expected exactly 1 definition of the whole-device wrapper in "
        f"{_MEMORY_REL}, found {len(def_matches)}.\n"
        f"Got (comment-stripped {_MEMORY_REL}):\n{stripped}"
    )
    body_span = _function_body_span(stripped, _WRAPPER_DEF_RE)
    assert body_span is not None and body_span[1] > body_span[0], (
        f"could not brace-match the whole-device wrapper's body in {_MEMORY_REL}."
    )
    body_text = stripped[body_span[0] : body_span[1] + 1]
    assert _REGION_CALL_ARGS_ZERO_AND_MEMSIZE_RE.search(body_text), (
        "expected the whole-device wrapper's body to call the region form "
        f"with (handle, 0, handle->mem_size) in {_MEMORY_REL} -- a wrapper "
        "that calls it with any other arguments is not expressing the "
        "whole device as a region.\nGot wrapper body:\n{body_text}".format(
            body_text=body_text
        )
    )


def test_eprom_cpp_calls_the_region_form_exactly_once_inside_write_init_body():
    """Coverage 3 -- the region form's one authorised call site (D-10):
    eprom.cpp contains exactly one call to it, and that call lies inside
    the brace-matched body of the write-init path's internal helper, not
    merely somewhere in the same file."""
    stripped = _read_stripped(_SCAN_EPROM)
    calls = list(_REGION_CALL_RE.finditer(stripped))
    assert len(calls) == 1, (
        "expected exactly 1 call to the region form in "
        f"{_EPROM_REL}, found {len(calls)} -- D-10 authorises exactly one "
        "call site.\nGot (comment-stripped "
        f"{_EPROM_REL}):\n{stripped}"
    )
    body_span = _function_body_span(stripped, _WRITE_INIT_BODY_DEF_RE)
    assert body_span is not None and body_span[1] > body_span[0], (
        "could not brace-match the write-init path's internal helper body "
        f"in {_EPROM_REL}."
    )
    call_idx = calls[0].start()
    assert body_span[0] <= call_idx <= body_span[1], (
        "the region form's one call site in "
        f"{_EPROM_REL} lies outside the write-init path's internal "
        f"helper body (call at index {call_idx}, helper body spans "
        f"[{body_span[0]}, {body_span[1]}]) -- D-10 authorises this call "
        "only inside that one function."
    )


def test_out_of_scope_protocol_files_contain_zero_region_form_calls():
    """Coverage 4 -- the negative counterpart to Coverage 2 (the wrapper's
    positive existence proof makes this non-vacuous). D-10 puts these four
    protocol files out of scope for this phase: none of them may call the
    region form, however it got there."""
    for rel, path in _OUT_OF_SCOPE_TARGETS:
        stripped = _read_stripped(path)
        calls = list(_REGION_CALL_RE.finditer(stripped))
        assert len(calls) == 0, (
            f"found {len(calls)} call(s) to the region form in {rel} -- "
            "D-10 puts this file out of scope for this phase; the region "
            "form is authorised at exactly one call site, in eprom.cpp's "
            "write-init path.\nGot (comment-stripped "
            f"{rel}):\n{stripped}"
        )


def test_exactly_six_whole_device_function_pointer_assignments_and_zero_region_form_ones():
    """Coverage 5 -- the six function-pointer assignments RESEARCH.md's
    caller census names (two in eprom.cpp, one each in the four
    out-of-scope protocol files) all still target the whole-device
    wrapper, and none targets the region form. This leg is belt-and-braces
    against a future SIGNATURE change: the region form's three-parameter
    signature does not match the
    void (*)(firestarter_handle_t*) function-pointer type today, so a
    compiler would reject repointing one of these six assignments at it as
    written -- this leg does not guard against something the compiler
    cannot already see, it guards against that type ever being loosened.
    Asserted as an EQUALITY against a named count, not a floor, so a
    seventh assignment appearing anywhere is caught exactly as loudly as a
    repointed one."""
    wrapper_hits = []
    region_hits = []
    for rel, path in _ASSIGNMENT_SCAN_TARGETS:
        stripped = _read_stripped(path)
        for m in _ASSIGN_RE.finditer(stripped):
            target = m.group(1)
            if target == _NEEDLE_WRAPPER:
                wrapper_hits.append(f"{rel}: {m.group(0).strip()}")
            elif target == _NEEDLE_REGION:
                region_hits.append(f"{rel}: {m.group(0).strip()}")
    assert len(wrapper_hits) == 6, (
        "expected exactly 6 function-pointer assignments of the "
        "whole-device wrapper to handle->firestarter_operation_main or "
        f"handle->firestarter_operation_end, found {len(wrapper_hits)}.\n"
        "Got:\n" + "\n".join(wrapper_hits)
    )
    assert len(region_hits) == 0, (
        "found a function-pointer assignment of the region form to "
        "handle->firestarter_operation_main or "
        "handle->firestarter_operation_end -- this is exactly the shape a "
        "future widening of the relaxation would take.\n"
        "Got:\n" + "\n".join(region_hits)
    )


def test_operation_end_is_defined_exactly_once_and_reads_both_members():
    """Coverage 6 -- plan 201-04's D-06 anchor: the operation-end
    resolution point (D-04's 0=absent=whole-device fallback plus the
    fail-closed clamp) is defined exactly once and its body reads both the
    region-end member and the device-size member, so the fail-closed
    clamp cannot be silently dropped while the function stays defined and
    apparently intact."""
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


def test_scan_targets_are_non_vacuous():
    """Coverage 7 -- structural self-check, never reads the environment
    seam: every DEFAULT scan target (recomputed fresh from _REPO_ROOT --
    the check_permitted_claims.py _HERE-resolves-to-the-wrong-directory
    landmine, closed here by construction) exists, is non-empty, resolves
    inside this repository, and its comment-stripped text is non-empty. A
    missing or empty scan target must FAIL, never silently pass as if
    nothing needed checking."""
    default_targets = (
        (_EPROM_REL, _REPO_ROOT / _EPROM_REL),
        (_MEMORY_REL, _REPO_ROOT / _MEMORY_REL),
    ) + _OUT_OF_SCOPE_TARGETS
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
    """Coverage 8 -- this module's own source contains no runtime
    skip-bypass call, no conditional skip-marker decorator, and no
    dependency-skip call anywhere, so the fail-closed contract this
    module documents is self-enforcing rather than merely stated. The
    three needle strings below are each built from three or more literal
    pieces -- never fewer than three, and never a piece that itself
    contains the marker's own middle six letters as a contiguous run --
    the same technique test_write_path_source_contract_v131.py's own
    equivalent check uses, so this test's own source and its own failure
    messages cannot match its own check, and neither can a would-be
    verification script doing a flat substring scan for that marker's
    name."""
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
    """Coverage 9 -- the concatenated-needle self-check: neither
    concatenation-built needle (the region form's name, the whole-device
    wrapper's name) may appear verbatim anywhere in this module's own
    source, including inside this very test's failure messages. Without
    this leg, a future edit could silently un-concatenate one of them
    (for example while "simplifying" the code) and this gate would keep
    passing against the real source files while having quietly stopped
    being able to fail against itself."""
    own_text = Path(__file__).read_text()
    for label, needle in _ALL_SELF_CHECK_NEEDLES:
        assert needle not in own_text, (
            f"the concatenation-built needle for {label} appears verbatim "
            "in this module's own source -- rebuild it from at least two "
            "literal pieces so this gate cannot match itself."
        )
