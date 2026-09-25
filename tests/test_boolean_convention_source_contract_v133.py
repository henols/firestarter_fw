"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 156 Plan 06 -- pins the boolean-convention flip Plan 05 landed in
src/operation_utils.cpp and src/eprom_operations.cpp with a committed,
non-vacuous source-scan gate.

Requirements: DEDUP-04

Defect class this closes: src/eprom_operations.cpp -- the seven eprom_*
command wrappers whose leading `!` Plan 05 removed -- compiles in NO
native environment. [env:native] and [env:native_nodevtools] both share
a build_src_filter that names src/proms/, two board-support translation
units, json_parser.c and src/operation_utils.cpp explicitly, and excludes
every other top-level src/*.cpp file by omission -- eprom_operations.cpp
among them. src/operation_utils.cpp (the six flipped engine return sites)
IS in that filter and is covered by Cases 24 and 25 of
test_eeprom28c_sdp.cpp; the seven wrapper call sites have no native and no
bench coverage at all -- only this source contract and Plan 05's
size-identity build. This module is a SOURCE CONTRACT: it proves the
shipped text says what Plan 05 claims, never what the compiled code does
at runtime.

Census note (Phase 204): Plan 05 landed with nine wrappers. Phase 204
Plan 01 retired eprom_verify (the verify command's own ordinal), taking
the census to eight; Phase 204 Plan 03 retires eprom_blank_check (the
blank-check command's own ordinal) in the same phase, taking it to
seven -- the current, live count.

Coverage:
  1. test_wrapper_negated_return_is_absent -- zero occurrences of the
     concatenation-built negated-call needle (_NEEDLE_INVERTED_CALL) in
     the comment-and-literal-stripped wrapper file. Checked as a plain
     substring count, not a word-boundary identifier match: the needle is
     a PREFIX of two larger identifiers, never a standalone word, so a
     \\b-wrapped search would never fire and would pass vacuously
     forever -- see _assert_identifier_absent's own docstring for why
     that helper is deliberately NOT used for this leg.
  2. test_engine_retains_exactly_one_negated_call -- the engine file
     contains EXACTLY ONE surviving negated call inside a return
     statement, and that call is the MAIN-phase callback delegation --
     Plan 05's honest nine-to-one reduction, not an elimination, because
     the callback argument (_process_incoming_data / _process_outgoing_data
     in eprom_operations.cpp) keeps its own opposite convention. A count
     of zero here would mean the surviving negation was wrongly removed;
     a count above one would mean a NEW negation crept in.
  3. test_the_seven_forwarding_calls_are_present -- the positive
     counterpart to Coverage 1: op_execute_stateful_operation and
     op_execute_simple_operation, each reached via a bare `return`,
     together occur exactly _EXPECTED_FORWARD_TOTAL (7) times in the
     comment-and-literal-stripped wrapper file. A deleted, emptied or
     truncated wrapper file satisfies Coverage 1 vacuously; this leg is
     what catches that.
  4. test_the_two_early_return_refusal_literals_survive -- eprom_erase's
     FLAG_CAN_ERASE refusal and eprom_check_chip_id's zero-chip-id
     refusal each still return the literal `true` exactly once, inside
     their own function body -- both are correct under both conventions,
     and a careless mechanical substitution over the whole file is the
     one mistake most likely to flip either of them by accident.
     Asserted on the enclosing function name and the returned literal,
     never on a line number -- Phases 157 and 158 move this file again.
  5. test_scan_targets_are_non_vacuous -- both default scan targets
     exist, are non-empty, resolve inside this repository, and their
     comment-and-literal-stripped text is non-empty.
  6. test_this_module_cannot_be_silently_skipped -- this module's own
     source contains no skip-bypass call, no skip-marker decorator and no
     dependency-skip call anywhere.
  7. test_own_needles_do_not_appear_verbatim_in_this_module -- the
     concatenation-built needles above appear nowhere verbatim in this
     module's own source, so this gate cannot match itself.

Literal-stripping decision (re-confirmed, not copied from the sibling).
test_write_path_source_contract_v131.py asserts its own two scan targets,
src/proms/eprom.cpp and src/proms/memory.cpp, contain no string or
character literal outside a comment or an #include, and treats that
finding as the reason a comment-only stripper suffices there. That
finding does NOT carry over to this module's own two targets unchecked.
src/eprom_operations.cpp was confirmed clean this session (its only
quoted text sits inside `//` comments and `#include` lines).
src/operation_utils.cpp is NOT clean: op_get_message() (its serial
message-framing parser) contains four character literals outside any
comment ('O', 'K', 'D', '#') and one string literal outside any comment
(the "DONE" text passed to PSTR()). None of those five literals contains
any substring this module searches for, so none of them could produce a
false positive under a comment-only stripper on their own -- but rather
than argue that case by case, this module instead borrows
test_protocol_branch_inventory.py's own comment-and-literal stripper
(comments AND the contents of every quoted literal are both replaced with
same-shape whitespace) and applies it uniformly to BOTH scan targets, so
the literal-safety property holds by construction rather than by one-off
inspection.

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover them)
  - FIRESTARTER_BOOLEAN_CONVENTION_SCAN_SOURCE -- overrides the scanned
    src/eprom_operations.cpp path ONLY. src/operation_utils.cpp has no
    override, following the sibling gate's own precedent (its
    src/proms/memory.cpp has none either) -- the engine file is a fixed
    site named by Plan 05's own record. Binds at IMPORT time (the
    module-level Path(os.environ.get(...)) expression below), so a
    planted-violation run that exercises this seam must set it in a CHILD
    PROCESS environment before this module is imported, never via a
    post-import monkeypatch. Coverage 2, 5 (the engine half), 6 and 7
    deliberately never read it: Coverage 2 always reads the fixed engine
    path; Coverage 5 recomputes the default targets directly from the
    repository root without ever reading os.environ; Coverage 6 and 7
    read only this module's own source. A stray seam value left set after
    a planted run cannot make any of these four pass vacuously -- at
    worst it makes Coverage 1, 3 or 4 fail loudly (a FileNotFoundError
    surfaces from Path.read_text() when the overridden path does not
    exist).

CI framing, stated honestly: `pytest tests/ -v` appears in
.github/workflows/build.yml:161 and .github/workflows/beta-build.yml:134,
so this module -- like every other file under tests/ -- WILL run in CI
once this branch reaches `main` or `beta`. Neither workflow triggers on
this milestone branch itself, so today this module lands in a CI leg
without being exercised by CI on this branch.

This module is a standalone pytest module: it is not named check_*.py
(that glob belongs to a different, paired convention this module does not
need), it adds no shared pytest configuration or fixture-registration
file, and it imports nothing beyond the Python standard library. It never
imports, parametrizes against, or edits any pre-existing gate module; the
scanning logic below is its own independent re-derivation of this plan's
own gate specification.
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_WRAPPERS_REL = "src/eprom_operations.cpp"
_ENGINE_REL = "src/operation_utils.cpp"

# Environment seam -- binds at IMPORT time. See the module docstring's
# "Environment seams" section above.
_SCAN_WRAPPERS = Path(
    os.environ.get(
        "FIRESTARTER_BOOLEAN_CONVENTION_SCAN_SOURCE", str(_REPO_ROOT / _WRAPPERS_REL)
    )
)
# src/operation_utils.cpp has no override -- see the "Environment seams"
# docstring section for why.
_SCAN_ENGINE = _REPO_ROOT / _ENGINE_REL

# Concatenation-built needles. Coverage 7 asserts none of these appear
# verbatim anywhere in this module's own source -- a gate that quotes its
# own forbidden token verbatim matches itself and can never pass.
_NEEDLE_INVERTED_CALL = "return" + " !" + "op_execute_"
_FORWARD_STATEFUL = "return " + "op_execute_stateful_operation"
_FORWARD_SIMPLE = "return " + "op_execute_simple_operation"
_EXPECTED_FORWARD_TOTAL = 7

_ALL_SELF_CHECK_NEEDLES = (
    ("the negated wrapper return form", _NEEDLE_INVERTED_CALL),
    ("the stateful forwarding form", _FORWARD_STATEFUL),
    ("the simple forwarding form", _FORWARD_SIMPLE),
)

_FORWARD_STATEFUL_RE = re.compile(re.escape(_FORWARD_STATEFUL) + r"\s*\(")
_FORWARD_SIMPLE_RE = re.compile(re.escape(_FORWARD_SIMPLE) + r"\s*\(")
_ENGINE_NEGATED_CALL_RE = re.compile(r"return\s+!\s*(\w+)\s*\(")

_ERASE_REFUSAL_IF_RE = re.compile(r"!\s*is_flag_set\s*\(\s*FLAG_CAN_ERASE\s*\)")
_CHIP_ID_REFUSAL_IF_RE = re.compile(r"handle\s*->\s*chip_id\s*==\s*0")
_RETURN_TRUE_RE = re.compile(r"\breturn\s+true\s*;")


def _strip_comments(text):
    """Strip `//` and `/* ... */` comments AND the contents of every
    string/char literal, replacing each stripped span with whitespace of
    the SAME SHAPE (a newline stays a newline, everything else becomes a
    single space) so every line number in the result matches the original
    file exactly -- borrowed from test_protocol_branch_inventory.py's own
    comment-and-literal stripper. Named `_strip_comments`, matching the
    sibling source-contract gate's own name for its module-level stripper,
    even though -- unlike that sibling's stripper -- this one ALSO strips
    literals. See the module docstring's "Literal-stripping decision"
    section for why that extension is needed here."""
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
        if c == '"' or c == "'":
            quote = c
            out.append(" ")
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\" and i + 1 < n:
                    out.append(" ")
                    out.append("\n" if text[i + 1] == "\n" else " ")
                    i += 2
                    continue
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(" ")
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _assert_identifier_absent(needle, label, text, target_desc):
    """Word-boundary absence check, copied in shape from the sibling
    source-contract gate's own helper of the same name. Used here only
    for a complete needle whose surrounding context is always non-word on
    both sides wherever it legitimately could occur (Coverage 7's
    own-needle self-check) -- never for the wrapper's negated-call prefix,
    which is not a standalone word (see test_wrapper_negated_return_is_absent
    for why that leg uses a plain substring count instead)."""
    matches = re.findall(r"\b" + re.escape(needle) + r"\b", text)
    assert matches == [], (
        f"found {len(matches)} occurrence(s) of {label} in {target_desc}.\n"
        f"Got:\n{text}"
    )


def _extract_function_body(stripped, func_name):
    """Brace-matched extraction of a `bool <func_name>(...) { ... }` body
    from comment-and-literal-stripped C++ text -- never a line-number
    lookup, so a future file move cannot invalidate it."""
    m = re.search(r"\bbool\s+" + re.escape(func_name) + r"\s*\([^)]*\)\s*\{", stripped)
    assert m is not None, (
        f"could not locate `bool {func_name}(...) {{` in the "
        "comment-and-literal-stripped scan target -- has this function "
        "been renamed or removed?"
    )
    start = m.end() - 1
    depth = 0
    i = start
    n = len(stripped)
    while i < n:
        if stripped[i] == "{":
            depth += 1
        elif stripped[i] == "}":
            depth -= 1
            if depth == 0:
                return stripped[start : i + 1]
        i += 1
    raise AssertionError(f"unbalanced braces while extracting {func_name}()")


# Tests


def test_wrapper_negated_return_is_absent():
    """Coverage 1 -- Plan 05 removed the leading `!` from all nine
    eprom_* wrapper bodies in src/eprom_operations.cpp. This leg proves
    the negated form is gone: zero occurrences of the concatenation-built
    needle (the word `return`, then a bang, then the shared op-layer call
    prefix) in the comment-and-literal-stripped wrapper file. A plain
    substring count, not a word-boundary identifier match -- the needle
    is a PREFIX of a larger identifier
    (op_execute_stateful_operation / op_execute_simple_operation), so it
    is never a standalone word and a \\b-wrapped search would never fire,
    passing vacuously forever."""
    stripped = _strip_comments(_SCAN_WRAPPERS.read_text())
    count = stripped.count(_NEEDLE_INVERTED_CALL)
    assert count == 0, (
        f"found {count} occurrence(s) of the negated op-layer call form "
        f"in the comment-and-literal-stripped {_WRAPPERS_REL} -- Plan 05 "
        "removed the leading `!` from all nine eprom_* wrapper bodies; "
        "this construct must not return.\n"
        f"Got:\n{stripped}"
    )


def test_engine_retains_exactly_one_negated_call():
    """Coverage 2 -- Plan 05's flip is an honest nine-to-one negation
    REDUCTION, not an elimination: the callback argument passed into
    op_execute_stateful_operation (_process_incoming_data /
    _process_outgoing_data in eprom_operations.cpp) keeps its own
    opposite, unflipped convention, so exactly one negated call inside a
    return statement must survive in src/operation_utils.cpp. Zero here
    would mean the surviving negation was wrongly removed (silently
    inverting the MAIN-phase delegation's result); more than one would
    mean a NEW negation crept in somewhere this phase did not touch."""
    stripped = _strip_comments(_SCAN_ENGINE.read_text())
    matches = _ENGINE_NEGATED_CALL_RE.findall(stripped)
    assert len(matches) == 1, (
        f"expected exactly 1 surviving negated call inside a return "
        f"statement in {_ENGINE_REL} (Plan 05's honest nine-to-one "
        f"reduction, not an elimination), found {len(matches)}: {matches}.\n"
        f"Got:\n{stripped}"
    )
    assert matches[0] == "callback", (
        "the one surviving negated call was expected to be the MAIN-phase "
        f"callback delegation, found a negated call to `{matches[0]}` "
        "instead -- this is the one call site whose OWN convention (true "
        "on success, false on error) Plan 05 deliberately left unflipped."
    )


def test_the_seven_forwarding_calls_are_present():
    """Coverage 3 -- the positive counterpart to Coverage 1: a deleted,
    emptied or truncated wrapper file would satisfy the absence leg above
    vacuously. This leg requires the two permitted forwarding calls --
    op_execute_stateful_operation and op_execute_simple_operation, each
    reached via a bare `return` -- to together occur exactly
    _EXPECTED_FORWARD_TOTAL (7) times. Phase 204 retired eprom_verify
    (CMD_VERIFY, ordinal 6) first, taking the census from Plan 05's
    original nine to eight, then eprom_blank_check (the blank-check
    command's own ordinal) in the same phase, taking it to seven."""
    stripped = _strip_comments(_SCAN_WRAPPERS.read_text())
    stateful = _FORWARD_STATEFUL_RE.findall(stripped)
    simple = _FORWARD_SIMPLE_RE.findall(stripped)
    total = len(stateful) + len(simple)
    assert total == _EXPECTED_FORWARD_TOTAL, (
        f"expected exactly {_EXPECTED_FORWARD_TOTAL} forwarding return "
        f"sites (stateful + simple) in {_WRAPPERS_REL}, found {total} "
        f"({len(stateful)} stateful, {len(simple)} simple) -- a deleted "
        "or truncated wrapper file would satisfy the absence leg above "
        "vacuously; this positive count is what catches that.\n"
        f"Got:\n{stripped}"
    )


def test_the_two_early_return_refusal_literals_survive():
    """Coverage 4 -- eprom_erase's FLAG_CAN_ERASE refusal and
    eprom_check_chip_id's zero-chip-id refusal are correct under BOTH the
    old and the new convention (both already returned the literal that
    means "finished"), so Plan 05 left both byte-unchanged. Asserted on
    the enclosing function name and the returned literal, never on a line
    number -- Phases 157/158 move this file again."""
    stripped = _strip_comments(_SCAN_WRAPPERS.read_text())

    erase_body = _extract_function_body(stripped, "eprom_erase")
    assert _ERASE_REFUSAL_IF_RE.search(erase_body), (
        "expected eprom_erase() to still guard on "
        "!is_flag_set(FLAG_CAN_ERASE).\nGot:\n" + erase_body
    )
    erase_returns_true = _RETURN_TRUE_RE.findall(erase_body)
    assert len(erase_returns_true) == 1, (
        "expected eprom_erase() to contain exactly one `return true;` "
        f"(the FLAG_CAN_ERASE refusal), found {len(erase_returns_true)}.\n"
        "Got:\n" + erase_body
    )

    chip_id_body = _extract_function_body(stripped, "eprom_check_chip_id")
    assert _CHIP_ID_REFUSAL_IF_RE.search(chip_id_body), (
        "expected eprom_check_chip_id() to still guard on "
        "handle->chip_id == 0.\nGot:\n" + chip_id_body
    )
    chip_id_returns_true = _RETURN_TRUE_RE.findall(chip_id_body)
    assert len(chip_id_returns_true) == 1, (
        "expected eprom_check_chip_id() to contain exactly one `return "
        f"true;` (the zero-chip-id refusal), found "
        f"{len(chip_id_returns_true)}.\nGot:\n" + chip_id_body
    )


def test_scan_targets_are_non_vacuous():
    """Coverage 5 -- structural self-check, never reads the environment
    seam: both DEFAULT scan targets (recomputed fresh from _REPO_ROOT --
    the check_permitted_claims.py _HERE-resolves-to-the-wrong-directory
    landmine, closed here by construction) exist, are non-empty, resolve
    inside this repository, and their comment-and-literal-stripped text is
    non-empty. A missing or empty scan target must FAIL, never silently
    pass as if nothing needed checking."""
    default_wrappers = _REPO_ROOT / _WRAPPERS_REL
    default_engine = _REPO_ROOT / _ENGINE_REL
    for label, p in (
        ("eprom_operations.cpp", default_wrappers),
        ("operation_utils.cpp", default_engine),
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
            f"comment-and-literal-stripped {label} is empty -- nothing "
            "would ever be scanned by this module's other legs."
        )


def test_this_module_cannot_be_silently_skipped():
    """Coverage 6 -- this module's own source contains no runtime
    skip-bypass call, no skip-marker decorator, and no dependency-skip
    call anywhere, so the fail-closed contract this module documents is
    self-enforcing rather than merely stated. The three needle strings
    below are built via concatenation -- the same technique the sibling
    source-contract gate uses for its own equivalent check -- so this
    test's own source and its own failure messages cannot match its own
    check: each literal substring must appear NOWHERE in this file."""
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
    """Coverage 7 -- the concatenated needles built above (the negated
    wrapper return form and the two forwarding forms) must appear NOWHERE
    verbatim in this module's own source, including inside this very
    test's own failure messages. Without this leg, a future edit could
    silently un-concatenate one of them (for example while "simplifying"
    the code) and this gate would keep scanning the firmware sources
    correctly while having quietly stopped being able to fail against
    itself."""
    own_text = Path(__file__).read_text()
    for label, needle in _ALL_SELF_CHECK_NEEDLES:
        _assert_identifier_absent(needle, label, own_text, "this module's own source")
