"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 141 Plan 06 -- closes LOOP-02's and LOOP-07's own WORDING with a
committed source-scan gate, mechanized rather than merely asserted in a
phase record. The D-13 branch-inventory gate (test_protocol_branch_inventory.py)
proves the write path's predicate SHAPE moved when Phase 141 rewrote it; it
does not prove that the four constructs LOOP-02 names BY IDENTITY are gone,
and no existing gate proves LOOP-07's claim is GLOBAL (that no call path
anywhere in the tree can reach delayMicroseconds() above its 16383 us
ceiling). This module supplies exactly that: a mechanical, CI-visible oracle
in place of a prose claim.

Requirements: LOOP-02, LOOP-07

Defect class this closes: TWO distinct gaps, both structural rather than
behavioural, so neither can be caught merely by running the firmware --

  (a) a requirement whose wording names constructs BY IDENTITY (a macro, two
      named functions, a specific stack array) cannot be proven removed by a
      predicate-shape inventory alone -- a shape-preserving rewrite could in
      principle keep any of the four alive under a different branch shape,
      and the branch-inventory gate would not notice, because it counts
      branch predicates, not identifiers.

  (b) a GLOBAL claim about a call ceiling ("no call path can reach
      delayMicroseconds() above 16383 us") cannot be proven by a per-case
      native test alone, because a per-case test only ever exercises the
      cases it was written for -- it cannot show that every OTHER call site
      in the tree is already safe. A source-scan gate can.

Coverage:
  1. test_number_of_retries_macro_is_absent_from_the_write_path -- zero
     matches for the retry-count macro identifier (needle
     concatenation-built) in the comment-stripped write path.
  2. test_the_removed_block_mismatch_reporter_function_is_absent -- zero
     matches for the removed block-level mismatch-reporting function's
     identifier (needle concatenation-built). Named descriptively rather
     than after the identifier itself -- see the note directly below the
     Environment seams section for why.
  3. test_the_removed_verify_mask_updater_function_is_absent -- zero
     matches for the removed mask-updating function's identifier (needle
     concatenation-built), same shape as Coverage 2 and the same naming
     note applies.
  4. test_adaptive_pulse_growth_formula_is_absent -- zero matches for an
     assignment to handle->pulse_delay whose right-hand side contains both
     a '*' and a '/' (the shape of the removed adaptive-growth formula: a
     base delay scaled by a retries-proportional term), and zero matches
     for the removed byte-mismatch bitmask identifier (needle
     concatenation-built).
  5. test_the_per_byte_loop_constructs_are_present -- the positive
     counterpart to Coverage 1-4: the rewritten write path still contains
     firestarter_set_data, firestarter_get_data, MSG_ERR_MAX_PULSES and
     MSG_ERR_ENERGY_CAP, and the single budget-failure reporter is defined
     exactly once and called at least twice.
  6. test_no_unclamped_pulse_delay_reaches_delaymicroseconds -- zero
     matches for delayMicroseconds(handle->pulse_delay) across BOTH D-06
     sites (src/proms/eprom.cpp and src/proms/memory.cpp), comment-stripped.
  7. test_both_over_ceiling_sites_route_through_the_safe_helper -- the
     positive counterpart to Coverage 6: mem_util_delay_us(...) appears
     exactly once in EACH of the two D-06 sites -- exactly two call sites
     in total. A deleted call satisfies Coverage 6 vacuously and fails
     here. AMENDED (debug session w27c512-devtest-all-bad): site 2's
     argument is now EPROM_ERASE_PULSE_US, not handle->pulse_delay -- the
     erase pulse is not the program pulse -- and this leg additionally
     asserts ZERO mem_util_delay_us(handle->pulse_delay) sites in
     src/proms/eprom.cpp.
  8. test_every_remaining_delaymicroseconds_argument_is_a_literal_or_a_clamped_value
     -- sweeps src/, include/, lib/ and platform/ for every remaining
     delayMicroseconds(...) CALL (never a macro-definition body, never the
     function's own definition) and asserts each argument is a decimal
     literal or one of the read path's three already-safe names
     (settling, strobe, the split helper's own bounded remainder).
  9. test_the_split_helper_ceiling_is_16383 -- MEM_UTIL_DELAY_US_MAX is
     defined as 16383UL in src/proms/memory.cpp and the split's first
     branch compares with <= against it, pinning the boundary that keeps
     every shipped pulse width emitting a single delayMicroseconds() call.
  10. test_scan_targets_are_non_vacuous -- both default scan targets exist,
      are non-empty, resolve inside this repository, and their
      comment-stripped text is non-empty. A missing or empty scan target
      must FAIL, never silently pass as if nothing needed checking.
  11. test_this_module_cannot_be_silently_skipped -- this module's own
      source contains no skip-bypass call, no skip-marker decorator and no
      dependency-skip call anywhere (all three checked via
      concatenation-built needles, the same technique Coverage 12 uses).
  12. test_own_needles_do_not_appear_verbatim_in_this_module -- the four
      concatenation-built LOOP-02 needles (Coverage 1-4) appear nowhere
      verbatim in this module's own source, so this gate cannot match
      itself.

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover them)
  - FIRESTARTER_WRITE_PATH_SCAN_SOURCE -- overrides the scanned
    src/proms/eprom.cpp path ONLY. src/proms/memory.cpp has no override --
    D-06 names it as a fixed site, and this module's own armed-gate proof
    exercises it the safer way for a single-file target (a temporary plant
    in the real file, captured, then restored) rather than adding a second
    seam. Consulted by Coverage 1, 2, 3, 4, 5, 6 and 7 (the eprom.cpp half
    only). Binds at IMPORT time (the module-level Path(os.environ.get(...))
    expression below), so a planted-violation run must set it in a CHILD
    PROCESS environment before this module is imported, never via a
    post-import monkeypatch. Coverage 8, 9, 10, 11 and 12 deliberately
    never read it: 8 and 9 scan fixed targets by design (D-06 names them
    and each is proven the seamless way); 10 recomputes the default
    targets directly from the repository root without ever reading
    os.environ; 11 and 12 read only this module's own source. A stray seam
    value left set after a planted run cannot make any of these five pass
    vacuously -- at worst it makes Coverage 1-7 fail loudly.

CI framing, stated honestly: `pytest tests/ -v` appears in
`.github/workflows/build.yml:161` and `.github/workflows/beta-build.yml:134`,
so this module -- like every other file under tests/ -- WILL run in CI once
this branch reaches `main` or `beta`. It does not run in any CI leg on the
milestone branch itself; that is a statement about this branch's current CI
wiring, not a claim that this module is permanently invisible to CI.

Naming note for Coverage 2 and 3: the two removed C functions' own names,
spelled out in full, are each a strict substring of the "obvious" test name
for the leg that proves them absent (the English words describing the
functions are, verbatim, the identifiers themselves) -- so a test literally
named after either identifier would make this module's own source contain,
verbatim, a needle it must instead prove is concatenation-built (Coverage
12's own check, and the same self-match hazard the module docstring's
opening paragraphs warn about). Coverage 2 and 3 are therefore named after
what the removed functions DID rather than after their identifiers; the
needle each leg actually searches for is still concatenation-built and
still exactly the removed identifier.

This module is a standalone pytest module: it is not named check_*.py (that
glob belongs to a different, paired convention this module does not need),
it adds no shared pytest configuration or fixture-registration file
anywhere -- firestarter/tests/ has none, by house convention, and this
module does not introduce one -- and it imports nothing beyond the Python
standard library. It never imports, parametrizes against, or edits any
pre-existing gate module; the scanning logic below is its own independent
re-derivation of this plan's own gate specification.
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_EPROM_REL = "src/proms/eprom.cpp"
_MEMORY_REL = "src/proms/memory.cpp"

# Environment seam -- binds at IMPORT time. See the module docstring's
# "Environment seams" section above. Coverage 8, 9, 10, 11 and 12
# deliberately never read this.
_SCAN_EPROM = Path(
    os.environ.get("FIRESTARTER_WRITE_PATH_SCAN_SOURCE", str(_REPO_ROOT / _EPROM_REL))
)
# memory.cpp has no override -- see the "Environment seams" docstring
# section for why.
_SCAN_MEMORY = _REPO_ROOT / _MEMORY_REL

# Concatenation-built needles. Coverage 12 asserts none of these appear
# verbatim anywhere in this module's own source -- see the module docstring
# and the plan's own warning: a gate that quotes its forbidden tokens
# verbatim matches itself and can never pass.
_NEEDLE_RETRY_MACRO = "NUMBER" + "_OF_RETRIES"
_NEEDLE_PROGRAM_MISMATCHED_BYTES = "program_mismatched" + "_bytes"
_NEEDLE_VERIFY_AND_UPDATE_MASK = "verify_and_update" + "_mask"
_NEEDLE_MISMATCH_BITMASK = "mismatch_bit" + "mask"

_ALL_SELF_CHECK_NEEDLES = (
    ("the retry-count macro", _NEEDLE_RETRY_MACRO),
    ("the removed block-mismatch reporting function", _NEEDLE_PROGRAM_MISMATCHED_BYTES),
    ("the removed mask-updating function", _NEEDLE_VERIFY_AND_UPDATE_MASK),
    ("the removed byte-mismatch bitmask", _NEEDLE_MISMATCH_BITMASK),
)

_PULSE_DELAY_ASSIGN_RE = re.compile(r"handle\s*->\s*pulse_delay\s*=(?!=)\s*([^;]*);")
_UNCLAMPED_PULSE_DELAY_RE = re.compile(
    r"delayMicroseconds\s*\(\s*handle\s*->\s*pulse_delay\s*\)"
)
_SAFE_HELPER_PULSE_DELAY_RE = re.compile(
    r"mem_util_delay_us\s*\(\s*handle\s*->\s*pulse_delay\s*\)"
)
_SAFE_HELPER_ERASE_PULSE_RE = re.compile(
    r"mem_util_delay_us\s*\(\s*EPROM_ERASE_PULSE_US\s*\)"
)

_FIRESTARTER_SET_DATA_RE = re.compile(r"\bfirestarter_set_data\b")
_FIRESTARTER_GET_DATA_RE = re.compile(r"\bfirestarter_get_data\b")
_MSG_ERR_MAX_PULSES_RE = re.compile(r"\bMSG_ERR_MAX_PULSES\b")
_MSG_ERR_ENERGY_CAP_RE = re.compile(r"\bMSG_ERR_ENERGY_CAP\b")
_BUDGET_FAILURE_DEF_RE = re.compile(r"\bvoid\s+eprom_internal_report_budget_failure\s*\(")
_BUDGET_FAILURE_CALL_RE = re.compile(
    r"\beprom_internal_report_budget_failure\s*\(\s*handle\s*,"
)

_DELAY_US_CALL_RE = re.compile(r"\bdelayMicroseconds\s*\(\s*([^()]*?)\s*\)")
_DELAY_US_DEFINITION_LINE_RE = re.compile(r"\bvoid\s+delayMicroseconds\s*\(")
_ALLOWED_DELAY_US_ARGS = frozenset({"settling", "strobe", "rem"})

# Macro-constant arguments additionally permitted at a delayMicroseconds()
# call site (debug session w27c512-program-fail-byte0). These are NOT clamped
# runtime names like the three above -- each must be an object-like #define
# under include/ whose replacement list is a plain decimal literal, i.e. a
# named literal. The companion leg
# test_every_allowlisted_delay_us_macro_resolves_to_an_in_range_literal
# RESOLVES every name in this set against the real headers and range-checks
# the value, and fails closed if a name resolves to nothing, so adding a name
# here cannot smuggle in an unbounded, runtime or out-of-range argument the
# way widening _ALLOWED_DELAY_US_ARGS would.
_ALLOWED_DELAY_US_MACROS = frozenset({"EPROM_VPP_SETUP_US", "EPROM_VPP_HOLD_US"})

_SPLIT_MAX_DEFINE_RE = re.compile(r"#\s*define\s+MEM_UTIL_DELAY_US_MAX\s+16383UL\b")
_SPLIT_LE_COMPARE_RE = re.compile(r"\bif\s*\(\s*us\s*<=\s*MEM_UTIL_DELAY_US_MAX\s*\)")

_SCAN_TREE_DIRS = ("src", "include", "lib", "platform")
_SCAN_TREE_EXTS = (".c", ".cpp", ".h", ".hpp", ".ino")


def _strip_comments(text):
    """Strip `//` line comments and `/* ... */` block comments, replacing
    each stripped span with whitespace of the SAME SHAPE (a newline stays a
    newline, everything else becomes a single space) so every line number
    in the result matches the original file exactly -- the same technique
    test_protocol_branch_inventory.py's own comment-and-literal stripper
    uses, narrowed to comments only. Both of this module's scan targets
    contain no string or character literal anywhere outside of a comment
    or an #include directive (confirmed by inspection at authoring time),
    so literal-stripping is not needed here."""
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


def _line_of(text, idx):
    return text.count("\n", 0, idx) + 1


def _iter_tree_source_files():
    """Every source file under src/, include/, lib/ and platform/ with a
    C/C++ extension -- the exact scope 141-RESEARCH.md's delayMicroseconds()
    call-site inventory used, and no wider: test/ and tests/ (the native
    and host-side test trees) are deliberately excluded, since LOOP-07's
    claim is about the SHIPPED write path, not the harnesses that exercise
    it."""
    for top in _SCAN_TREE_DIRS:
        base = _REPO_ROOT / top
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if p.is_file() and p.suffix in _SCAN_TREE_EXTS:
                yield p


def _delaymicroseconds_call_sites(paths):
    """Yield (path, line_no, argument) for every delayMicroseconds(...)
    CALL across the given paths, comment-stripped -- never a #define macro
    body (RURP_DELAY_US's own two AVR/native-branch definitions in
    include/rurp_platform.h are dead text: nothing under src/ ever invokes
    RURP_DELAY_US, so those two lines are never expanded on any target this
    tree compiles) and never the function's own definition (the py32
    Arduino.h compatibility shim defines its own delayMicroseconds() and
    calls RURP_DELAY_US from inside its body -- that is a definition, not a
    call, and is excluded by the same rule)."""
    sites = []
    for path in paths:
        text = path.read_text(errors="replace")
        stripped = _strip_comments(text)
        for line_no, line in enumerate(stripped.splitlines(), start=1):
            probe = line.strip()
            if probe.startswith("#define"):
                continue
            if _DELAY_US_DEFINITION_LINE_RE.search(line):
                continue
            for m in _DELAY_US_CALL_RE.finditer(line):
                sites.append((path, line_no, m.group(1).strip()))
    return sites


def _assert_identifier_absent(needle, label, stripped, target_rel):
    matches = re.findall(r"\b" + re.escape(needle) + r"\b", stripped)
    assert matches == [], (
        f"found {len(matches)} occurrence(s) of {label} in the "
        f"comment-stripped {target_rel} -- LOOP-02 requires this construct "
        "removed from the write path entirely, by identity, not merely "
        f"reshaped.\nGot (comment-stripped {target_rel}):\n{stripped}"
    )


# Tests


def test_number_of_retries_macro_is_absent_from_the_write_path():
    """Coverage 1 -- LOOP-02 names the block loop's own retry-count macro
    by identity; this leg proves it gone from the comment-stripped write
    path via a concatenation-built needle (see module docstring)."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    _assert_identifier_absent(
        _NEEDLE_RETRY_MACRO, "the retry-count macro identifier", stripped, _EPROM_REL
    )


def test_the_removed_block_mismatch_reporter_function_is_absent():
    """Coverage 2 -- LOOP-02 names the removed block-level mismatch
    reporting function by identity; same shape as Coverage 1. Named after
    what the function did rather than after its identifier -- see the
    module docstring's "Naming note for Coverage 2 and 3" -- but the needle
    this leg searches for is still the exact, concatenation-built removed
    identifier."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    _assert_identifier_absent(
        _NEEDLE_PROGRAM_MISMATCHED_BYTES,
        "the removed block-mismatch reporting function's identifier",
        stripped,
        _EPROM_REL,
    )


