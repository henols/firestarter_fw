"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 158 Plan 02 -- pins the `jsmntok_t` layout Plan 02's own task 1 landed
in lib/jsmn/src/jsmn.h with a committed, non-vacuous source-scan gate.

Requirements: LAND-05

Defect class this closes: the twelve `-1` sentinel field references on six
lines of lib/jsmn/src/jsmn.c (`:15,222,241,256,290,348` -- each line carrying
two field references, `start` and `end`, twelve total) depend on `start` and
`end` staying a SIGNED type. `jsmn_alloc_token` sets `tok->start = tok->end =
-1;` and every closing-bracket branch of `jsmn_parse` tests
`token->start != -1 && token->end == -1` to find the innermost unclosed
token. A future tidy-up narrowing either field to `unsigned` or to a
fixed-width unsigned type (`uint16_t`, for example) would break every one of
those twelve sentinel comparisons SILENTLY -- the code still compiles, and
no other gate in this tree would notice, because `sizeof(jsmntok_t)` cannot
be asserted in a native test (AVR gives 6 B, the host gives 12 B; see the
"sizeof prohibition" section below) and no native suite exercises the
sentinel's *signedness*, only its *behaviour* on inputs that happen not to
trip the bug. This module is a SOURCE CONTRACT: it proves the shipped text
still says `int start;` / `int end;`, never what the compiled code does at
runtime.

Coverage:
  1. test_token_start_and_end_remain_signed_int -- within the LIVE
     jsmntok struct region only (see "Region scope" below), exactly one
     `int start;` member declaration and exactly one `int end;` member
     declaration, and no unsigned or fixed-width integer type is applied
     to either member name anywhere in that region.
  2. test_token_type_and_size_are_uint8 -- within the same region, exactly
     one `uint8_t type;` and exactly one `uint8_t size;`, so a revert of
     task 1's narrowing is also loud.
  3. test_scan_target_is_non_vacuous -- the default scan target
     (recomputed fresh from _REPO_ROOT, never reading the environment
     seam) exists, is non-empty, resolves inside this repository, its
     comment-and-literal-stripped text is non-empty, AND the live-header
     region sliced from it is also non-empty.
  4. test_this_module_cannot_be_silently_skipped -- this module's own
     source contains no skip-bypass call, no skip-marker decorator and no
     dependency-skip call anywhere.
  5. test_own_needles_do_not_appear_verbatim_in_this_module -- the
     concatenation-built needles used by Coverage 4 appear nowhere
     verbatim in this module's own source, so this gate cannot match
     itself.

Region scope (load-bearing -- read before editing this module).
lib/jsmn/src/jsmn.h carries a DEAD DUPLICATE IMPLEMENTATION starting at the
`#ifndef JSMN_HEADER` line, guarded by a `#define JSMN_HEADER` that precedes
it earlier in the same file -- so that whole second copy of jsmntok-adjacent
code compiles in NO translation unit, but is live TEXT to a naive grep or
regex scan (OD-6, ceiling 9). A gate that scanned the WHOLE file could be
satisfied by a correct-looking `int start;` / `int end;` pair sitting in the
dead copy while the LIVE struct above it had been silently narrowed --
passing vacuously on exactly the defect it exists to catch. This module
therefore scans ONLY the region of the (comment-and-literal-stripped)
header text from its start up to the first occurrence of the literal marker
`#ifndef JSMN_HEADER` (stored in `_LIVE_STRUCT_END_MARKER`), via
`_live_header_region`. `_live_header_region` RAISES rather than falling
back to scanning the whole file if that marker is not found -- a missing
marker means the header's structure changed underneath this module, which
is a reason to fail loudly, not a licence to widen the scan (Coverage 3's
own non-vacuity leg also runs `_live_header_region` over the real file, so
a marker rename is caught there too).

