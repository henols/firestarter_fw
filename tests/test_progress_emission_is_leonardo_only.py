"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 143 Plan 08 (BF-2 closure; keeps HOST-02's firmware half honest;
protects HOST-03 on Uno-class boards) -- pins, as a SOURCE CONTRACT, that
plan 143-05's intra-block MSG_DATA_PROGRESS (0xE0) emission inside
eprom.cpp's per-byte write loop is compiled OUT -- the emission and its
millis() state variable, both -- on every Uno-class build, and compiled in
everywhere else.

Requirements: none. 143-08-PLAN.md's own frontmatter `requirements: []` is
deliberate: this plan marks no requirement Complete. It contributes the gate
that keeps HOST-02 honest and protects HOST-03 on Uno-class boards; plan
143-10 flips the HOST-* checkboxes once every plan's evidence exists.

Why a SOURCE CONTRACT, never a behavioural oracle (mirrors
tests/test_hv_routing_source_contract_v142.py's own framing; Phase 142's
command_done() gate is the precedent this module follows): the file that
implements the hazard this guard exists to prevent --
src/boards/uno_rurp_shield.cpp, which defines com_mode, the strong
rurp_log_id() override and the 4-slot deferred_log buffer -- is compiled in
NO native environment. Every native env's build_src_filter is `+<proms/>
+<boards/rurp_serial_utils.cpp> +<json_parser.c> +<operation_utils.cpp>`:
note that <boards/rurp_serial_utils.cpp> is named but <boards/
uno_rurp_shield.cpp> never is. And the native rurp_log_id capture stub
(test/native/avr/test_loop_eprom_v131/host_stubs.cpp and every sibling
suite's own copy) captures every frame UNCONDITIONALLY -- grep for
"com_mode" under test/ returns zero hits anywhere. A native test therefore
records every emitted frame regardless of build configuration and proves
NOTHING about Uno-class delivery: it cannot distinguish "delivered" from
"would have been delivered if the UART were not torn down for the whole
programmer-mode window." Only a scan of the checked-in source TEXT can pin
this guard's presence, so that is what this module does -- never state,
here or in the SUMMARY or the phase record, that any behavioural evidence
backs the Uno-class non-delivery half of this claim.

BF-2, in full (143-RESEARCH.md): on uno/uno328pb the whole per-byte write
loop runs inside one programmer-mode window (operation_utils.cpp's
_execute_operation calls rurp_set_programmer_mode(); callback(handle);
rurp_set_communication_mode();), and rurp_set_programmer_mode()
(src/boards/uno_rurp_shield.cpp) tears the UART down for the block's whole
duration via rurp_serial_end(). The Uno's strong rurp_log_id() override
(same file) DEFERS frames into a 4-slot buffer (DEFERRED_LOG_MAX 4) while
com_mode is false, and a 5th deferred frame is silently DROPPED (that
file's own sizing-rationale comment: "an operation emits at most ~1-2
critical frames per programmer-mode window"). An unguarded intra-block
progress emission would fill those four slots, so the MSG_ERR_MAX_PULSES
frame a failing block needs to send would be dropped on its fifth attempt
-- turning a program FAILURE (HOST-03's exact target: a program failure
naming the address, not a transport error) into a host SerialTimeoutError,
on a path that works today without this emission. The guard this module
pins is therefore load-bearing for HOST-03, not merely for HOST-02's own
progress bar -- and an unguarded emission would be a REGRESSION, not a
missing feature.

Second, smaller thing this module pins: plan 143-05's millis() state
variable (the emission's own last-frame timestamp) is inside the SAME guard
class as the emission itself. An unreferenced local on a build that defines
the flag is an unused-variable warning, and
scripts/baseline/size_baseline.json's AVR policy is exactly zero warnings
(avr_rule: "== 0") -- a gate that pinned only the emission would let that
regression back in silently.

D-06's non-claim, both dimensions, restated here because this module is
where the SECOND dimension is mechanically enforced (Coverage 7): intra-
block write progress is emitted on the EPROM path only (not flash, not
EEPROM, not SRAM), and delivered on leonardo only -- a STRUCTURAL
consequence of the flag this module pins, not a choice this module makes.

Coverage:
  1. test_the_progress_emit_exists_inside_the_write_execute_body -- exactly
     one MSG_DATA_PROGRESS reference inside the brace-matched
     eprom_internal_write_execute_body. A second emit would mean two
     cadences.
  2. test_the_progress_emit_is_inside_a_serial_on_io_guard -- the emit's
     offset falls inside a region whose OWN opening directive is an
     #ifndef naming the Uno-class build flag, determined by scanning every
     preprocessor conditional directive in the body IN ORDER and pairing
     each with its OWN matching #endif via depth tracking -- never by a
     nearby-substring heuristic, which would pass for an emit that merely
     follows an unrelated guard elsewhere in the same function.
  3. test_the_millis_state_variable_is_inside_the_same_guard_class -- the
     millis() state variable's OWN declaration is ALSO inside such a
     region, checked independently of Coverage 2. Message states the
     zero-warning AVR policy reason directly: a leg pinning only the emit
     would let an unused-variable regression back in.
  4. test_the_emit_precedes_the_skip_continues -- the emit's offset is LESS
     than the offset of the "expected == 0xFF" skip inside the same body,
     so the cadence is independent of how many bytes are skipped (D-03's
     placement rationale).
  5. test_the_emit_uses_the_named_interval_constant -- the emit's own
     time-since-last-frame comparison names the EPROM_PROGRESS_EMIT_INTERVAL_MS
     constant, and no bare decimal literal appears in that comparison
     instead -- a literal would let the native cadence case and the
     firmware's own predicate drift apart silently.
  6. test_the_payload_keeps_one_contract_for_the_id -- the emit's second
     argument is op_end, the resolved end of the operation's range (an
     ABSOLUTE quantity, equal to handle->mem_size whenever no region was
     supplied -- D-06); the forbidden block-relative payload alternative
     (concatenation-built -- see the Naming note below) does not appear
     inside the emit's own block. D-04/D-06: 0xE0 must keep one payload
     meaning for its one emitter -- after D-06 that meaning is "the end of
     the operation's range", not merely "the device size". (Phase 205:
     0xE0 used to have a second emitter in memory.cpp's blank-check
     machinery, which retired in that phase; this coverage item's own
     assertions were always scoped to this one emitter and are unaffected.)
  7. test_serial_on_io_is_defined_on_exactly_the_uno_class_envs -- the
     Uno-class build flag's exact compiler-invocation spelling
     (concatenation-built -- see the Naming note below) is present in
     exactly the uno and uno328pb env sections of platformio.ini, and
     absent from every other env section, leonardo and every native* env
     included -- so D-06's "delivered on leonardo only" half stays true
     against a future platformio.ini edit in EITHER direction.
  8. test_scan_targets_are_non_vacuous -- both default scan targets
     (eprom.cpp, platformio.ini), recomputed from _REPO_ROOT WITHOUT
     reading os.environ, exist, are non-empty and resolve inside this
     repository; and the extracted write-execute body (from whatever the
     seam-aware target currently resolves to) is itself non-empty and
     contains at least one #ifndef directive.
  9. test_this_module_cannot_be_silently_skipped -- concatenation-built
     skip needles.
  10. test_own_needles_do_not_appear_verbatim_in_this_module -- proves the
      concatenation discipline stays machine-checked.

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover them,
mirroring test_hv_routing_source_contract_v142.py's and
test_ack_layout_source_contract_v143.py's own convention)
  - FIRESTARTER_PROGRESS_SCAN_EPROM_SOURCE -- overrides the scanned
    src/proms/eprom.cpp path ONLY. Consulted by Coverage 1-6 and the second
    half of Coverage 8. Binds at IMPORT time (the module-level
    `Path(os.environ.get(...))` expression below), so a planted-violation
    run must set it in a CHILD PROCESS environment before this module is
    imported, never via a post-import monkeypatch.
  - FIRESTARTER_PROGRESS_SCAN_PIO_CONFIG -- overrides the scanned
    platformio.ini path ONLY. Consulted by Coverage 7. Binds at import
    time, same as above. Does not collide with plan 143-03's
    FIRESTARTER_ACK_SCAN_DISPATCH_SOURCE (a different variable name,
    scanning a different file).
  - Coverage 8's default-path half, Coverage 9 and Coverage 10 deliberately
    never read either seam: Coverage 8's first half recomputes both
    default targets directly from the repository root without ever
    reading os.environ (the check_permitted_claims.py `_HERE`-resolves-
    to-the-wrong-directory landmine, closed here by construction, the same
    technique both analog modules' own non-vacuity legs use); 9 and 10 read
    only this module's own source. A stray seam value left set after a
    planted run cannot make Coverage 8's default-path half, 9 or 10 pass
    vacuously -- at worst Coverage 1-7 and Coverage 8's second half
    redirect to whatever the seam happens to point at, which fails loudly
    rather than silently.

Naming note -- a deliberate departure from the two analogs' simpler
convention, recorded here because the reasoning is not obvious by
inspection: this module's central subject IS the Uno-class build flag, so
its BARE macro name necessarily appears throughout this docstring, this
module's own regex patterns (Coverage 2, 3 and 8's positive-detection
logic) and assertion messages -- exactly as "CONTROL_REGISTER" and
"handle->protocol" appear freely throughout test_hv_routing_source_
contract_v142.py without being registered as self-check needles, because
they are not themselves forbidden constructs. Registering the bare macro
name as a self-check needle would make Coverage 10 fail against this
module's OWN necessary, legitimate prose. What IS registered instead is the
flag's exact COMPILER-INVOCATION spelling -- the literal text pairing a
"-D" flag immediately against the macro name, exactly as it appears on its
own line in platformio.ini -- because prose describing the flag naturally
names it without that prefix immediately attached (nobody writes "the -D
SERIAL_ON_IO flag" in running English; they write "the SERIAL_ON_IO flag").
Coverage 7's own detection regex is built with `-D\\s+<name>` (a regex
escape sequence, not a literal space) rather than from the registered
needle, so it can never itself satisfy Coverage 10 by accident. The same
discipline applies to the forbidden block-relative payload alternative in
Coverage 6: no test name or docstring sentence above spells it out
verbatim -- each is named after what it forbids, never after its exact
token, so this module's own test names and prose cannot become the thing
Coverage 10 has to catch.

CI framing, stated honestly (mirrors both analogs' own section): `pytest
tests/ -v` appears in .github/workflows/build.yml:161 and
.github/workflows/beta-build.yml:134, so this module -- like every other
file under tests/ -- WILL run in CI once this branch reaches main or beta.
It does not run in any CI leg on the milestone branch itself; that is a
statement about this branch's current CI wiring, not a claim that this
module is permanently invisible to CI.

This module is a standalone pytest module: it is not named check_*.py, it
adds no shared pytest configuration or fixture-registration file anywhere
(firestarter/tests/ has none, by house convention, and this module does not
introduce one), and it imports nothing beyond the Python standard library
(os, re, pathlib). It never imports, parametrizes against, or edits
tests/test_hv_routing_source_contract_v142.py or
tests/test_ack_layout_source_contract_v143.py -- the scanning logic below
is its own independent re-derivation of this plan's own gate specification,
and neither analog module is modified by this one.
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_EPROM_REL = "src/proms/eprom.cpp"
_PIO_REL = "platformio.ini"

# Environment seams -- bind at IMPORT time. See the module docstring's
# "Environment seams" section above.
_SCAN_EPROM = Path(
    os.environ.get("FIRESTARTER_PROGRESS_SCAN_EPROM_SOURCE", str(_REPO_ROOT / _EPROM_REL))
)
_SCAN_PIO = Path(
    os.environ.get("FIRESTARTER_PROGRESS_SCAN_PIO_CONFIG", str(_REPO_ROOT / _PIO_REL))
)

# The Uno-class guard macro's BARE name -- deliberately NOT a self-check
# needle. See the module docstring's Naming note for why: it is this
# module's own subject and appears throughout its prose and its
# positive-detection regex patterns by necessity.
_GUARD_MACRO_NAME = "SERIAL_ON_IO"


def _strip_comments(text):
    """Strip `//` line comments and `/* ... */` block comments, replacing
    each stripped span with whitespace of the SAME SHAPE (a newline stays a
    newline, everything else becomes a single space) so every line number
    in the result matches the original file exactly -- copied verbatim from
    tests/test_write_path_source_contract_v131.py:203-235 via
    tests/test_hv_routing_source_contract_v142.py's and
    tests/test_ack_layout_source_contract_v143.py's own copies. The scanned
    function body carries no string or character literal outside of a
    comment or a preprocessor directive (confirmed by inspection at
    authoring time), so literal-stripping is not needed here either."""
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


# Concatenation-built needles. Coverage 10 asserts none of these appear
# verbatim anywhere in this module's own source -- see the module docstring
# and its Naming note: a gate that quotes its forbidden tokens verbatim
# matches itself and can never pass.
_NEEDLE_SERIAL_ON_IO_DEFINE = "-D" + " SERIAL_ON_IO"
_NEEDLE_DATA_SIZE_PAYLOAD = "handle" + "->data_size"
_NEEDLE_SKIP_CALL = "pytest" + ".skip"
_NEEDLE_SKIPIF_MARKER = "mark" + ".skipif"
_NEEDLE_DEPENDENCY_SKIP_CALL = "importor" + "skip"

_ALL_SELF_CHECK_NEEDLES = (
    (
        "the Uno-class build flag's exact compiler-invocation spelling",
        _NEEDLE_SERIAL_ON_IO_DEFINE,
    ),
    ("the forbidden block-relative payload alternative", _NEEDLE_DATA_SIZE_PAYLOAD),
    ("a pytest skip call", _NEEDLE_SKIP_CALL),
    ("a pytest skipif marker", _NEEDLE_SKIPIF_MARKER),
    ("a pytest dependency-skip call", _NEEDLE_DEPENDENCY_SKIP_CALL),
)

# eprom_internal_write_execute_body() extraction (same discipline as both
# analogs' own brace-matched body extractors).
_WRITE_EXECUTE_BODY_DEF_RE = re.compile(
    r"\bstatic\s+void\s+eprom_internal_write_execute_body\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)

_PROGRESS_EMIT_RE = re.compile(r"\bMSG_DATA_PROGRESS\b")
_SKIP_0XFF_RE = re.compile(r"\bexpected\s*==\s*0x[fF][fF]\b")
_STATE_VAR_DECL_RE = re.compile(r"\buint32_t\s+last_emit_ms\s*=\s*millis\s*\(\s*\)\s*;")
# LOCATOR WIDENED (debug session w27c512-write-slow-3x): the leading
# (?P<pre>...) group tolerates additional conjuncts BEFORE the
# time-since-last-frame comparison inside the same `if`. It was added because
# the pass-batched program loop gates the emit on `pulses == 0` as well as on
# the interval, and the previous form required the `if (` to open DIRECTLY
# with the (uint32_t)(millis() - last_emit_ms) term -- so it matched 0 blocks
# and took Coverage 5 and 6 down with it.
#
# WHAT DID NOT CHANGE, and this is the point: every assertion downstream of
# this regex is untouched in both wording and intent. `interval`, `arg1` and
# `arg2` still capture exactly the same sub-expressions, and Coverage 5 and 6
# still demand EPROM_PROGRESS_EMIT_INTERVAL_MS and handle->mem_size verbatim.
# `pre` is deliberately `[^{]*?` -- lazy, and unable to cross into the block
# body -- so widening the locator cannot let the emit's own CONTENTS drift.
# The new gate itself is pinned separately by
# test_the_emit_is_gated_to_the_first_pass below, so the added conjunct is
# recorded as a contract rather than merely tolerated here.
_EMIT_BLOCK_RE = re.compile(
    r"if\s*\((?P<pre>[^{]*?)\(uint32_t\)\s*\(\s*millis\s*\(\s*\)\s*-\s*last_emit_ms\s*\)\s*>=\s*"
    r"(?P<interval>[^()\s]+)\s*\)\s*\{\s*"
    r"last_emit_ms\s*=\s*millis\s*\(\s*\)\s*;\s*"
    r"LOG_DATA_ID_U32_U32\s*\(\s*MSG_DATA_PROGRESS\s*,\s*(?P<arg1>[^,()]+?)\s*,\s*(?P<arg2>[^,()]+?)\s*\)\s*;\s*\}"
)
# The pass gate the pass-batched loop added. `pulses` is the loop's pass
# counter, so `pulses == 0` is "first scan pass only".
_FIRST_PASS_GATE_RE = re.compile(r"\bpulses\s*==\s*0\s*&&")
_BARE_1000_RE = re.compile(r"(?<![A-Za-z0-9_])1000(?![A-Za-z0-9_])")

# Preprocessor conditional directives, matched line-by-line on the
# comment-stripped body. Group 2 (the macro name) is only meaningful for
# ifndef/ifdef; else/elif/endif carry no identifier this module needs.
_PP_DIRECTIVE_RE = re.compile(
    r"^[ \t]*#[ \t]*(ifndef|ifdef|elif|else|endif)\b[ \t]*([A-Za-z_][A-Za-z0-9_]*)?",
    re.MULTILINE,
)

_ANY_SECTION_HEADER_RE = re.compile(r"^\[([^\]]*)\][ \t]*$", re.MULTILINE)
# Regex-escape-based detection (NOT the registered needle -- see the
# module docstring's Naming note): '-D\s+SERIAL_ON_IO' as a PATTERN never
# contains the registered needle's literal single space, so this line
# cannot itself satisfy Coverage 10.
_SERIAL_ON_IO_DEFINE_DETECT_RE = re.compile(r"-D\s+" + _GUARD_MACRO_NAME + r"\b")


def _extract_write_execute_body(stripped):
    """Locate eprom_internal_write_execute_body's single definition and
    extract its body by brace-depth counting on the comment-stripped text
    (the same discipline both analogs' own extractors use), so a match
    ANYWHERE ELSE in the file cannot satisfy Coverage 1-6. Returns (body,
    def_match); asserts exactly one definition exists and that the
    brace-matching scan actually closes."""
    matches = list(_WRITE_EXECUTE_BODY_DEF_RE.finditer(stripped))
    assert len(matches) == 1, (
        "expected exactly 1 definition of static void "
        "eprom_internal_write_execute_body(firestarter_handle_t* handle) "
        f"in the comment-stripped {_EPROM_REL}, found {len(matches)} -- "
        "this is a source-contract claim (see module docstring): BF-2's "
        "guard can only be pinned if there is exactly one function body to "
        "pin.\n"
        f"Got (comment-stripped {_EPROM_REL}):\n{stripped}"
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
        "brace-matching scan for eprom_internal_write_execute_body's body "
        f"reached EOF before the opening brace at {_EPROM_REL}:"
        f"{_line_of(stripped, m.start())} ever closed -- unbalanced braces "
        "in the scanned text."
    )
    return stripped[start : i - 1], m


def _find_ifndef_regions(body):
    """Scan `body` for #ifdef/#ifndef ... #endif regions, pairing each
    directive with its OWN matching #endif via a depth-tracked stack --
    NEVER a nearby-substring heuristic, which would pass for an emit that
    merely follows an unrelated guard elsewhere in the function. Returns a
    list of (macro_or_None, kind, true_start, true_end) tuples describing
    each frame's OWN true-branch extent: from just after the directive's
    own line to its own #else/#elif (if present) or, absent one, to its own
    #endif. A #endif always closes the innermost still-open frame, so a
    #endif belonging to a nested block can never be mistaken for the
    #endif of an outer, earlier-opened frame -- and an #ifndef whose own
    #endif has already been consumed can never be mistaken for a later,
    textually-nearby #ifndef of the same macro name."""
    stack = []
    regions = []
    for m in _PP_DIRECTIVE_RE.finditer(body):
        kind = m.group(1)
        macro = m.group(2)
        line_end = body.find("\n", m.end())
        after_directive = (line_end + 1) if line_end != -1 else len(body)
        if kind in ("ifndef", "ifdef"):
            stack.append(
                {"kind": kind, "macro": macro, "true_start": after_directive, "true_end": None}
            )
        elif kind in ("else", "elif"):
            if stack and stack[-1]["true_end"] is None:
                stack[-1]["true_end"] = m.start()
        elif kind == "endif":
            if stack:
                frame = stack.pop()
                true_end = frame["true_end"] if frame["true_end"] is not None else m.start()
                regions.append((frame["macro"], frame["kind"], frame["true_start"], true_end))
    return regions


def _offset_inside_ifndef_guard(regions, offset, macro):
    return any(
        kind == "ifndef" and mname == macro and start <= offset < end
        for mname, kind, start, end in regions
    )


def _iter_env_sections(pio_text):
    """Split platformio.ini's raw text into named [env:NAME] sections, each
    paired with the text from just after its own header to the start of
    the NEXT section header of ANY kind (so the shared [env] defaults
    block and [platformio] never leak into a named env's own text, and a
    flag added only to the shared [env] block -- which every env inherits
    via ${env.build_flags} -- is correctly NOT attributed to any single
    named env by this function; Coverage 7 checks each env's OWN literal
    text, matching the two plant directions D-25 actually names). Returns a
    dict of env name -> section text."""
    headers = list(_ANY_SECTION_HEADER_RE.finditer(pio_text))
    sections = {}
    for i, header_match in enumerate(headers):
        name = header_match.group(1)
        start = header_match.end()
        end = headers[i + 1].start() if i + 1 < len(headers) else len(pio_text)
        if name.startswith("env:"):
            sections[name[len("env:") :]] = pio_text[start:end]
    return sections


# Tests -- write-execute-body source contract (Coverage 1-7).


def test_the_progress_emit_exists_inside_the_write_execute_body():
    """Coverage 1 -- exactly one MSG_DATA_PROGRESS reference inside the
    brace-matched eprom_internal_write_execute_body. A second emit would
    mean two cadences, defeating D-03's single time-bounded-cadence
    decision."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    body, _ = _extract_write_execute_body(stripped)
    hits = list(_PROGRESS_EMIT_RE.finditer(body))
    assert len(hits) == 1, (
        "expected exactly 1 MSG_DATA_PROGRESS reference inside "
        f"eprom_internal_write_execute_body's body, found {len(hits)} -- a "
        "second emit would mean two cadences (D-03).\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_the_progress_emit_is_inside_a_serial_on_io_guard():
    """Coverage 2 -- BF-2/HOST-03: the emit's offset must fall inside a
    region whose OWN opening directive is an #ifndef naming the Uno-class
    build flag, determined by scanning every preprocessor conditional
    directive in the body IN ORDER and pairing each with its OWN matching
    #endif via depth tracking -- never by a nearby-substring heuristic,
    which would pass for an emit that merely follows an unrelated guard
    elsewhere in the function. An unguarded emission on uno/uno328pb would
    fill the 4-slot deferred_log buffer (src/boards/uno_rurp_shield.cpp)
    and starve a subsequent MSG_ERR_MAX_PULSES frame of its slot, turning a
    program FAILURE into a host transport timeout on a path that works
    today (HOST-03's exact anti-goal)."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    body, _ = _extract_write_execute_body(stripped)
    emit_hits = list(_PROGRESS_EMIT_RE.finditer(body))
    assert len(emit_hits) >= 1, (
        "expected at least 1 MSG_DATA_PROGRESS reference inside "
        "eprom_internal_write_execute_body's body to check for guard "
        f"membership, found {len(emit_hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    regions = _find_ifndef_regions(body)
    offset = emit_hits[0].start()
    assert _offset_inside_ifndef_guard(regions, offset, _GUARD_MACRO_NAME), (
        "the MSG_DATA_PROGRESS emit's offset does not fall inside any "
        f"'#ifndef {_GUARD_MACRO_NAME}' ... '#endif' region of "
        "eprom_internal_write_execute_body's body (determined by "
        "preprocessor-directive depth tracking, not a nearby-substring "
        "heuristic) -- BF-2/HOST-03: an unguarded emission on uno/uno328pb "
        "would fill the 4-slot deferred_log buffer and starve a "
        "subsequent MSG_ERR_MAX_PULSES frame of its slot, turning a "
        "program failure into a host transport timeout on a path that "
        "works today.\n"
        f"Regions found (macro, kind, true_start, true_end): {regions}\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_the_millis_state_variable_is_inside_the_same_guard_class():
    """Coverage 3 -- D-22's reason, stated directly: an unreferenced local
    on a build that defines the Uno-class flag is an unused-variable
    warning, and the AVR warning policy
    (scripts/baseline/size_baseline.json, avr_rule "== 0") is exactly zero
    -- a leg pinning only the emit (Coverage 2) would let that regression
    back in silently. The millis() state variable's OWN declaration must
    fall inside an '#ifndef <flag>' ... '#endif' region too, checked
    independently of Coverage 2 so the two guards cannot silently diverge."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    body, _ = _extract_write_execute_body(stripped)
    hits = list(_STATE_VAR_DECL_RE.finditer(body))
    assert len(hits) == 1, (
        "expected exactly 1 'uint32_t last_emit_ms = millis();' "
        "declaration inside eprom_internal_write_execute_body's body, "
        f"found {len(hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    regions = _find_ifndef_regions(body)
    offset = hits[0].start()
    assert _offset_inside_ifndef_guard(regions, offset, _GUARD_MACRO_NAME), (
        "the millis() state variable's declaration does not fall inside "
        f"any '#ifndef {_GUARD_MACRO_NAME}' ... '#endif' region of "
        "eprom_internal_write_execute_body's body -- D-22: an unreferenced "
        "local on a build that defines the flag is an unused-variable "
        "warning, and the AVR warning policy is exactly zero; a leg "
        "pinning only the emit would let that regression back in.\n"
        f"Regions found (macro, kind, true_start, true_end): {regions}\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_the_emit_precedes_the_skip_continues():
    """Coverage 4 -- D-03's placement rationale: the emit's offset must be
    LESS than the offset of the 'expected == 0xFF' skip inside the same
    body, so the cadence is independent of how many bytes are skipped --
    the more honest reading of "progress" than gating on bytes actually
    pulsed, and empirically confirmed load-bearing by plan 143-05's own
    D-25 plant 4 (relocating the emit after the skips measurably changed
    the achievable frame count)."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    body, _ = _extract_write_execute_body(stripped)
    emit_hits = list(_PROGRESS_EMIT_RE.finditer(body))
    skip_hits = list(_SKIP_0XFF_RE.finditer(body))
    assert len(emit_hits) == 1, (
        "expected exactly 1 MSG_DATA_PROGRESS reference inside "
        f"eprom_internal_write_execute_body's body, found {len(emit_hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    assert len(skip_hits) == 1, (
        "expected exactly 1 'expected == 0xFF' skip inside "
        f"eprom_internal_write_execute_body's body, found {len(skip_hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    assert emit_hits[0].start() < skip_hits[0].start(), (
        "expected the MSG_DATA_PROGRESS emit's offset "
        f"({emit_hits[0].start()}) to be LESS than the 'expected == 0xFF' "
        f"skip's offset ({skip_hits[0].start()}) inside "
        "eprom_internal_write_execute_body's body -- D-03: the cadence "
        "must be independent of how many bytes are skipped.\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_the_emit_uses_the_named_interval_constant():
    """Coverage 5 -- the emit's own time-since-last-frame comparison must
    name the EPROM_PROGRESS_EMIT_INTERVAL_MS constant (include/eprom.h),
    never a bare decimal literal -- a literal would let the native cadence
    case (test_loop_eprom_v131.cpp, which references the constant by name)
    and the firmware's own predicate drift apart silently."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    body, _ = _extract_write_execute_body(stripped)
    hits = list(_EMIT_BLOCK_RE.finditer(body))
    assert len(hits) == 1, (
        "expected exactly 1 time-gated MSG_DATA_PROGRESS emit block inside "
        f"eprom_internal_write_execute_body's body, found {len(hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    interval = hits[0].group("interval")
    assert interval == "EPROM_PROGRESS_EMIT_INTERVAL_MS", (
        "expected the emit's own time-since-last-frame comparison to use "
        f"the named EPROM_PROGRESS_EMIT_INTERVAL_MS constant, found "
        f"{interval!r} instead -- a literal would let the native cadence "
        "case and the firmware's own predicate drift apart silently.\n"
        f"Body (comment-stripped):\n{body}"
    )
    assert _BARE_1000_RE.search(interval) is None, (
        "found a bare decimal literal in the emit's own predicate "
        f"({interval!r}) instead of the named "
        "EPROM_PROGRESS_EMIT_INTERVAL_MS constant.\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_the_payload_keeps_one_contract_for_the_id():
    """Coverage 6 -- D-04/D-06: 0xE0 must keep one payload meaning for its
    one emitter (Phase 205 retired the second emitter this coverage item
    used to guard against drifting apart from -- memory.cpp's blank-check
    machinery). After D-06, that meaning is "the end
    of the operation's range" -- op_end, resolved once per write-execute
    call from mem_util_operation_end(handle), equal to the device size for
    a whole-device operation and to the region end for a bounded one. This
    is still one meaning, not two: op_end simply equals handle->mem_size
    whenever no region was supplied, which is exactly what the emit sent
    before this plan. The emit's second argument must be that bare
    identifier, op_end, never an inline expression (D-06's fallback is
    resolved once, before this block, precisely so the argument here stays
    a plain identifier the (?P<arg2>[^,()]+?) capture group can match); the
    forbidden block-relative payload alternative (concatenation-built --
    see the module docstring's Naming note) must not appear inside the
    emit's OWN block. The check is scoped to the matched block itself, not
    the whole function body, because the forbidden alternative's field name
    legitimately appears elsewhere in this same function as the per-byte
    loop's own upper bound (the for-loop condition just above this
    function's own final verify pass) -- an unscoped check would
    false-positive against the real, correct source."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    body, _ = _extract_write_execute_body(stripped)
    hits = list(_EMIT_BLOCK_RE.finditer(body))
    assert len(hits) == 1, (
        "expected exactly 1 time-gated MSG_DATA_PROGRESS emit block inside "
        f"eprom_internal_write_execute_body's body, found {len(hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    arg2 = re.sub(r"\s+", "", hits[0].group("arg2"))
    assert arg2 == "op_end", (
        "expected the emit's second argument to be op_end (the resolved "
        f"end of the operation's range), found {hits[0].group('arg2')!r} -- "
        "D-06: after this plan, 0xE0's denominator means the end of the "
        "operation's range, which is the device size for a whole-device "
        "operation and the region end for a bounded one -- still one "
        "meaning, expressed by a value that equals the device size "
        "whenever no region was supplied. A block-relative pair would give "
        "the id a second meaning depending on which operation emitted it; "
        "that argument is not overturned by this change, only restated.\n"
        f"Body (comment-stripped):\n{body}"
    )
    emit_block_text = hits[0].group(0)
    forbidden_hits = list(re.finditer(re.escape(_NEEDLE_DATA_SIZE_PAYLOAD), emit_block_text))
    assert forbidden_hits == [], (
        "found the forbidden block-relative payload alternative inside "
        "the emit's OWN block (not merely elsewhere in the function, "
        "where it legitimately appears as the per-byte loop's own bound) "
        "-- D-06: 0xE0 must keep exactly one payload meaning for its one "
        "emitter.\n"
        f"Emit block (comment-stripped):\n{emit_block_text}"
    )


def test_serial_on_io_is_defined_on_exactly_the_uno_class_envs():
    """Coverage 7 -- D-06's second non-claim dimension, mechanically
    enforced: the Uno-class build flag's exact compiler-invocation spelling
    (concatenation-built -- see the module docstring's Naming note) must
    appear in exactly the uno and uno328pb env sections of platformio.ini,
    and in NO other env section -- leonardo and every native* env included.
    Both plant directions (added to leonardo; removed from uno328pb) are
    caught by this single set-equality assertion, because D-06's claim
    ("delivered on leonardo only") is falsified by either direction."""
    pio_text = _SCAN_PIO.read_text()
    envs = _iter_env_sections(pio_text)
    assert {"uno", "uno328pb", "leonardo", "native"} <= set(envs), (
        "expected at least the uno, uno328pb, leonardo and native env "
        f"sections to be discoverable in {_PIO_REL}, found only "
        f"{sorted(envs)} -- a section parser that silently discovers too "
        "few sections could make this leg's absence half pass vacuously "
        "for envs it never actually inspected."
    )
    flagged = {name for name, text in envs.items() if _SERIAL_ON_IO_DEFINE_DETECT_RE.search(text)}
    assert flagged == {"uno", "uno328pb"}, (
        "expected the Uno-class build flag on exactly the uno and "
        f"uno328pb env sections of {_PIO_REL} and on no other env "
        f"(leonardo and every native* env included), found it on: "
        f"{sorted(flagged)} -- D-06's non-claim (\"delivered on leonardo "
        "only\") is false the moment an env gains or loses this flag; "
        "this leg is what keeps that non-claim honest against a future "
        f"{_PIO_REL} edit in EITHER direction."
    )


# Tests -- self-protection (Coverage 8-10).


def test_the_emit_is_gated_to_the_first_pass():
    """Coverage 6b -- debug session w27c512-write-slow-3x. Pins a REAL
    behavioural change to the emission contract, so it is recorded rather
    than silently tolerated by the widened locator above.

    WHAT CHANGED. The loop this module scans is no longer per-byte; it is
    pass-batched (a scan pass alternating with a pulse pass), and the scan
    pass restarts at index 0 on every pass. MSG_DATA_PROGRESS (0xE0) carries
    an ABSOLUTE chip address, and the host applies it verbatim -- it assigns
    the bar's position rather than advancing it
    (firestarter_app/firestarter/eprom_operations.py::_apply_write_progress
    sets progress.pbar.n = position). So a frame emitted from a LATER pass
    would carry a LOWER address than one already sent and the host's write
    bar would visibly rewind. Gating the emit on the first pass is what makes
    0xE0's address sequence monotonic within a block, which is the property
    the host relies on.

    THE CONSEQUENCE, stated because it is a real behaviour change and not a
    no-op: the first scan pass of a 1024-byte block completes in well under
    the 1000 ms interval, so on an ordinary write this emit now fires ZERO
    times per block and the host falls back to its per-chunk handoff bar --
    the same path every uno/uno328pb write already takes, which the host
    handles by design (its firmware_drives_bar latch simply never engages).
    That is not a user-visible regression: a block now completes in ~0.44 s
    where the per-byte loop took ~1.57 s, so per-chunk granularity is FINER
    in wall-clock terms than the 1 s intra-block cadence it replaces. The
    emit is kept, not deleted, because a slow row (a multi-pass block, or a
    0x0B part at 500 us pulses) can still exceed the interval inside the
    first pass.

    This leg exists so that removing the gate -- which would restore the
    rewinding bar -- fails here, and so that a future reader learns the
    reason from a test rather than from a git archaeology session."""
    stripped = _strip_comments(_SCAN_EPROM.read_text())
    body, _ = _extract_write_execute_body(stripped)
    hits = list(_EMIT_BLOCK_RE.finditer(body))
    assert len(hits) == 1, (
        "expected exactly 1 time-gated MSG_DATA_PROGRESS emit block inside "
        f"eprom_internal_write_execute_body's body, found {len(hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    pre = hits[0].group("pre")
    assert _FIRST_PASS_GATE_RE.search(pre) is not None, (
        "expected the emit's own predicate to be gated on the loop's first "
        f"pass as well as on the interval, found predicate prefix {pre!r} -- "
        "without that gate a later pass emits a LOWER absolute address than "
        "one already sent, and the host's write bar (which assigns "
        "pbar.n from the frame verbatim) rewinds.\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_scan_targets_are_non_vacuous():
    """Coverage 8 -- structural self-check, two halves.

    Part (a): both DEFAULT scan targets are recomputed fresh from
    _REPO_ROOT WITHOUT reading os.environ (the check_permitted_claims.py
    `_HERE`-resolves-to-the-wrong-directory landmine, closed here by
    construction): each exists, is non-empty, and resolves inside this
    repository; eprom.cpp additionally has non-empty comment-stripped
    text.

    Part (b): the body-extractor, run against whatever `_SCAN_EPROM`
    currently resolves to (the SAME seam-aware target every leg above
    scans), must itself yield a body that is non-empty AND contains at
    least one '#ifndef' directive -- this is the half a planted EMPTY
    scratch file (which instead trips the shared extractor's own "exactly
    1 definition" assertion, taking Coverage 1-6 down with it as an
    honest, stronger-than-required spillover) or a signature-with-empty-
    body scratch file (which reaches this leg's OWN non-empty-body
    assertion directly) turns RED."""
    default_eprom = _REPO_ROOT / _EPROM_REL
    default_pio = _REPO_ROOT / _PIO_REL
    for label, p in (("eprom.cpp", default_eprom), ("platformio.ini", default_pio)):
        assert p.is_file(), (
            f"default {label} scan target {p} does not exist on disk -- a "
            "missing scan target must FAIL, never silently pass."
        )
        assert p.stat().st_size > 0, f"default {label} scan target {p} is empty"
        assert p.resolve().is_relative_to(_REPO_ROOT), (
            f"default {label} scan target {p} resolves outside "
            f"_REPO_ROOT ({_REPO_ROOT}) -- a naive future copy of this "
            "module into another directory must fail loudly here, not "
            "scan nothing and exit 0."
        )
    default_stripped = _strip_comments(default_eprom.read_text())
    assert default_stripped.strip() != "", (
        f"comment-stripped {_EPROM_REL} (default target) is empty -- "
        "nothing would ever be scanned by this module's other legs."
    )

    stripped = _strip_comments(_SCAN_EPROM.read_text())
    body, _ = _extract_write_execute_body(stripped)
    assert body.strip() != "", (
        "the extracted eprom_internal_write_execute_body (from the "
        f"CURRENT scan target {_SCAN_EPROM}) is empty -- a brace-matcher "
        "that silently returns an empty body would make every "
        "positive-presence leg in this module (Coverage 1-6) fail to find "
        "anything to check."
    )
    assert "#ifndef" in body, (
        "the extracted eprom_internal_write_execute_body (from the "
        f"CURRENT scan target {_SCAN_EPROM}) contains no '#ifndef' "
        "directive -- Coverage 2/3's guard-membership legs depend on this "
        "body actually containing the guard directives they scan for; a "
        "technically non-empty but guard-free extraction would be "
        "indistinguishable from a genuinely broken brace-matcher for "
        "those two legs specifically."
    )


def test_this_module_cannot_be_silently_skipped():
    """Coverage 9 -- this module's own source contains no runtime
    skip-bypass call, no skip-marker decorator, and no dependency-skip call
    anywhere, so the fail-closed contract this module documents is
    self-enforcing rather than merely stated. All three needles are built
    via concatenation, the same technique both analog modules' own
    equivalent legs use, so this test's own source and its own failure
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
    """Coverage 10 -- the concatenated-needle self-check: each of the five
    needles built by concatenation above (Coverage 6, 7 and 9) must appear
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