def test_the_removed_verify_mask_updater_function_is_absent():
    """Coverage 3 -- LOOP-02 names the removed mask-updating function by
    identity; same shape as Coverage 1 and 2, and the same naming note
    applies (module docstring, "Naming note for Coverage 2 and 3")."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    _assert_identifier_absent(
        _NEEDLE_VERIFY_AND_UPDATE_MASK,
        "the removed mask-updating function's identifier",
        stripped,
        _EPROM_REL,
    )


def test_adaptive_pulse_growth_formula_is_absent():
    """Coverage 4 -- LOOP-02's fourth named construct is not a single
    identifier but a SHAPE: an assignment to handle->pulse_delay whose
    right-hand side scales a base delay by a retries-proportional term (it
    contains both a '*' and a '/'). D-07's two legitimate assignments to
    handle->pulse_delay -- the overprogram set and its immediate restore --
    must both stay simple, arithmetic-free assignments, which is what makes
    this leg discriminating rather than blanket: it does not fire on either
    of them, but would fire the instant either one gained multiplication
    and division on its right-hand side. Paired with the removed
    byte-mismatch bitmask identifier (needle concatenation-built) as the
    second half of this leg."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    growth_hits = []
    for m in _PULSE_DELAY_ASSIGN_RE.finditer(stripped):
        rhs = m.group(1)
        if "*" in rhs and "/" in rhs:
            growth_hits.append(
                f"{_EPROM_REL}:{_line_of(stripped, m.start())}: {m.group(0).strip()}"
            )
    assert growth_hits == [], (
        "found an assignment to handle->pulse_delay whose right-hand side "
        "contains both '*' and '/' -- this is the shape of the removed "
        "adaptive pulse-growth formula (a base delay scaled by a "
        "retries-proportional term). D-07's two legitimate assignments "
        "(the overprogram set and its immediate restore) must both stay "
        "simple, arithmetic-free assignments.\n"
        + "\n".join(growth_hits)
        + f"\nGot (comment-stripped {_EPROM_REL}):\n{stripped}"
    )
    _assert_identifier_absent(
        _NEEDLE_MISMATCH_BITMASK,
        "the removed byte-mismatch bitmask identifier",
        stripped,
        _EPROM_REL,
    )