sizeof prohibition. This module asserts NOTHING about `sizeof(jsmntok_t)`.
`jsmntok_t` is 6 bytes on AVR (uint8_t + uint8_t + int + int, no padding at
`-Os`) and 12 bytes on the pytest host (int is commonly 4 bytes there, plus
alignment padding before the two `int` members) -- asserting a fixed size
in a module that Python, not avr-gcc, imports and executes would be
asserting the HOST's sizeof, which proves nothing about the shipped AVR
binary. `src/json_parser.c`'s `field_desc_t` documents the identical
prohibition for its own sibling struct (grep
"ASSERTION ON sizeof(field_desc_t) MAY BE AUTHORED WITHOUT A TARGET GUARD"
in that file). The RAM saving this module's Task 1 commit records is
witnessed by the linker's own `RAM: used N` line and by `avr-nm -S` on a
real atmega328p object file -- never by a native `sizeof` expression
(ceiling 6).

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover them)
  - FIRESTARTER_JSMN_TOKEN_LAYOUT_SCAN_SOURCE -- overrides the scanned
    lib/jsmn/src/jsmn.h path. Binds at IMPORT time (the module-level
    `Path(os.environ.get(...))` expression below), so `monkeypatch.setenv`
    CANNOT move it after this module has already been imported by pytest's
    collection phase -- a planted-violation run that exercises this seam
    must set the variable in a CHILD PROCESS environment (a subprocess
    invocation of `python3 -m pytest` against this one module) before this
    module is imported there, never via a post-import monkeypatch.
    Coverage 3 (the non-vacuity leg) deliberately never reads it: it
    recomputes the default target directly from `_REPO_ROOT`, so it stays
    meaningful even when the seam is pointed elsewhere for a probe.
    Coverage 4 and 5 read only this module's own source and never touch
    the seam either. A stray seam value left set after a planted run
    cannot make Coverage 1 or 2 pass vacuously -- at worst it makes them
    fail loudly (a FileNotFoundError surfaces from Path.read_text() when
    the overridden path does not exist), which is exactly Probe C's own
    shape (see this module's own commit message and the plan's SUMMARY for
    the recorded outcome).

CI coverage, stated CORRECTLY (not copied from the boolean-convention
analog, whose own equivalent paragraph is stale in exactly the way Plan 05
of this phase corrects in tests/test_check_size_baseline.py and
tests/meta_presence.py -- see C-9 in .planning/v1.33/158-before-figures.md).
`pytest tests/ -v` DOES fire on this milestone branch: it appears at
.github/workflows/build.yml:161, ungated by any `if:`, and that workflow's
own `on:` block filters `push: branches: ['**', '!beta']` -- `'**'` matches
every branch including `gsd/v1.33-source-hygiene-firmware-size-reduction`,
and only `beta` itself is excluded. This module therefore IS exercised by
CI on this branch, as part of that one `pytest tests/ -v` step, the moment
this commit is pushed.

This module is a standalone pytest module: it is not named check_*.py (that
glob belongs to a different, paired convention this module does not need),
it adds no shared pytest configuration or fixture-registration file, and it
imports nothing beyond the Python standard library. It never imports,
parametrizes against, or edits any pre-existing gate module; the scanning
logic below is its own independent re-derivation of this plan's own gate
specification, modelled in shape on
tests/test_boolean_convention_source_contract_v133.py (the smallest of the
four in-tree source-contract gates) but scanning different text for a
different defect class.
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_JSMN_HEADER_REL = "lib/jsmn/src/jsmn.h"

# Environment seam -- binds at IMPORT time. See the module docstring's
# "Environment seams" section above.
_SCAN_JSMN = Path(
    os.environ.get(
        "FIRESTARTER_JSMN_TOKEN_LAYOUT_SCAN_SOURCE", str(_REPO_ROOT / _JSMN_HEADER_REL)
    )
)

# The dead duplicate implementation begins at this literal marker. See the
# module docstring's "Region scope" section for why the scan stops here.
_LIVE_STRUCT_END_MARKER = "#ifndef JSMN_HEADER"

