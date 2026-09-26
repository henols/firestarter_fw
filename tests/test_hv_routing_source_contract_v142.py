"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 142 Plan 06 -- authors the one test this phase owes (D-09) and the
structural half of VPP-03's evidence, as a single new pytest gate module.

Requirements: VPP-02, VPP-03

Defect class this closes: TWO distinct gaps, neither of which any existing
gate covers --

  (a) D-09 reads command_done() (src/firestarter.cpp:162-171) as the
      operation-level disable: it zeroes CONTROL_REGISTER,
      LEAST_SIGNIFICANT_BYTE and MOST_SIGNIFICANT_BYTE on BOTH the
      timeout-abort arm of loop() and the `if (finished)` arm, and because
      the top-address bits and the vpp_line bit all live in those three
      registers, that zeroing fully de-energises every route this phase
      touches. `grep -rn command_done src/ test/` today returns four
      declarations/definitions/calls, one prose mention, and four comments
      in test_loop_eprom_v131.cpp explaining why that suite deliberately
      does NOT drive it -- zero assertions. A BEHAVIOURAL oracle is
      impossible without breaking D-14: firestarter.cpp sits outside every
      native environment's build_src_filter (`+<proms/>
      +<boards/rurp_serial_utils.cpp> +<json_parser.c>
      +<operation_utils.cpp>` -- see test/native/avr/_shared/
      host_stubs_common.inc:317-335's op_reset_timeout stub and its comment
      explaining exactly this exclusion), and pulling it in would collide
      with the suite's own main() and require a SEVENTH native
      environment, which D-14 forbids. This module's command_done legs are
      therefore a SOURCE-CONTRACT claim, not a behavioural one: they prove
      the source SAYS the right thing; they do not, and cannot, prove the
      AVR EXECUTES it. Named non-claim, stated rather than left inferable:
      that command_done() actually runs on the real AVR abort path is not
      provable here, because that arm's guard depends on millis() and is
      outside every native suite's reach.

  (b) VPP-03's structural half. Plan 142-05's task 2 already proves the
      BEHAVIOURAL half (eprom_check_vpp and the write path emit the same
      physical control byte). The re-derived branch-inventory golden
      (tests/golden/protocol_branch_inventory.json, tier-1 3 -> 1) proves
      the write path's predicate SHAPE moved, but it does not prove the
      underlying MECHANISM is a single shared resolver and a single shared
      pair of composite masks rather than duplication merely relocated
      somewhere else in the same file. This module supplies exactly that
      structural proof: one resolver definition, at least two call sites,
      zero surviving equality predicates keyed on a second algorithm
      selector, exactly one definition of each composite across include/,
      and no surviving hand-rolled equivalent of either composite.

L-12, stated rather than implied: D-03's non-claim discipline (no
unqualified "fixed"/"conformant" language about silicon this phase cannot
measure) is enforced by PROSE ONLY on this branch -- CLOSE-01's
check_permitted_claims.py is Phase 146's, and Phase 139 shipped only a
Phase-139-scoped script. This module does not enforce that discipline and
must not be read as if it did; it enforces only the two claims named above.

Coverage:
  1. test_command_done_is_defined_exactly_once -- exactly one definition of
     command_done(firestarter_handle_t* handle) in the comment-stripped
     dispatcher source. Source-contract claim (see above).
  2. test_command_done_body_zeroes_all_three_latch_registers_individually
     -- command_done's own body (extracted by brace-matching, so a write
     elsewhere in the file cannot satisfy this leg) contains exactly three
     rurp_write_to_register(<reg>, 0x00) calls, and each of
     CONTROL_REGISTER, LEAST_SIGNIFICANT_BYTE and MOST_SIGNIFICANT_BYTE is
     asserted individually, not merely as a count of three.
  3. test_command_done_is_called_from_both_dispatch_arms_individually --
     at least two total command_done(&handle) call sites, with the
     timeout-abort branch of loop() and the `if (finished)` branch each
     asserted separately, so a future deletion of either arm fails this
     gate on the arm it removed, not merely on a total.
  4. test_command_done_sets_the_command_to_idle -- command_done's own body
     assigns the idle command to handle->cmd; a command_done that zeroed
     every register but left the command state live would not actually
     end the operation.
  5. test_write_execute_and_write_init_wrappers_disable_conditionally --
     VPP-03 presence leg pairing two counts in one message (so the
     wrapper's CONDITIONAL shape is what is pinned, not two unrelated
     counts): at least two comparisons gating on an error response code,
     and at least two references to the shared all-off composite, in the
     comment-stripped write path.
  6. test_route_resolver_is_defined_exactly_once -- exactly one definition
     of the EPROM high-voltage route resolver returning rurp_register_t.
  7. test_route_resolver_is_called_from_at_least_two_sites -- at least two
     call sites of that resolver, each passing the handle.
  8. test_write_execute_body_is_defined_exactly_once_as_static -- exactly
     one static definition of the write-execute inner body.
  9. test_write_init_body_is_defined_exactly_once_as_static -- exactly one
     static definition of the write-init inner body.
  10. test_each_hv_composite_is_defined_exactly_once_across_include --
      exactly one #define of each of the two EPROM high-voltage composite
      masks across every header under include/ (globbed, not seamed -- see
      Environment seams below), plus a leg that the all-off composite's
      own #define line does not name the P1-routing bit (correction C-4).
  11. test_no_second_algorithm_selector_predicate_survives_in_the_write_path
      -- zero matches for an equality comparison keyed on handle->protocol
      (the shape of the two removed hand-rolled forks) in the
      comment-stripped write path, needle concatenation-built. Named after
      what it forbids (a second algorithm selector), never after the
      token -- see the Naming note below.
  12. test_no_literal_regulator_and_drop_bit_or_sequence_survives -- zero
      matches for the regulator-enable bit and the drop-enable bit joined
      by a literal bitwise-OR, in either order, in the comment-stripped
      write path -- both needles concatenation-built.
  13. test_the_deleted_dead_regulator_guard_helper_does_not_return -- zero
      matches for Open Question 6's deleted dead regulator-enable guard
      helper's identifier, anywhere under src/ or include/, needle
      concatenation-built. Named after what the helper did, never after
      its identifier -- see the Naming note below.
  14. test_scan_targets_are_non_vacuous -- every default scan target (the
      two files AND the include/ directory) exists, is non-empty, resolves
      inside this repository, and (for the two files) has non-empty
      comment-stripped text. A missing or empty scan target must FAIL,
      never silently pass as if nothing needed checking.
  15. test_this_module_cannot_be_silently_skipped -- this module's own
      source contains no skip-bypass call, no skip-marker decorator and no
      import-or-skip call anywhere, each checked via a concatenation-built
      needle.
  16. test_own_needles_do_not_appear_verbatim_in_this_module -- every
      concatenation-built needle from Coverage 11-13 appears nowhere
      verbatim in this module's own source, so this gate cannot match
      itself.

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover them)
  - FIRESTARTER_HV_SCAN_DISPATCH_SOURCE -- overrides the scanned
    src/firestarter.cpp path ONLY. Consulted by Coverage 1-4 (the
    command_done legs). Binds at IMPORT time (the module-level
    Path(os.environ.get(...)) expression below), so a planted-violation
    run must set it in a CHILD PROCESS environment before this module is
    imported, never via a post-import monkeypatch.
  - FIRESTARTER_HV_SCAN_EPROM_SOURCE -- overrides the scanned
    src/proms/eprom.cpp path ONLY. Consulted by Coverage 5-9 and 11-12 (the
    VPP-03 structural legs against the write path). Binds at import time,
    same as above.
  - include/ has NO seam of its own: Coverage 10 globs a whole directory
    rather than scanning one file, and adding a third seam here would
    contradict this module's own fixed two-seam contract. This module's
    own SUMMARY states explicitly whether Coverage 10 was armed via some
    other mechanism or is recorded as unplanted, and why -- see that
    document, not this docstring, for that decision.
  - Coverage 13 (the deleted dead helper) reads neither seam: it sweeps
    the whole of src/ and include/ by design, not a single seamed file.
  - Coverage 14, 15 and 16 deliberately never read either seam: 14
    recomputes the default targets directly from the repository root
    without ever reading os.environ; 15 and 16 read only this module's own
    source. A stray seam value left set after a planted run cannot make
    any of Coverage 1-13 pass vacuously -- at worst it redirects that leg
    to whatever the seam happens to point at, which fails loudly rather
    than silently.

Naming note (mirrors tests/test_write_path_source_contract_v131.py's own
"Naming note for Coverage 2 and 3"): the deleted dead regulator-enable
guard helper's own identifier, spelled out in full, is a strict substring
of the "obvious" English test name for the leg proving it absent -- so a
test literally named after it would make this module's own source
contain, verbatim, a needle it must instead prove is concatenation-built
(Coverage 16's own check). Coverage 13 is therefore named after what the
helper DID rather than after its identifier; the needle it actually
searches for is still concatenation-built and still exactly the removed
identifier. The same hazard, and the same fix, apply to Coverage 11 and
12: neither test name spells out a handle->protocol equality comparison or
either ordering of the regulator/drop composite's two component bits
joined by a literal OR.

CI framing, stated honestly (mirrors the analog's own section): `pytest
tests/ -v` appears in `.github/workflows/build.yml:161` and
`.github/workflows/beta-build.yml:134`, so this module -- like every other
file under tests/ -- WILL run in CI once this branch reaches `main` or
`beta`. It does not run in any CI leg on the milestone branch itself; that
is a statement about this branch's current CI wiring, not a claim that
this module is permanently invisible to CI.

This module is a standalone pytest module: it is not named check_*.py, it
adds no shared pytest configuration or fixture-registration file anywhere
(firestarter/tests/ has none, by house convention, and this module does
not introduce one), and it imports nothing beyond the Python standard
library. It never imports, parametrizes against, or edits
tests/test_write_path_source_contract_v131.py or
tests/test_protocol_branch_inventory.py -- the scanning logic below is its
own independent re-derivation of this plan's own gate specification, and
neither analog module is modified by this one.
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_DISPATCH_REL = "src/firestarter.cpp"
_EPROM_REL = "src/proms/eprom.cpp"
_INCLUDE_REL = "include"

# Environment seams -- bind at IMPORT time. See the module docstring's
# "Environment seams" section above.
_SCAN_DISPATCH = Path(
    os.environ.get("FIRESTARTER_HV_SCAN_DISPATCH_SOURCE", str(_REPO_ROOT / _DISPATCH_REL))
)
_SCAN_EPROM = Path(
    os.environ.get("FIRESTARTER_HV_SCAN_EPROM_SOURCE", str(_REPO_ROOT / _EPROM_REL))
)
# include/ has no seam of its own -- see the docstring's "Environment
# seams" section for why.
_SCAN_INCLUDE_DIR = _REPO_ROOT / _INCLUDE_REL
_INCLUDE_EXTS = (".h", ".hpp")
_DEAD_HELPER_SCAN_DIRS = ("src", "include")
_DEAD_HELPER_SCAN_EXTS = (".c", ".cpp", ".h", ".hpp")


def _strip_comments(text):
    """Strip `//` line comments and `/* ... */` block comments, replacing
    each stripped span with whitespace of the SAME SHAPE (a newline stays a
    newline, everything else becomes a single space) so every line number
    in the result matches the original file exactly -- copied verbatim
    from tests/test_write_path_source_contract_v131.py:203-235, itself
    narrowed from test_protocol_branch_inventory.py's own comment-and-
    literal stripper. Every one of this module's scan targets carries no
    string or character literal outside of a comment or an #include
    directive (confirmed by inspection at authoring time), so
    literal-stripping is not needed here either."""
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


def _flexible_pattern_from_needle(needle):
    """Build a whitespace-tolerant regex pattern from a concatenation-built
    needle: each literal piece (split on a single literal space) is
    escaped, and the space itself becomes \\s* between pieces, so a
    planted violation's exact spacing need not match this codebase's own
    formatting style precisely. The needle argument is the already-
    concatenated full string, not the pieces -- splitting happens here,
    not at the call site, so the needle registered for the own-needles
    self-check (Coverage 16) is exactly what gets searched for."""
    parts = [p for p in needle.split(" ") if p != ""]
    return r"\s*".join(re.escape(p) for p in parts)


# Concatenation-built needles. Coverage 16 asserts none of these appear
# verbatim anywhere in this module's own source -- see the module
# docstring and the plan's own warning: a gate that quotes its forbidden
# tokens verbatim matches itself and can never pass.
_NEEDLE_PROTOCOL_EQUALITY = "handle->protocol" + " =="
_NEEDLE_HV_OR_REG_THEN_DROP = "CTRL_VPP_REGULATOR_ENABLE" + " | CTRL_VPP_VPE_DROP_ENABLE"
_NEEDLE_HV_OR_DROP_THEN_REG = "CTRL_VPP_VPE_DROP_ENABLE" + " | CTRL_VPP_REGULATOR_ENABLE"
_NEEDLE_DEAD_REGULATOR_HELPER = "eprom_internal_ensure" + "_regulator_enabled"

_ALL_SELF_CHECK_NEEDLES = (
    ("a second algorithm-selector equality predicate", _NEEDLE_PROTOCOL_EQUALITY),
    ("the hand-rolled regulator-then-drop OR sequence", _NEEDLE_HV_OR_REG_THEN_DROP),
    ("the hand-rolled drop-then-regulator OR sequence", _NEEDLE_HV_OR_DROP_THEN_REG),
    ("the deleted dead regulator-enable guard helper", _NEEDLE_DEAD_REGULATOR_HELPER),
)

_PROTOCOL_EQUALITY_RE = re.compile(_flexible_pattern_from_needle(_NEEDLE_PROTOCOL_EQUALITY))
_HV_OR_REG_THEN_DROP_RE = re.compile(_flexible_pattern_from_needle(_NEEDLE_HV_OR_REG_THEN_DROP))
_HV_OR_DROP_THEN_REG_RE = re.compile(_flexible_pattern_from_needle(_NEEDLE_HV_OR_DROP_THEN_REG))
_DEAD_REGULATOR_HELPER_RE = re.compile(
    r"\b" + re.escape(_NEEDLE_DEAD_REGULATOR_HELPER) + r"\b"
)

# command_done() (src/firestarter.cpp) -- source-contract patterns.
_COMMAND_DONE_DEF_RE = re.compile(
    r"\bvoid\s+command_done\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)
_COMMAND_DONE_CALL_RE = re.compile(r"\bcommand_done\s*\(\s*&\s*handle\s*\)")
_TIMEOUT_ABORT_ARM_RE = re.compile(
    r"timeout\s*<\s*millis\s*\(\s*\)\s*\)\s*\{[^{}]*?command_done\s*\(\s*&\s*handle\s*\)"
)
_FINISHED_ARM_RE = re.compile(
    r"if\s*\(\s*finished\s*\)\s*\{[^{}]*?command_done\s*\(\s*&\s*handle\s*\)"
)
_ANY_REG_WRITE_ZERO_RE = re.compile(
    r"\brurp_write_to_register\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*0x00\s*\)"
)
_CONTROL_REGISTER_ZERO_RE = re.compile(
    r"\brurp_write_to_register\s*\(\s*CONTROL_REGISTER\s*,\s*0x00\s*\)"
)
_LSB_ZERO_RE = re.compile(
    r"\brurp_write_to_register\s*\(\s*LEAST_SIGNIFICANT_BYTE\s*,\s*0x00\s*\)"
)
_MSB_ZERO_RE = re.compile(
    r"\brurp_write_to_register\s*\(\s*MOST_SIGNIFICANT_BYTE\s*,\s*0x00\s*\)"
)
_CMD_IDLE_ASSIGN_RE = re.compile(r"\bhandle\s*->\s*cmd\s*=(?!=)\s*CMD_IDLE\s*;")

_ROUTE_RESOLVER_DEF_RE = re.compile(
    r"\brurp_register_t\s+eprom_hv_route_mask\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)
_ROUTE_RESOLVER_CALL_RE = re.compile(r"\beprom_hv_route_mask\s*\(\s*handle\s*\)")
_WRITE_EXECUTE_BODY_DEF_RE = re.compile(
    r"\bstatic\s+void\s+eprom_internal_write_execute_body\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)
_WRITE_INIT_BODY_DEF_RE = re.compile(
    r"\bstatic\s+void\s+eprom_internal_write_init_body\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)
_RESPONSE_ERROR_COMPARE_RE = re.compile(
    r"\bhandle\s*->\s*response_code\s*==\s*RESPONSE_CODE_ERROR\b"
)
_ALL_OFF_MASK_REF_RE = re.compile(r"\bEPROM_HV_ALL_OFF_MASK\b")

# include/ composite-count patterns (globbed, not seamed).
_ROUTE_MASK_DEFINE_RE = re.compile(r"#\s*define\s+EPROM_HV_ROUTE_MASK\b")
_ALL_OFF_MASK_DEFINE_RE = re.compile(r"#\s*define\s+EPROM_HV_ALL_OFF_MASK\b")
_CTRL_VPP_P1_ENABLE_RE = re.compile(r"\bCTRL_VPP_P1_ENABLE\b")


def _iter_include_headers():
    """Every header file directly under the include/ scan target -- see
    the module docstring's "Environment seams" section for why this glob
    carries no seam of its own."""
    for p in sorted(_SCAN_INCLUDE_DIR.rglob("*")):
        if p.is_file() and p.suffix in _INCLUDE_EXTS:
            yield p


def _iter_dead_helper_scan_files():
    """Every C/C++ source or header file under src/ and include/ -- the
    exact scope Coverage 13 needs to prove Open Question 6's deletion
    cannot silently return anywhere in either tree. Neither directory is
    seamed (see the module docstring); both are recomputed fresh from
    _REPO_ROOT."""
    for top in _DEAD_HELPER_SCAN_DIRS:
        base = _REPO_ROOT / top
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if p.is_file() and p.suffix in _DEAD_HELPER_SCAN_EXTS:
                yield p


def _extract_command_done_body(stripped):
    """Locate command_done's single definition and extract its body by
    brace-depth counting on the comment-stripped text (the same discipline
    142-RESEARCH.md and 142-PATTERNS.md's D-3 both name), so a
    rurp_write_to_register(..., 0x00) call ANYWHERE ELSE in the file
    cannot satisfy Coverage 2. Returns (body, def_match); asserts exactly
    one definition exists and that the brace-matching scan actually
    closes."""
    matches = list(_COMMAND_DONE_DEF_RE.finditer(stripped))
    assert len(matches) == 1, (
        "expected exactly 1 definition of command_done(firestarter_handle_t* "
        f"handle) in the comment-stripped {_DISPATCH_REL}, found "
        f"{len(matches)} -- this is a source-contract claim (see module "
        "docstring): D-09's operation-level disable can only be pinned if "
        "there is exactly one definition to pin.\n"
        f"Got (comment-stripped {_DISPATCH_REL}):\n{stripped}"
    )
    m = matches[0]
    start = m.end()  # just after the opening brace
    depth = 1
    i = start
    n = len(stripped)
    while i < n and depth > 0:
        if stripped[i] == "{":
            depth += 1
        elif stripped[i] == "}":
            depth -= 1
        i += 1
    assert depth == 0, (
        "brace-matching scan for command_done's body reached EOF before "
        f"the opening brace at {_DISPATCH_REL}:{_line_of(stripped, m.start())} "
        "ever closed -- unbalanced braces in the scanned text."
    )
    return stripped[start : i - 1], m


def _assert_pattern_absent(pattern, label, stripped, target_rel, requirement_note):
    matches = list(pattern.finditer(stripped))
    assert matches == [], (
        f"found {len(matches)} occurrence(s) of {label} in the "
        f"comment-stripped {target_rel} -- {requirement_note}\n"
        f"Got (comment-stripped {target_rel}):\n{stripped}"
    )


# Tests -- command_done() source contract (Coverage 1-4).


def test_command_done_is_defined_exactly_once():
    """Coverage 1 -- exactly one definition of
    command_done(firestarter_handle_t* handle) in the comment-stripped
    dispatcher source. Source-contract claim: see module docstring."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    _extract_command_done_body(stripped)  # asserts exactly 1 internally


def test_command_done_body_zeroes_all_three_latch_registers_individually():
    """Coverage 2 -- D-09's own reading: CONTROL_REGISTER,
    LEAST_SIGNIFICANT_BYTE and MOST_SIGNIFICANT_BYTE together carry the
    top-address bits and the vpp_line bit, so zeroing all three fully
    de-energises every route this phase touches. The body is extracted by
    brace-matching (_extract_command_done_body) so a
    rurp_write_to_register(..., 0x00) call anywhere ELSE in
    src/firestarter.cpp cannot satisfy this leg -- confirmed by
    inspection: the only three occurrences of rurp_write_to_register in
    the whole file are these three, all inside this function's body."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_command_done_body(stripped)
    total = list(_ANY_REG_WRITE_ZERO_RE.finditer(body))
    control_hits = list(_CONTROL_REGISTER_ZERO_RE.finditer(body))
    lsb_hits = list(_LSB_ZERO_RE.finditer(body))
    msb_hits = list(_MSB_ZERO_RE.finditer(body))
    assert len(control_hits) == 1, (
        "expected exactly 1 rurp_write_to_register(CONTROL_REGISTER, 0x00) "
        f"call inside command_done's own body, found {len(control_hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    assert len(lsb_hits) == 1, (
        "expected exactly 1 rurp_write_to_register(LEAST_SIGNIFICANT_BYTE, "
        f"0x00) call inside command_done's own body, found {len(lsb_hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    assert len(msb_hits) == 1, (
        "expected exactly 1 rurp_write_to_register(MOST_SIGNIFICANT_BYTE, "
        f"0x00) call inside command_done's own body, found {len(msb_hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    assert len(total) == 3, (
        "expected exactly 3 rurp_write_to_register(<reg>, 0x00) calls "
        f"inside command_done's own body in total, found {len(total)} -- a "
        "4th would be an unnamed extra register this leg has not vetted.\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_command_done_is_called_from_both_dispatch_arms_individually():
    """Coverage 3 -- D-09's claim is that command_done() fires on BOTH the
    success and the abort path. Each arm is asserted separately, ordered
    BEFORE the total, so a future deletion of either arm fails this gate
    naming exactly which arm is gone rather than only a generic total."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    abort_arm_hits = list(_TIMEOUT_ABORT_ARM_RE.finditer(stripped))
    finished_arm_hits = list(_FINISHED_ARM_RE.finditer(stripped))
    total_hits = list(_COMMAND_DONE_CALL_RE.finditer(stripped))
    assert len(abort_arm_hits) >= 1, (
        "expected command_done(&handle) to be called from loop()'s "
        "timeout-abort branch (the 'timeout < millis()' guard) in "
        f"{_DISPATCH_REL}, found {len(abort_arm_hits)} -- this arm is half "
        "of D-09's both-paths claim.\n"
        f"Got (comment-stripped {_DISPATCH_REL}):\n{stripped}"
    )
    assert len(finished_arm_hits) >= 1, (
        "expected command_done(&handle) to be called from loop()'s "
        f"'if (finished)' branch in {_DISPATCH_REL}, found "
        f"{len(finished_arm_hits)} -- this arm is the other half of D-09's "
        "both-paths claim.\n"
        f"Got (comment-stripped {_DISPATCH_REL}):\n{stripped}"
    )
    assert len(total_hits) >= 2, (
        f"expected at least 2 total command_done(&handle) call sites in "
        f"{_DISPATCH_REL}, found {len(total_hits)}."
    )


def test_command_done_sets_the_command_to_idle():
    """Coverage 4 -- a command_done that zeroed every register but left
    the command state live would not actually end the operation."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_command_done_body(stripped)
    hits = list(_CMD_IDLE_ASSIGN_RE.finditer(body))
    assert len(hits) == 1, (
        "expected exactly 1 assignment of the idle command to handle->cmd "
        f"inside command_done's own body, found {len(hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )



def test_write_execute_and_write_init_wrappers_disable_conditionally():
    """Coverage 5 -- pins the CONDITIONAL shape of eprom_write_init's and
    eprom_write_execute's single-exit wrappers: each gates its shared
    all-off-composite disable on an error response code rather than
    disabling unconditionally (D-10 as amended, operator-confirmed
    correction C-1). Both counts are asserted TOGETHER in one message, so
    the wrapper's conditional shape is what is being pinned, not two
    unrelated counts that could each individually pass for an unrelated
    reason."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    error_gate_count = len(_RESPONSE_ERROR_COMPARE_RE.findall(stripped))
    alloff_count = len(_ALL_OFF_MASK_REF_RE.findall(stripped))
    assert error_gate_count >= 2 and alloff_count >= 2, (
        "VPP-03 requires eprom_write_init's and eprom_write_execute's "
        "single-exit wrappers to clear the shared all-off composite "
        "conditionally on an error response code: found "
        f"{error_gate_count} error-response-code comparison(s) (need >= 2) "
        f"and {alloff_count} all-off-composite reference(s) (need >= 2) in "
        f"{_EPROM_REL}.\nGot (comment-stripped {_EPROM_REL}):\n{stripped}"
    )


def test_route_resolver_is_defined_exactly_once():
    """Coverage 6 -- exactly one definition of the EPROM high-voltage
    route resolver returning rurp_register_t. VPP-03: one shared resolver,
    not one hand-rolled fork per call site."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    hits = list(_ROUTE_RESOLVER_DEF_RE.finditer(stripped))
    assert len(hits) == 1, (
        f"expected exactly 1 definition of the route resolver in "
        f"{_EPROM_REL}, found {len(hits)}.\n"
        f"Got (comment-stripped {_EPROM_REL}):\n{stripped}"
    )


def test_route_resolver_is_called_from_at_least_two_sites():
    """Coverage 7 -- the positive counterpart to Coverage 6: a resolver
    that exists but is never called would satisfy Coverage 6 vacuously.
    At least two call sites, each passing the handle -- the write path and
    eprom_check_vpp, per plan 142-04's own rewrite."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    hits = list(_ROUTE_RESOLVER_CALL_RE.finditer(stripped))
    assert len(hits) >= 2, (
        f"expected at least 2 call sites of the route resolver in "
        f"{_EPROM_REL}, found {len(hits)}.\n"
        f"Got (comment-stripped {_EPROM_REL}):\n{stripped}"
    )


def test_write_execute_body_is_defined_exactly_once_as_static():
    """Coverage 8 -- the write-execute inner body is defined exactly once,
    and as static (D-10 as amended, D-12): the single-exit wrapper's
    disable guarantee depends on there being exactly one body with no
    other external entry point."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    hits = list(_WRITE_EXECUTE_BODY_DEF_RE.finditer(stripped))
    assert len(hits) == 1, (
        "expected exactly 1 static definition of the write-execute inner "
        f"body in {_EPROM_REL}, found {len(hits)}.\n"
        f"Got (comment-stripped {_EPROM_REL}):\n{stripped}"
    )


def test_write_init_body_is_defined_exactly_once_as_static():
    """Coverage 9 -- same shape as Coverage 8, for the write-init inner
    body."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    hits = list(_WRITE_INIT_BODY_DEF_RE.finditer(stripped))
    assert len(hits) == 1, (
        "expected exactly 1 static definition of the write-init inner "
        f"body in {_EPROM_REL}, found {len(hits)}.\n"
        f"Got (comment-stripped {_EPROM_REL}):\n{stripped}"
    )


def test_each_hv_composite_is_defined_exactly_once_across_include():
    """Coverage 10 -- exactly one #define of each of the two EPROM
    high-voltage composite masks across EVERY header under include/
    (globbed fresh from _REPO_ROOT, no seam -- see module docstring). Two
    definitions of either would be the duplication VPP-03 exists to
    prevent. Also asserts the all-off composite's own #define line does
    not name the P1-routing bit: naming it would defeat
    eprom_internal_set_control_register's VPE-to-P1 remap and, on Rev
    2-class hardware, buys no additional physical guarantee anyway
    (correction C-4)."""
    headers = list(_iter_include_headers())
    assert headers, (
        "non-vacuous guard: the include/ header sweep visited zero files "
        f"under {_SCAN_INCLUDE_DIR} -- a zero-file scan must FAIL, never "
        "silently pass as nothing to check."
    )
    route_hits = []
    alloff_hits = []
    for p in headers:
        stripped = _strip_comments(p.read_text(errors="replace"))
        for m in _ROUTE_MASK_DEFINE_RE.finditer(stripped):
            route_hits.append((p, _line_of(stripped, m.start())))
        for m in _ALL_OFF_MASK_DEFINE_RE.finditer(stripped):
            line_no = _line_of(stripped, m.start())
            line_text = stripped.splitlines()[line_no - 1]
            alloff_hits.append((p, line_no, line_text))
    assert len(route_hits) == 1, (
        "expected exactly 1 '#define EPROM_HV_ROUTE_MASK' across every "
        f"header under {_INCLUDE_REL}/, found {len(route_hits)}: "
        f"{route_hits} -- a second definition would be the duplication "
        "VPP-03 exists to prevent."
    )
    assert len(alloff_hits) == 1, (
        "expected exactly 1 '#define EPROM_HV_ALL_OFF_MASK' across every "
        f"header under {_INCLUDE_REL}/, found {len(alloff_hits)}: "
        f"{[(p, ln) for p, ln, _ in alloff_hits]} -- a second definition "
        "would be the duplication VPP-03 exists to prevent."
    )
    _, alloff_line_no, alloff_line_text = alloff_hits[0]
    assert not _CTRL_VPP_P1_ENABLE_RE.search(alloff_line_text), (
        "the all-off composite's own #define line "
        f"({alloff_hits[0][0]}:{alloff_line_no}) names the P1-routing bit "
        "-- this would defeat eprom_internal_set_control_register's "
        "VPE-to-P1 remap and, on Rev 2-class hardware, buys no additional "
        f"physical guarantee anyway (correction C-4).\nLine: {alloff_line_text}"
    )


def test_no_second_algorithm_selector_predicate_survives_in_the_write_path():
    """Coverage 11 -- PROJECT.md is explicit that protocol_id stays the
    single source of truth for dispatch; no new database algorithm field
    or second firmware algorithm selector is in scope for this milestone.
    Zero matches for an equality comparison keyed on handle->protocol
    (the exact shape of the two removed hand-rolled forks) in the
    comment-stripped write path. Needle concatenation-built -- see the
    module docstring's Naming note for why this leg is not named after the
    token."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    _assert_pattern_absent(
        _PROTOCOL_EQUALITY_RE,
        "an equality comparison keyed on handle->protocol",
        stripped,
        _EPROM_REL,
        "VPP-03 / PROJECT.md forbid a second firmware algorithm selector "
        "-- route selection must go through the resolver's vpp_path "
        "lookup only, never a re-introduced protocol-keyed fork.",
    )


def test_no_literal_regulator_and_drop_bit_or_sequence_survives():
    """Coverage 12 -- every one of the four hand-rolled disables and both
    route asserts this phase's rewrite touched now go through the shared
    composite or the resolver, so a surviving literal two-bit OR combining
    the regulator-enable and drop-enable bits directly (in EITHER order)
    is either a missed conversion or a new duplication. Both needles
    concatenation-built."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    _assert_pattern_absent(
        _HV_OR_REG_THEN_DROP_RE,
        "the regulator-enable bit OR'd directly with the drop-enable bit "
        "(regulator first)",
        stripped,
        _EPROM_REL,
        "VPP-03 requires one shared set of routing masks rather than "
        "duplicated hand-rolled copies.",
    )
    _assert_pattern_absent(
        _HV_OR_DROP_THEN_REG_RE,
        "the drop-enable bit OR'd directly with the regulator-enable bit "
        "(drop first)",
        stripped,
        _EPROM_REL,
        "VPP-03 requires one shared set of routing masks rather than "
        "duplicated hand-rolled copies, regardless of which operand is "
        "written first.",
    )