def test_the_per_byte_loop_constructs_are_present():
    """Coverage 5 -- the positive counterpart to Coverage 1-4: an emptied
    or gutted eprom.cpp would satisfy every absence leg above vacuously.
    This leg requires the rewritten per-byte loop's OWN constructs to
    actually be present, so deletion-without-replacement fails here even
    when it passes every absence leg."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    for label, rx in (
        ("firestarter_set_data", _FIRESTARTER_SET_DATA_RE),
        ("firestarter_get_data", _FIRESTARTER_GET_DATA_RE),
        ("MSG_ERR_MAX_PULSES", _MSG_ERR_MAX_PULSES_RE),
        ("MSG_ERR_ENERGY_CAP", _MSG_ERR_ENERGY_CAP_RE),
    ):
        count = len(rx.findall(stripped))
        assert count > 0, (
            f"expected at least one occurrence of {label} in the "
            f"comment-stripped {_EPROM_REL}, found {count} -- the "
            "rewritten per-byte loop must still use this construct.\n"
            f"Got (comment-stripped {_EPROM_REL}):\n{stripped}"
        )
    def_count = len(_BUDGET_FAILURE_DEF_RE.findall(stripped))
    call_count = len(_BUDGET_FAILURE_CALL_RE.findall(stripped))
    assert def_count == 1, (
        "expected exactly 1 definition of the per-byte budget-failure "
        f"reporter in {_EPROM_REL}, found {def_count}.\n"
        f"Got (comment-stripped {_EPROM_REL}):\n{stripped}"
    )
    assert call_count >= 2, (
        "expected at least 2 call sites of the per-byte budget-failure "
        f"reporter in {_EPROM_REL} (one per LOOP-05 budget limit), found "
        f"{call_count}.\nGot (comment-stripped {_EPROM_REL}):\n{stripped}"
    )


def test_no_unclamped_pulse_delay_reaches_delaymicroseconds():
    """Coverage 6 -- LOOP-07's claim is GLOBAL. D-06 names the complete
    over-ceiling-capable inventory as exactly two sites, both taking the
    unclamped handle->pulse_delay: memory.cpp's program pulse and
    eprom.cpp's erase pulse. This leg asserts zero matches for
    delayMicroseconds(handle->pulse_delay) across BOTH, comment-stripped."""
    eprom_stripped = _strip_comments(_SCAN_EPROM.read_text())
    memory_stripped = _strip_comments(_SCAN_MEMORY.read_text())
    eprom_hits = _UNCLAMPED_PULSE_DELAY_RE.findall(eprom_stripped)
    memory_hits = _UNCLAMPED_PULSE_DELAY_RE.findall(memory_stripped)
    assert eprom_hits == [] and memory_hits == [], (
        "found an unclamped delayMicroseconds(handle->pulse_delay) call -- "
        f"{len(eprom_hits)} in {_EPROM_REL}, {len(memory_hits)} in "
        f"{_MEMORY_REL}. LOOP-07's claim is GLOBAL: D-06 names exactly two "
        "over-ceiling-capable sites and both must route through the safe "
        "split helper instead.\n"
        f"Got ({_EPROM_REL}):\n{eprom_stripped}\n\n"
        f"Got ({_MEMORY_REL}):\n{memory_stripped}"
    )


def test_both_over_ceiling_sites_route_through_the_safe_helper():
    """Coverage 7 -- the positive counterpart to Coverage 6: a deleted call
    (rather than a rerouted one) satisfies the absence leg above
    vacuously. This leg requires exactly one mem_util_delay_us(...) call
    site in EACH of the two D-06 sites -- exactly two in total -- so
    deletion fails here even when it passes Coverage 6.

    AMENDED, debug session w27c512-devtest-all-bad. Site 2 (the erase
    pulse in src/proms/eprom.cpp) no longer passes handle->pulse_delay:
    that value is the per-BYTE PROGRAM width and using it as the CE ERASE
    pulse emitted ~100 us against a datasheet T_PWE of 95/100/105 ms. Site
    2's argument is now the EPROM_ERASE_PULSE_US constant, so this leg
    keeps its full strength by asserting each site's OWN expected argument
    rather than dropping the site:

      * site 1 (memory.cpp, the program pulse)  -> handle->pulse_delay
      * site 2 (eprom.cpp, the erase pulse)     -> EPROM_ERASE_PULSE_US

    and it now ALSO asserts the negative that motivated the amendment:
    zero mem_util_delay_us(handle->pulse_delay) sites in eprom.cpp, so a
    regression that puts the program pulse back on the erase path fails
    here as well as in
    test_vpp_eprom_v131.cpp::test_erase_ce_pulse_width_is_the_datasheet_
    erase_pulse_not_the_program_pulse. The D-06 ceiling property this leg
    exists for is unchanged: both over-ceiling arguments still go through
    mem_util_delay_us's split helper, never a bare delayMicroseconds."""
    eprom_stripped = _strip_comments(_SCAN_EPROM.read_text())
    memory_stripped = _strip_comments(_SCAN_MEMORY.read_text())
    eprom_hits = _SAFE_HELPER_ERASE_PULSE_RE.findall(eprom_stripped)
    memory_hits = _SAFE_HELPER_PULSE_DELAY_RE.findall(memory_stripped)
    eprom_program_pulse_hits = _SAFE_HELPER_PULSE_DELAY_RE.findall(eprom_stripped)
    assert len(eprom_hits) == 1, (
        "expected exactly 1 mem_util_delay_us(EPROM_ERASE_PULSE_US) call "
        f"site in {_EPROM_REL} (D-06 site 2, the erase pulse), found "
        f"{len(eprom_hits)}.\nGot ({_EPROM_REL}):\n{eprom_stripped}"
    )
    assert len(memory_hits) == 1, (
        "expected exactly 1 mem_util_delay_us(handle->pulse_delay) call "
        f"site in {_MEMORY_REL} (D-06 site 1, the pulse), found "
        f"{len(memory_hits)}.\nGot ({_MEMORY_REL}):\n{memory_stripped}"
    )
    assert len(eprom_program_pulse_hits) == 0, (
        "found mem_util_delay_us(handle->pulse_delay) in "
        f"{_EPROM_REL} -- handle->pulse_delay is the per-BYTE PROGRAM pulse "
        "width and must never be spent as the CE ERASE pulse; use "
        f"EPROM_ERASE_PULSE_US.\nGot ({_EPROM_REL}):\n{eprom_stripped}"
    )
    total = len(eprom_hits) + len(memory_hits)
    assert total == 2, (
        "expected exactly 2 mem_util_delay_us(...) call sites in total "
        f"across both D-06 sites, found {total}"
    )