# Concatenation-built needles. Coverage 5 asserts none of these appear
# verbatim anywhere in this module's own source -- a gate that quotes its
# own forbidden token verbatim matches itself and can never pass.
_SKIP_CALL = "pytest" + ".skip"
_SKIPIF_MARKER = "mark" + ".skipif"
_DEPENDENCY_SKIP_CALL = "importor" + "skip"

_ALL_SELF_CHECK_NEEDLES = (
    ("the skip-bypass call", _SKIP_CALL),
    ("the skipif marker", _SKIPIF_MARKER),
    ("the dependency-skip call", _DEPENDENCY_SKIP_CALL),
)

# Region-scoped member-declaration patterns. `\b` boundaries on both sides of
# the type keyword and the member name so `int start;` cannot be satisfied
# by a longer identifier like `unsigned_start` or `int startup;`.
_START_INT_DECL_RE = re.compile(r"\bint\s+start\s*;")
_END_INT_DECL_RE = re.compile(r"\bint\s+end\s*;")
_TYPE_UINT8_DECL_RE = re.compile(r"\buint8_t\s+type\s*;")
_SIZE_UINT8_DECL_RE = re.compile(r"\buint8_t\s+size\s*;")

# Any member declaration at all named `start` / `end` -- used together with
# the exact-int-count assertions above to prove the ONE declaration each
# member has is the signed `int` one, not merely that an `int`-typed one
# exists ALONGSIDE some other narrowed redeclaration.
_ANY_START_MEMBER_RE = re.compile(r"\bstart\s*;")
_ANY_END_MEMBER_RE = re.compile(r"\bend\s*;")

# Explicit forbidden-type scan: an unsigned or fixed-width integer type
# immediately preceding `start;` or `end;` anywhere in the live region.
_FORBIDDEN_NARROW_TYPE_RE = re.compile(
    r"\b(?:unsigned(?:\s+int)?|uint8_t|uint16_t|uint32_t|uint64_t|"
    r"int8_t|int16_t|int32_t|int64_t|size_t)\s+(?:start|end)\s*;"
)


def _strip_comments(text):
    """Strip `//` and `/* ... */` comments AND the contents of every
    string/char literal, replacing each stripped span with whitespace of
    the SAME SHAPE (a newline stays a newline, everything else becomes a
    single space) so every line number in the result matches the original
    file exactly -- copied verbatim from
    tests/test_boolean_convention_source_contract_v133.py's own
    `_strip_comments`, itself borrowed from
    test_protocol_branch_inventory.py's stripper. Load-bearing here because
    task 1 of this plan added inline comments naming `start`/`end` and
    `jsmn.c` line numbers directly inside the live struct -- without
    stripping, those comments' own prose (which mentions `start`/`end` in
    plain English) could confuse a naive substring scan; stripping removes
    that risk entirely rather than trusting the comment wording to stay
    scan-safe forever."""
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
    source-contract gate's own helper of the same name."""
    matches = re.findall(r"\b" + re.escape(needle) + r"\b", text)
    assert matches == [], (
        f"found {len(matches)} occurrence(s) of {label} in {target_desc}.\n"
        f"Got:\n{text}"
    )


def _live_header_region(text):
    """Returns the substring of the comment-and-literal-stripped header
    from the start of the file up to (not including) the first occurrence
    of `_LIVE_STRUCT_END_MARKER`. RAISES rather than returning the whole
    file when the marker is absent -- see the module docstring's "Region
    scope" section for why a missing marker must fail loudly instead of
    silently widening the scan to include the dead duplicate
    implementation."""
    idx = text.find(_LIVE_STRUCT_END_MARKER)
    assert idx != -1, (
        f"expected to find the literal marker {_LIVE_STRUCT_END_MARKER!r} in "
        "the scanned jsmn.h text -- this module scopes its scan to the "
        "region ABOVE that marker (the live jsmntok struct), and a missing "
        "marker means the header's structure changed underneath this "
        "module. Widening the scan to the whole file would risk being "
        "satisfied by the dead duplicate implementation instead of the "
        "live struct (OD-6) -- see the module docstring's 'Region scope' "
        "section."
    )
    return text[:idx]