def test_the_deleted_dead_regulator_guard_helper_does_not_return():
    """Coverage 13 -- Open Question 6's deletion (zero callers anywhere in
    the tree at deletion time) must not silently return as a second copy
    of the resolver's own once-per-block guard. Scans both src/ and
    include/, not just eprom.cpp. Needle concatenation-built -- see the
    module docstring's Naming note."""
    paths = list(_iter_dead_helper_scan_files())
    assert paths, (
        "non-vacuous guard: the src/+include/ sweep visited zero files -- "
        "a zero-file scan must FAIL, never silently pass as nothing to "
        "check."
    )
    hits = []
    for p in paths:
        stripped = _strip_comments(p.read_text(errors="replace"))
        for m in _DEAD_REGULATOR_HELPER_RE.finditer(stripped):
            hits.append(f"{p}:{_line_of(stripped, m.start())}")
    assert hits == [], (
        "found the deleted dead regulator-enable guard helper's "
        "identifier still present -- Open Question 6's deletion must not "
        "silently return as a second copy of the resolver's own guard.\n"
        + "\n".join(hits)
    )


# Tests -- self-protection (Coverage 14-16).


def test_scan_targets_are_non_vacuous():
    """Coverage 14 -- structural self-check, never reads either env seam:
    both default FILE scan targets and the default include/ DIRECTORY
    target are recomputed fresh from _REPO_ROOT (the
    check_permitted_claims.py _HERE-resolves-to-the-wrong-directory
    landmine, closed here by construction, the same technique
    tests/test_write_path_source_contract_v131.py's own Coverage 10
    uses). Each: exists, is non-empty, resolves inside this repository,
    and (for the two files) has non-empty comment-stripped text. A missing
    or empty scan target must FAIL, never silently pass as if nothing
    needed checking."""
    default_dispatch = _REPO_ROOT / _DISPATCH_REL
    default_eprom = _REPO_ROOT / _EPROM_REL
    for label, p in (
        ("firestarter.cpp", default_dispatch),
        ("eprom.cpp", default_eprom),
    ):
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
    assert _SCAN_INCLUDE_DIR.is_dir(), (
        f"default include/ scan target {_SCAN_INCLUDE_DIR} does not exist "
        "as a directory -- a missing scan target must FAIL, never "
        "silently pass."
    )
    assert _SCAN_INCLUDE_DIR.resolve().is_relative_to(_REPO_ROOT), (
        f"default include/ scan target {_SCAN_INCLUDE_DIR} resolves "
        f"outside _REPO_ROOT ({_REPO_ROOT})."
    )
    assert list(_iter_include_headers()), (
        f"default include/ scan target {_SCAN_INCLUDE_DIR} contains zero "
        "headers -- a zero-file scan must FAIL, never silently pass."
    )


def test_this_module_cannot_be_silently_skipped():
    """Coverage 15 -- this module's own source contains no runtime
    skip-bypass call, no skip-marker decorator, and no dependency-skip
    call anywhere, so the fail-closed contract this module documents is
    self-enforcing rather than merely stated. All three needles are built
    via concatenation, the same technique
    tests/test_write_path_source_contract_v131.py's own Coverage 11 uses,
    so this test's own source and its own failure messages cannot match
    its own check."""
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
    """Coverage 16 -- the concatenated-needle self-check: each of the four
    needles built by concatenation above (Coverage 11-13) must appear
    NOWHERE verbatim in this module's own source, including inside this
    very test's failure messages. Without this leg, a future edit could
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