def test_every_remaining_delaymicroseconds_argument_is_a_literal_or_a_clamped_value():
    """Coverage 8 -- 141-RESEARCH.md's exhaustive inventory found 12 direct
    call sites plus one macro indirection (RURP_DELAY_US, whose only call
    site in the tree is the PY32's own delayMicroseconds shim, so it adds
    no AVR path). This leg re-derives that inventory live and is what keeps
    it from silently growing: every delayMicroseconds(...) CALL under src/,
    include/, lib/ and platform/ must take a decimal literal, or one of the
    two names the read path clamps to 1000 us (settling, strobe), or the
    split helper's own bounded remainder variable -- never an unclamped
    identifier."""
    paths = list(_iter_tree_source_files())
    assert paths, (
        "non-vacuous guard: the tree-wide sweep over "
        f"{_SCAN_TREE_DIRS!r} visited zero files under {_REPO_ROOT} -- a "
        "locator resolving to nothing must FAIL, never read as nothing to "
        "report."
    )
    sites = _delaymicroseconds_call_sites(paths)
    assert sites, (
        "non-vacuous guard: the tree-wide sweep found zero "
        "delayMicroseconds(...) call sites -- a zero-call-site scan must "
        "FAIL, never silently pass as 'nothing to check'."
    )
    violations = [
        (p, ln, arg)
        for (p, ln, arg) in sites
        if not re.fullmatch(r"\d+", arg)
        and arg not in _ALLOWED_DELAY_US_ARGS
        and arg not in _ALLOWED_DELAY_US_MACROS
    ]
    assert violations == [], (
        "found delayMicroseconds() call(s) whose argument is neither a "
        "decimal literal, nor one of the read path's clamped names "
        f"{sorted(_ALLOWED_DELAY_US_ARGS)!r}, nor one of the resolved "
        f"named-literal macros {sorted(_ALLOWED_DELAY_US_MACROS)!r}:\n"
        + "\n".join(f"{p}:{ln}: delayMicroseconds({a})" for p, ln, a in violations)
    )