# Tests


def test_token_start_and_end_remain_signed_int():
    """Coverage 1 -- within the LIVE jsmntok struct region only, exactly
    one `int start;` and exactly one `int end;` member declaration, and no
    unsigned or fixed-width integer type is applied to either member name
    anywhere in that region. The twelve `-1` sentinel field references on
    six lines of lib/jsmn/src/jsmn.c (`:15,222,241,256,290,348`) depend on
    both fields staying signed; narrowing either to unsigned (or to any
    fixed-width unsigned type) would break every one of those twelve
    comparisons SILENTLY -- the code still compiles, and the failure would
    only surface as a JSON command being mis-parsed on real hardware."""
    stripped = _strip_comments(_SCAN_JSMN.read_text())
    region = _live_header_region(stripped)

    start_count = len(_START_INT_DECL_RE.findall(region))
    end_count = len(_END_INT_DECL_RE.findall(region))
    any_start_count = len(_ANY_START_MEMBER_RE.findall(region))
    any_end_count = len(_ANY_END_MEMBER_RE.findall(region))
    forbidden = _FORBIDDEN_NARROW_TYPE_RE.findall(region)

    assert start_count == 1, (
        f"expected exactly one `int start;` member declaration in the live "
        f"jsmntok struct region of {_JSMN_HEADER_REL}, found {start_count}. "
        "The twelve `-1` sentinel field references on six lines of "
        "lib/jsmn/src/jsmn.c (:15,222,241,256,290,348) depend on `start` "
        "staying a signed int; a narrowed or unsigned `start` would break "
        "the parser silently.\n"
        f"Got region:\n{region}"
    )
    assert end_count == 1, (
        f"expected exactly one `int end;` member declaration in the live "
        f"jsmntok struct region of {_JSMN_HEADER_REL}, found {end_count}. "
        "The twelve `-1` sentinel field references on six lines of "
        "lib/jsmn/src/jsmn.c (:15,222,241,256,290,348) depend on `end` "
        "staying a signed int; a narrowed or unsigned `end` would break "
        "the parser silently.\n"
        f"Got region:\n{region}"
    )
    assert any_start_count == 1, (
        f"expected exactly one member declaration named `start` in the live "
        f"jsmntok struct region, found {any_start_count} -- combined with "
        "the int-typed count above, this proves the one `start` member is "
        "the signed `int` one, not one of several redeclarations.\n"
        f"Got region:\n{region}"
    )
    assert any_end_count == 1, (
        f"expected exactly one member declaration named `end` in the live "
        f"jsmntok struct region, found {any_end_count} -- combined with "
        "the int-typed count above, this proves the one `end` member is "
        "the signed `int` one, not one of several redeclarations.\n"
        f"Got region:\n{region}"
    )
    assert forbidden == [], (
        f"found an unsigned or fixed-width integer type applied to `start` "
        f"or `end` in the live jsmntok struct region: {forbidden}. Both "
        "fields must stay plain signed `int` -- see the sentinel "
        "dependency named above.\n"
        f"Got region:\n{region}"
    )


def test_token_type_and_size_are_uint8():
    """Coverage 2 -- within the same live-struct region, exactly one
    `uint8_t type;` and exactly one `uint8_t size;`, so a revert of task
    1's narrowing (back to `jsmntype_t type;` / `int size;`) is also
    loud."""
    stripped = _strip_comments(_SCAN_JSMN.read_text())
    region = _live_header_region(stripped)

    type_count = len(_TYPE_UINT8_DECL_RE.findall(region))
    size_count = len(_SIZE_UINT8_DECL_RE.findall(region))

    assert type_count == 1, (
        f"expected exactly one `uint8_t type;` member declaration in the "
        f"live jsmntok struct region of {_JSMN_HEADER_REL}, found "
        f"{type_count} -- this is Task 1's own narrowing; a revert to "
        "`jsmntype_t type;` must be loud.\n"
        f"Got region:\n{region}"
    )
    assert size_count == 1, (
        f"expected exactly one `uint8_t size;` member declaration in the "
        f"live jsmntok struct region of {_JSMN_HEADER_REL}, found "
        f"{size_count} -- this is Task 1's own narrowing; a revert to "
        "`int size;` must be loud.\n"
        f"Got region:\n{region}"
    )


