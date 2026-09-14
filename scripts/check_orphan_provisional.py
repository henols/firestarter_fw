#!/usr/bin/env python3
"""scripts/check_orphan_provisional.py -- BASE-05 orphan-provisional-macro
gate (Phase 123 Plan 05, D-06/D-07).

Every `RURP_*_PROVISIONAL`-style flag must have at least one consumer
outside its own `#define` line. A provisional pin-map placeholder that
compiles clean but is never actually gated by anything is exactly this
project's v1.18-class defect: an entire milestone (v1.18) traced back to one
mis-modelled pin, and a provisional flag with zero consumers is the same
failure mode one level up -- the flag that was SUPPOSED to force a review
before real hardware trusts the placeholder, silently doing nothing.

**Confirmed absent from `beta` today.** `grep -rn PROVISIONAL` over this
firmware tree returns nothing -- this gate ships UNARMED and exits 0 on the
real tree (123-RESEARCH.md "BASE-05: The Orphan Provisional Macro").

**Confirmed present with exactly ONE repo-wide hit on the py32 branch.**
`include/boards/py32f071_rurp_shield.h` defines both:

    37: #define RURP_PY32F071_PINMAP_CONFIGURED 1
    38: #define RURP_PY32F071_PINMAP_PROVISIONAL 1

and a repo-wide grep for RURP_PY32F071_PINMAP_PROVISIONAL returns exactly
one hit -- its own definition at line 38. So this gate has a known, real,
non-planted first firing: landed unchanged, Phase 124 cannot merge the port
without this gate blocking it, because the flag has zero consumers. That is
precisely MERGE-04's requirement (the provisional-pinmap refusal must
actually be able to fire) -- this gate is the enforcement arm that makes it
land wired instead of decorative.

**Note, separately, the sibling `_CONFIGURED` macro is NOT this gate's
concern.** `RURP_PY32F071_PINMAP_CONFIGURED` is `#define`d `1` at line 37 and
tested with `#if !RURP_PY32F071_PINMAP_CONFIGURED -> #error` at lines 71-73
of the SAME header -- so that `#error` is structurally dead (the macro can
never be anything but 1, so the negation can never be true). Fixing that is
MERGE-04's problem, not this gate's: `_CONFIGURED` is not a `_PROVISIONAL`
flag and this checker must NOT be widened to try to catch it. Widening the
match pattern to catch `_CONFIGURED` too would conflate two different
defect classes (an orphan warning flag vs. a dead compile-time guard) behind
one gate, which is exactly the kind of "helpful" scope creep that makes a
gate's failure message stop telling the reader what actually broke.

Coarse-key arming (D-07). `platform/py32f071/` does not exist on this branch
yet -- it arrives with Phase 124's merge. `ARMED` is computed from whether
`<root>/platform/py32f071` is a DIRECTORY (never a manual flip constant),
mirroring check_cmake_manifest.py's identical D-07 idiom exactly so the two
gates read as one pattern rather than two dialects. If absent, this gate
prints `UNARMED:` naming the absent directory and Phase 124, and exits 0.

**Rejected alternative reading, recorded deliberately.** BASE-05's scan is
repo-wide (see below) and would be semantically meaningful even without the
ARM port -- a `RURP_AVR_SOMETHING_PROVISIONAL` macro could appear tomorrow
in shared code. Two readings were considered:

  (a) Follow D-07 literally: UNARMED until platform/py32f071/ exists.
  (b) Always armed, but report "0 provisional macros found" as a pass.

Reading (a) was CHOSEN. Reading (b) was considered and REJECTED: an
always-armed gate would have to treat "zero provisional macros found" as a
pass, which is precisely the vacuous shape D-08's floor-count requirement
exists to forbid (a gate that always exits 0 on an empty tree looks
identical to a gate that is working correctly). Reconciling (b) with the
never-vacuous rule below would need a special case carved out of the
arming logic itself. Consistency with D-07's literal reading wins, and it
is what stops a later reader from "fixing" this gate by widening it to
always-armed.

Definition scan (repo-wide by design). `DEFINE_RE` matches a preprocessor
`#define` of an identifier named `RURP_`, followed by upper-case letters,
digits and underscores, ending in `_PROVISIONAL`, tolerating whitespace
between `#` and `define`. The scan covers `SCAN_DIRS` = include/, src/,
platform/, test/ (restricted to `SCAN_SUFFIXES` = .h, .hpp, .c, .cpp) --
the WHOLE firmware repo, not just platform/py32f071/. The `RURP_` prefix is
what bounds the pattern; restricting the scan to the platform directory
would miss a provisional flag introduced in SHARED code (include/ or src/),
which is the more dangerous case -- a provisional flag in a file every
platform compiles is a bigger blast radius than one confined to the new
ARM-only tree.

Consumer search. For each defined macro, every OTHER occurrence of the
identical identifier anywhere in the same directory set counts as a
consumer, EXCEPT: the defining line itself, any `#undef` of the macro
(`UNDEF_RE` exists purely to exclude this -- an `#undef` removes the flag,
it does not consume it as a gated behaviour), and any occurrence that
appears only inside a `//` or `/* */` comment (a comment naming the macro
documents it, it does not consume it -- counting a comment as a consumer
is the same class of false-negative as counting an `#undef`, per
123-RESEARCH.md's discriminating-fixture requirement). Comments are
stripped (newlines preserved, so line numbers stay valid) before the
consumer scan runs. A macro with zero consumers is a violation naming the
macro, its defining file and line, and stating what a real consumer would
look like (a `#if`/`#ifdef`/`#elif` test, or a direct reference in live
code) -- the message tells the reader how to fix it, not merely that it is
wrong.

Never-vacuous guard. If the gate is ARMED (platform/py32f071/ present) and
the definition scan finds ZERO `_PROVISIONAL` macros anywhere in the repo,
that is a FAILURE, not a pass: an armed key says a port with a provisional
pin map is present, and finding no provisional flag at all means either the
flag was renamed out of this pattern, or SCAN_DIRS/SCAN_SUFFIXES no longer
cover where it lives. Exit 1 naming which condition is suspected. This is
the single most important design decision in this checker, and it is the
reason rejected reading (b) above could never have been chosen: an
always-armed gate cannot take this position, because "zero found" would be
indistinguishable from its own default pass state.

Encoding and binary safety. Every scanned file is read as UTF-8 with
decoding errors replaced (`errors="replace"`), so one stray non-UTF-8 byte
in a source file cannot crash this gate into an unhandled traceback that a
CI caller might misread as "gate did not run" rather than "gate found a
violation". A file that cannot be opened at all (permissions, deleted
mid-scan) is a scan failure -- exit 2, never a silent skip of that file,
because silently skipping a file could hide the one file with the orphan
macro in it.

Output. `UNARMED:` + exit 0 when platform/py32f071/ is absent. `PASS:`
naming every provisional macro found and its consumer count, so a run that
scanned nothing cannot visually resemble a run that scanned everything and
found it clean. `FAIL:` bucketed with a 20-row cap, each row naming the
macro, its defining file:line, and what a consumer would look like.
`ERROR:` to stderr + exit 2 for scan/configuration failures (unreadable
file, scan directory missing while armed in an unexpected way).

Non-claim: a green run proves every `RURP_*_PROVISIONAL` flag found by this
scan is REFERENCED in live (non-comment) text somewhere outside its own
definition. It does NOT prove that reference is a working guard -- a
`#if RURP_..._PROVISIONAL` that is always true, or a reference inside dead
code, still counts as a consumer here. Proving the reference actually gates
behaviour is out of this gate's scope entirely.

Exit codes:
  0 -- UNARMED (platform/py32f071/ absent), OR armed and every
       `_PROVISIONAL` macro found has at least one consumer outside its own
       definition (gate passes)
  1 -- armed, and at least one `_PROVISIONAL` macro has zero consumers, OR
       armed and zero `_PROVISIONAL` macros were found at all
       (never-vacuous guard)
  2 -- a scan/configuration failure: a scan directory could not be read, or
       a file could not be decoded/opened -- a tool error, never silently
       reported as a pass

Anti-hollow contract: this checker's mandatory paired pytest is
`tests/test_check_orphan_provisional.py`, exercising the two fixture trees
under `tests/fixtures/` (`planted_orphan_provisional_macro/`,
`clean_orphan_provisional_consumed/`) plus the shared `clean_unarmed_tree/`
(from Plan 123-04) via the `FIRESTARTER_PROVISIONAL_ROOT` env seam and a
real subprocess -- never an in-process import -- so a passing suite proves
this script itself fails on a real orphaned macro, not merely that the test
asserts it should.

Usage:
    python3 scripts/check_orphan_provisional.py
    FIRESTARTER_PROVISIONAL_ROOT=/path/to/tree python3 scripts/check_orphan_provisional.py
"""
import os
import re
import sys
from pathlib import Path