def test_every_allowlisted_delay_us_macro_resolves_to_an_in_range_literal():
    """Companion to Coverage 8 (debug session w27c512-program-fail-byte0).

    The leg above accepts a small set of MACRO names at a
    delayMicroseconds() call site. On its own that would be a bare
    allowlist -- a name nobody re-checks. This leg closes that: every name
    in _ALLOWED_DELAY_US_MACROS must resolve, in the real headers under
    include/, to an object-like #define whose replacement list is a plain
    decimal literal, and that literal must sit in [1, 16383] -- the same
    accurate ceiling test_the_split_helper_ceiling_is_16383 below pins,
    because a macro argument gets no split-helper protection.

    Fails closed three ways: an allowlisted name that resolves to nothing;
    a name that resolves to something other than a decimal literal (an
    expression, a cast, another macro); and a name whose value is outside
    the accurate range. A name defined more than once also fails -- two
    definitions mean the value a call site gets depends on include order,
    which this leg cannot read.
    """
    headers = [
        p
        for p in _iter_tree_source_files()
        if p.suffix in (".h", ".hpp") and p.is_relative_to(_REPO_ROOT / "include")
    ]
    assert headers, (
        "non-vacuous guard: the sweep found zero headers under "
        f"{_REPO_ROOT / 'include'} -- a locator resolving to nothing must "
        "FAIL, never read as nothing to check."
    )
    assert _ALLOWED_DELAY_US_MACROS, (
        "non-vacuous guard: _ALLOWED_DELAY_US_MACROS is empty, so this leg "
        "would assert nothing. Empty it only together with the macro arm of "
        "the violations filter in the leg above."
    )

    resolved = {}
    unparsed = {}
    for name in sorted(_ALLOWED_DELAY_US_MACROS):
        # Object-like define only: the name must be followed by whitespace,
        # never by '(' -- a function-like macro takes an argument this leg
        # cannot range-check.
        define_re = re.compile(
            r"^[ \t]*#[ \t]*define[ \t]+" + re.escape(name) + r"[ \t]+(.+?)[ \t]*$",
            re.M,
        )
        hits = []
        for p in headers:
            for m in define_re.finditer(_strip_comments(p.read_text(encoding="utf-8"))):
                hits.append((p, m.group(1)))
        assert len(hits) == 1, (
            f"{name} must have exactly ONE object-like #define under "
            f"include/ for its value to be readable here; found {len(hits)}: "
            f"{[str(p) for p, _ in hits]}"
        )
        _, body = hits[0]
        if re.fullmatch(r"\d+", body):
            resolved[name] = int(body)
        else:
            unparsed[name] = body

    assert unparsed == {}, (
        "every allowlisted delayMicroseconds() macro must expand to a plain "
        "decimal literal -- these do not, so their runtime value cannot be "
        f"range-checked from source: {unparsed!r}"
    )
    out_of_range = {n: v for n, v in resolved.items() if not 1 <= v <= 16383}
    assert out_of_range == {}, (
        "AVR delayMicroseconds() takes a 16-bit unsigned int and its 16 MHz "
        "arm computes `us <<= 2`, overflowing above 16383, and a macro "
        "argument bypasses mem_util_delay_us's split helper entirely -- "
        f"these allowlisted macros are outside [1, 16383]: {out_of_range!r}"
    )