def test_scan_target_is_non_vacuous():
    """Coverage 3 -- structural self-check, never reads the environment
    seam: the DEFAULT scan target (recomputed fresh from _REPO_ROOT, the
    check_permitted_claims.py _HERE-resolves-to-the-wrong-directory
    landmine, closed here by construction) exists, is non-empty, resolves
    inside this repository, its comment-and-literal-stripped text is
    non-empty, AND `_live_header_region` over it is also non-empty. A
    missing or emptied scan target must FAIL, never silently pass as if
    nothing needed checking."""
    default_target = _REPO_ROOT / _JSMN_HEADER_REL
    assert default_target.is_file(), (
        f"default jsmn.h scan target {default_target} does not exist on "
        "disk -- a missing scan target must FAIL, never silently pass."
    )
    assert default_target.stat().st_size > 0, (
        f"default jsmn.h scan target {default_target} is empty"
    )
    assert default_target.resolve().is_relative_to(_REPO_ROOT), (
        f"default jsmn.h scan target {default_target} resolves outside "
        f"_REPO_ROOT ({_REPO_ROOT}) -- a naive future copy of this module "
        "into another directory must fail loudly here, not scan nothing "
        "and exit 0."
    )
    stripped = _strip_comments(default_target.read_text())
    assert stripped.strip() != "", (
        f"comment-and-literal-stripped {_JSMN_HEADER_REL} is empty -- "
        "nothing would ever be scanned by this module's other legs."
    )
    region = _live_header_region(stripped)
    assert region.strip() != "", (
        f"the live-header region sliced from {_JSMN_HEADER_REL} (above the "
        f"{_LIVE_STRUCT_END_MARKER!r} marker) is empty -- nothing would "
        "ever be scanned by Coverage 1 or Coverage 2."
    )


def test_this_module_cannot_be_silently_skipped():
    """Coverage 4 -- this module's own source contains no runtime
    skip-bypass call, no skip-marker decorator, and no dependency-skip
    call anywhere, so the fail-closed contract this module documents is
    self-enforcing rather than merely stated. The three needle strings
    below are built via concatenation -- the same technique the sibling
    source-contract gate uses for its own equivalent check -- so this
    test's own source and its own failure messages cannot match its own
    check: each literal substring must appear NOWHERE in this file."""
    own_text = Path(__file__).read_text()
    assert _SKIP_CALL not in own_text, (
        "expected no " + _SKIP_CALL + " call anywhere in this module -- a "
        "missing or empty scan target must FAIL, never SKIP."
    )
    assert _SKIPIF_MARKER not in own_text, (
        "expected no @pytest." + _SKIPIF_MARKER + " decorator anywhere in "
        "this module -- a missing or empty scan target must FAIL, never "
        "SKIP."
    )
    assert ("pytest." + _DEPENDENCY_SKIP_CALL) not in own_text, (
        "expected no pytest." + _DEPENDENCY_SKIP_CALL + " call anywhere in "
        "this module -- a missing dependency must FAIL, never SKIP."
    )


def test_own_needles_do_not_appear_verbatim_in_this_module():
    """Coverage 5 -- the concatenated needles built above (the skip-bypass
    call, the skipif marker, the dependency-skip call) must appear NOWHERE
    verbatim
    in this module's own source, including inside this very test's own
    failure messages. Without this leg, a future edit could silently
    un-concatenate one of them (for example while "simplifying" the code)
    and this gate would keep scanning jsmn.h correctly while having
    quietly stopped being able to fail against itself."""
    own_text = Path(__file__).read_text()
    for label, needle in _ALL_SELF_CHECK_NEEDLES:
        _assert_identifier_absent(needle, label, own_text, "this module's own source")