# Resolve the repo root from this file's location so the gate behaves
# identically regardless of the caller's working directory (mirrors
# check_cmake_manifest.py:110).
REPO_ROOT = Path(__file__).resolve().parent.parent

FIRESTARTER_PROVISIONAL_ROOT = os.environ.get(
    "FIRESTARTER_PROVISIONAL_ROOT", str(REPO_ROOT)
)

_ROOT = Path(FIRESTARTER_PROVISIONAL_ROOT)
_PLATFORM_DIR = _ROOT / "platform" / "py32f071"

ARMED = _PLATFORM_DIR.is_dir()

# Repo-wide scan scope. The RURP_ prefix is what bounds the pattern, not the
# directory restriction -- a provisional flag in include/ or src/ (shared
# code every platform compiles) is the more dangerous case, so the scan is
# NOT scoped to platform/py32f071/ alone.
SCAN_DIRS = ("include", "src", "platform", "test")
SCAN_SUFFIXES = (".h", ".hpp", ".c", ".cpp")

# Matches a #define of a RURP_*_PROVISIONAL macro, tolerating whitespace
# between '#' and 'define'. Captures the macro name and (via finditer's
# match object) the line is recovered by splitting the source text by line
# number in find_definitions() below. Mirrors the exact spelling used by
# py32f071_rurp_shield.h ("#define RURP_PY32F071_PINMAP_PROVISIONAL 1").
#
# Deliberately [ \t]* (horizontal whitespace only), NOT \s* -- \s matches
# a literal newline too, so with re.MULTILINE a leading "^\s*" can walk
# BACKWARDS across a preceding blank line and anchor the match's start()
# one or more lines earlier than the '#define' text itself, corrupting the
# reported definition line number (and, worse, silently defeating the
# same-line exclusion in find_consumers(), which compares by (path, line)
# tuple). Restricting to [ \t]* keeps the match anchored to the actual
# '#define' line.
DEFINE_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(?P<macro>RURP_[A-Z0-9_]*_PROVISIONAL)\b", re.MULTILINE)