def test_the_split_helper_ceiling_is_16383():
    """Coverage 9 -- pins the exact boundary LOOP-07's split helper relies
    on: AVR's delayMicroseconds() takes a 16-bit unsigned int and its 16
    MHz arm computes `us <<= 2`, which overflows at 16384, so 16383 is the
    exact accurate ceiling (framework-arduino-avr 5.3.0
    cores/arduino/wiring.c:120,167-183). A request of exactly 16383 must
    NOT split -- it fits and is accurate as a single delayMicroseconds()
    call -- which is what keeps every shipped pulse width (the widest
    shipped value is 1000 us) emitting one delayMicroseconds() call and
    keeps a later trace diff attributable to cadence rather than to a
    changed split boundary."""
    stripped = _strip_comments(_SCAN_MEMORY.read_text())
    assert _SPLIT_MAX_DEFINE_RE.search(stripped), (
        "expected '#define MEM_UTIL_DELAY_US_MAX 16383UL' in "
        f"{_MEMORY_REL}.\nGot (comment-stripped {_MEMORY_REL}):\n{stripped}"
    )
    assert _SPLIT_LE_COMPARE_RE.search(stripped), (
        "expected the split helper's first branch to compare with '<=' "
        f"against MEM_UTIL_DELAY_US_MAX in {_MEMORY_REL} -- a '<' here "
        "would make a request of exactly 16383 us take the millisecond "
        "split path unnecessarily.\n"
        f"Got (comment-stripped {_MEMORY_REL}):\n{stripped}"
    )


