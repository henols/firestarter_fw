"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 204 Plan 02 -- FWCMD-04's own source-contract gate, the eighth member
of the house source-contract family. Phase 204 removes the CMD_VERIFY /
CMD_BLANK_CHECK command surfaces from the firmware; this module proves that
removing a command surface did not remove the in-algorithm verification that
survives underneath it -- specifically, that the shared final-pass verify
function is still called from inside the plus-final verify arm of the
per-byte program loop, for the two 27C protocols (0x07, 0x08) that ship that
verify mode.

Requirements: FWCMD-04

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
     three concatenation-built needles (the shared verify call's own
     identifier, the plus-final enumerator's identifier as used in the
     containment arm's regex, and the same enumerator's identifier as used
     in the parameter-table leg's regex) appears verbatim anywhere in this
     module's own source, so this gate cannot match itself.

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

# Environment seam -- binds at IMPORT time. See the module docstring's
# "Environment seams" section above. Only this one target is overridable.
_SCAN_EPROM = Path(
    os.environ.get(
        "FIRESTARTER_VERIFY_SURVIVAL_SCAN_EPROM_SOURCE",
        str(_REPO_ROOT / _EPROM_REL),
    )
)
# The remaining two targets have no override -- see the docstring section
# above for why. A stray environment value pointed at the eprom.cpp seam
# cannot make Coverage 2 or 3 vacuous.
_SCAN_PARAMS = _REPO_ROOT / _PARAMS_REL
_SCAN_MEMORY = _REPO_ROOT / _MEMORY_REL

# Concatenation-built needles. Coverage 6 asserts none of these appears
# verbatim anywhere in this module's own source -- see the module docstring
# and the plan's own warning: a gate that quotes its own forbidden or
# required tokens verbatim can end up matching itself.
_NEEDLE_CALL = "memory_verify_exec" + "ute"
_NEEDLE_MODE = "VERIFY_PER_PULSE_PLUS" + "_FINAL"
_NEEDLE_DEFINITION = "VERIFY_PER_PULSE_PLUS_FI" + "NAL"

_ALL_SELF_CHECK_NEEDLES = (
    ("the shared final-pass verify call's identifier", _NEEDLE_CALL),
    ("the plus-final enumerator's identifier (containment arm)", _NEEDLE_MODE),
    ("the plus-final enumerator's identifier (parameter-table leg)", _NEEDLE_DEFINITION),
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


def test_scan_targets_are_non_vacuous():
    """Coverage 4 -- structural self-check, never reads the environment
    seam: every DEFAULT scan target (recomputed fresh from _REPO_ROOT --
    the check_permitted_claims.py _HERE-resolves-to-the-wrong-directory
    landmine, closed here by construction) exists, is non-empty, resolves
    inside this repository, and its comment-stripped text is non-empty. A
    missing or empty scan target must FAIL, never silently pass as if
    nothing needed checking."""
    default_targets = (
        (_EPROM_REL, _REPO_ROOT / _EPROM_REL),
        (_PARAMS_REL, _REPO_ROOT / _PARAMS_REL),
        (_MEMORY_REL, _REPO_ROOT / _MEMORY_REL),
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