# Matches an #undef of the same identifier shape -- an #undef is NOT a
# consumer (it removes the flag, it does not gate behaviour on it), so
# every #undef line is excluded from the consumer search explicitly. Same
# [ \t]*-only rationale as DEFINE_RE above.
UNDEF_RE = re.compile(r"^[ \t]*#[ \t]*undef[ \t]+(?P<macro>RURP_[A-Z0-9_]*_PROVISIONAL)\b", re.MULTILINE)


class ScanError(Exception):
    """A scan/configuration failure: an unreadable file or scan directory.

    Caught only at the entry point and converted to exit 2 -- never exit 0
    and never exit 1. A file this gate cannot read must never be silently
    skipped, because the one unreadable file could be the one with the
    orphan macro in it.
    """


def _iter_scan_files(root):
    """Yield every file under root/<SCAN_DIRS>/ whose suffix is in
    SCAN_SUFFIXES, as (relative_posix_path, Path) pairs. A scan directory
    that does not exist is simply skipped (not every fixture tree has all
    four; the real tree may not have platform/ or test/ before Phase 124).
    """
    for scan_dir in SCAN_DIRS:
        d = root / scan_dir
        if not d.is_dir():
            continue
        for p in sorted(d.rglob("*")):
            if p.is_file() and p.suffix in SCAN_SUFFIXES:
                try:
                    rel = str(p.relative_to(root)).replace(os.sep, "/")
                except ValueError:
                    rel = str(p)
                yield rel, p


def _read_text(path):
    """Read a file as UTF-8 with decode errors replaced -- one stray
    non-UTF-8 byte must never crash this gate into an unhandled traceback.
    A file that cannot be OPENED at all (not merely decoded oddly) is a
    ScanError -> exit 2, never a silent skip.
    """
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        raise ScanError(f"could not read {path}: {e}") from e