def test_scan_targets_are_non_vacuous():
    """Coverage 10 -- structural self-check, never reads the environment
    seam: both DEFAULT scan targets (recomputed fresh from _REPO_ROOT --
    the check_permitted_claims.py _HERE-resolves-to-the-wrong-directory
    landmine, closed here by construction, the same technique
    test_protocol_branch_inventory.py's own Coverage 6 uses) exist, are
    non-empty, resolve inside this repository, and their comment-stripped
    text is non-empty. A missing or empty scan target must FAIL, never
    silently pass as if nothing needed checking."""
    default_eprom = _REPO_ROOT / _EPROM_REL
    default_memory = _REPO_ROOT / _MEMORY_REL
    for label, p in (("eprom.cpp", default_eprom), ("memory.cpp", default_memory)):
        assert p.is_file(), (
            f"default {label} scan target {p} does not exist on disk -- a "
            "missing scan target must FAIL, never silently pass."
        )
        assert p.stat().st_size > 0, f"default {label} scan target {p} is empty"
        assert p.resolve().is_relative_to(_REPO_ROOT), (
            f"default {label} scan target {p} resolves outside _REPO_ROOT "
            f"({_REPO_ROOT}) -- a naive future copy of this module into "
            "another directory must fail loudly here, not scan nothing and "
            "exit 0."
        )
        stripped = _strip_comments(p.read_text())
        assert stripped.strip() != "", (
            f"comment-stripped {label} is empty -- nothing would ever be "
            "scanned by this module's other legs."
        )