def find_definitions(root):
    """Scan every file under root/<SCAN_DIRS>/ for RURP_*_PROVISIONAL
    #define lines. Returns {macro_name: [(rel_path, line_no), ...]} -- a
    list because nothing prevents (accidental) re-definition in more than
    one file; every defining occurrence is tracked.
    """
    definitions = {}
    for rel, path in _iter_scan_files(root):
        text = _read_text(path)
        for m in DEFINE_RE.finditer(text):
            macro = m.group("macro")
            line_no = text.count("\n", 0, m.start()) + 1
            definitions.setdefault(macro, []).append((rel, line_no))
    return definitions


def _strip_comments(text):
    """Remove `//` and `/* */` comments from `text`, replacing their
    contents with whitespace/newlines so the LINE NUMBERING of everything
    else is unchanged. Used only for the consumer scan -- a macro named
    solely inside a comment must not count as a consumer (see the
    "Consumer search" section of the module docstring and threat
    T-123-05-01, which treats a loose-comment match as the same defect
    class as counting a bare `#undef`).
    """

    def _repl_block(m):
        return "\n" * m.group(0).count("\n")

    text = re.sub(r"/\*.*?\*/", _repl_block, text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def find_consumers(root, macro, defining_locations):
    """Return [(rel_path, line_no), ...] for every occurrence of `macro`
    anywhere under root/<SCAN_DIRS>/ that is NOT one of its own defining
    lines, NOT an #undef of it, and NOT inside a comment. A plain
    identifier match (word-boundary) over comment-stripped text is used, so
    a #if/#ifdef/#elif test or a direct reference in live code counts as a
    consumer, but a comment mention alone does not (see the module
    docstring's "Consumer search" section and Non-claim paragraph).
    """
    defining_set = set(defining_locations)
    consumer_re = re.compile(r"\b" + re.escape(macro) + r"\b")
    consumers = []
    for rel, path in _iter_scan_files(root):
        text = _read_text(path)
        undef_lines = {
            text.count("\n", 0, m.start()) + 1
            for m in UNDEF_RE.finditer(text)
            if m.group("macro") == macro
        }
        scan_text = _strip_comments(text)
        for m in consumer_re.finditer(scan_text):
            line_no = scan_text.count("\n", 0, m.start()) + 1
            if (rel, line_no) in defining_set:
                continue
            if line_no in undef_lines:
                continue
            consumers.append((rel, line_no))
    return consumers


def main():
    if not ARMED:
        print(
            f"UNARMED: {_PLATFORM_DIR} absent -- this gate arms itself the "
            "moment Phase 124 lands the py32f071 port (no manual flip "
            "needed; a rename inside the port cannot disarm it either)."
        )
        return 0

    try:
        definitions = find_definitions(_ROOT)
    except ScanError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    if not definitions:
        print(
            "FAIL: armed (platform/py32f071/ present) but ZERO "
            "RURP_*_PROVISIONAL definitions were found under "
            f"{sorted(SCAN_DIRS)} -- either the flag was renamed out of "
            "the RURP_*_PROVISIONAL pattern, or SCAN_DIRS/SCAN_SUFFIXES no "
            "longer cover where it lives. An armed tree with no "
            "provisional flag at all must never look like a clean pass "
            "(never-vacuous guard)."
        )
        return 1

    violations = []
    pass_lines = []
    for macro in sorted(definitions):
        defining_locations = definitions[macro]
        try:
            consumers = find_consumers(_ROOT, macro, defining_locations)
        except ScanError as e:
            print(f"ERROR: {e}", file=sys.stderr)
            return 2
        if not consumers:
            loc = ", ".join(f"{rel}:{line}" for rel, line in defining_locations)
            violations.append(
                f"{macro}: zero consumers outside its own definition ({loc}) "
                "-- a consumer looks like '#if " + macro + "', "
                "'#ifdef " + macro + "', or a direct reference in an "
                "expression; add one or remove the flag"
            )
        else:
            pass_lines.append(f"{macro} ({len(consumers)} consumer(s))")

    if violations:
        print(f"FAIL: {len(violations)} violation(s):")
        for v in violations[:20]:
            print(f"  {v}")
        if len(violations) > 20:
            print(f"  ... and {len(violations) - 20} more")
        return 1

    print(f"PASS: {', '.join(pass_lines)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ScanError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(2)