def test_this_module_cannot_be_silently_skipped():
    """Coverage 11 -- this module's own source contains no runtime
    skip-bypass call, no skip-marker decorator, and no dependency-skip call
    anywhere, so the fail-closed contract this module documents is
    self-enforcing rather than merely stated. The three needle strings
    below are built via concatenation -- the same technique
    test_vpp_seam_manual_on_every_board.py uses for its own equivalent
    check -- so this test's own source and its own failure messages cannot
    match its own check: each literal substring must appear NOWHERE in this
    file."""
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    dependency_skip_call = "importor" + "skip"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- a "
        "missing or empty scan target must FAIL, never SKIP."
    )
    assert skipif_marker not in own_text, (
        "expected no @pytest." + skipif_marker + " decorator anywhere in "
        "this module -- a missing or empty scan target must FAIL, never "
        "SKIP."
    )
    assert ("pytest." + dependency_skip_call) not in own_text, (
        "expected no pytest." + dependency_skip_call + " call anywhere in "
        "this module -- a missing dependency must FAIL, never SKIP."
    )


def test_own_needles_do_not_appear_verbatim_in_this_module():
    """Coverage 12 -- the concatenated-needle self-check: each of the four
    LOOP-02 needles built by concatenation above (Coverage 1-4) must appear
    NOWHERE verbatim in this module's own source, including inside this
    very test's failure messages. Without this leg, a future edit could
    silently un-concatenate one of the needles (for example while
    "simplifying" the code) and this gate would keep passing against
    eprom.cpp while having quietly stopped being able to fail against
    itself."""
    own_text = Path(__file__).read_text()
    for label, needle in _ALL_SELF_CHECK_NEEDLES:
        assert needle not in own_text, (
            f"the concatenation-built needle for {label} appears verbatim "
            "in this module's own source -- rebuild it from at least two "
            "literal pieces so this gate cannot match itself."
        )
