#!/usr/bin/env python3
"""scripts/check_erase_no_vpp.py -- Phase 153 / ERASE-04 GATE-03 primary
control: a brace-matched negative source scan proving
`eeprom28c_erase_execute`'s body never reaches a VPP/VPE control-register
write, a chip-enable/disable bracket, or a bus-config-bypassing flash_utils
helper.

Requirements: ERASE-04
Decisions covered: D-153-03

**The mechanism correction this checker exists to fix (D-153-03).** The
ROADMAP's criterion 3 implies the host repository's `tools/check_dispatch.py`
(the Python-host sibling project) is what stops the datasheet's hardware
Chip Erase mode (12V on the OE pin,
AT28C256 DS20006386B Table 6-1) from reaching a `0x0D` chip. It is not, and
it cannot be: `check_dispatch.py`'s GATE-03 guard fires only on a
`handler == "configure_eprom"` paired with a no-VPP-pin pinout
(`no_vpp_pin_pinouts`, built from the pinout file) -- a database-and-
dispatch-table scan. It is structurally unable to observe a control-register
write inside a C++ handler body; `eeprom28c_erase_execute` is entirely
outside that Python checker's field of view. **A green `check_dispatch.py`
must never be cited as a VPP proof for this operation.** This module is the
primary control instead: a brace-matched scan of the erase body's own
source text.

**Proximity, not absence, was the risk -- the copy source is now gone.** The
flash4 (`0x05`) handler no longer carries a bulk-erase routine to copy from:
`flash_5v_page.cpp` asserts no control-register bit at all. A legitimate
high-voltage erase does still exist elsewhere -- `eprom_internal_erase` in
`src/proms/eprom.cpp` brackets `CTRL_VPP_REGULATOR_ENABLE | CTRL_VPE_ENABLE`
with `rurp_chip_enable()` / `rurp_chip_disable()` for UV-EPROM parts that
require it -- so this is not a claim that the shape is absent from the tree,
only that the handler adjacent to `eeprom_28c.cpp` no longer models it. The gate stays armed regardless, because the hazardous shape is still
describable and still reachable by anyone re-deriving it from the AT28C256
datasheet's own hardware Chip Erase mode and copying it into
`eeprom_28c.cpp`'s erase handler by mistake -- exactly the failure mode this
checker exists to catch, which is why the scan is body-scoped rather than a
whole-file zero-occurrence grep: `eeprom_28c.cpp` legitimately contains the
same tokens in `eeprom28c_check_chip_id` (the A9-12V chip-identification
path), so a file-wide scan would be RED on arrival against a function this
checker is not meant to guard.

**Body-scoped, not file-wide.** `--function` (default
`eeprom28c_erase_execute`) is located by its definition (signature followed
by `{`, never the semicolon-terminated forward declaration a few lines
above it in the real file) and brace-matched to its closing `}`. Only that
span is scanned.

**Comments are scanned too, deliberately.** Unlike this checker's sibling in
the host repository's tooling, `check_no_log_in_sdp_window.py` (which blanks
comments before scanning), this checker does NOT strip comments from the hazard
scan: a comment naming a hazard token inside the matched body is
indistinguishable, at grep distance, from a real call, and this project has
already been bitten by comment text invalidating its own gate. Comments
ARE still blanked for the purpose of locating the function definition and
brace-matching its body (so a stray `{` or `}` inside a comment cannot
desynchronize the brace counter) -- but the reported violation text is read
back from the original, uncleaned source, at the same character offsets
(the comment-blanking transform is length- and newline-preserving, so
offsets never drift between the two views).

**Non-vacuity anchor -- but only on the PASS path.** A checker that can
only ever pass proves nothing (v1.12's hollow GATE-03 class). Before this
module reports a clean body as a PASS, it requires the matched body to
contain at least one `handle->firestarter_set_data(` call and at least one
`delay(AT28C_TEC_MAX_MS)` call -- the two source-level fingerprints of the
real AN-0544B six-byte software chip erase this checker exists to guard.
Hazard-token violations are reported unconditionally, ahead of the anchor
check: `eeprom28c_check_chip_id` (the discrimination fixture) has neither
anchor either, but it is still reported as a violation rather than an
anchor failure, because the token scan runs first and finds real hazard
tokens in that body. The anchor check only gates the *empty* or *wrong*
body case -- one that reports no violations only because it is not the
erase op at all.

**Fails closed, never open.** A missing source file, an unresolvable
function definition, an unbalanced brace count, or a failed non-vacuity
anchor is never a silent pass: each prints an `ERROR:` message and exits 2.
A hazard-token violation prints a `FAIL:` message and exits 1 -- a
different exit code from every fail-closed case, so the paired pytest
(`tests/test_check_erase_no_vpp.py`) can tell "found a real hazard" apart
from "could not render a trustworthy verdict at all", mirroring
`check_no_log_in_sdp_window.py`'s own `ERROR:` versus `FAIL:` distinction.

**No dependency, no other-repo scan.** stdlib only. This module never reads
anything under the Python-host sibling repository's tree and never weakens,
exempts, or re-baselines anything in its `tools/` directory -- the host
repo's `check_dispatch.py` stays byte-unchanged by this phase;
`git diff --quiet -- tools/check_dispatch.py`, run from that repository,
must hold at phase end, independently of this checker's own result.

**No CI leg.** This module -- and its paired pytest -- executes in NO CI
leg on this branch: `build.yml` and `beta-build.yml` run only
`pio test -e native` and `pio test -e native_nodevtools`; neither invokes
`pytest` over `firestarter/tests/` at all. The phase record (this plan's
SUMMARY.md) is the only evidence this checker was ever exercised, exactly
like `check_size_baseline.py`'s own standing disclosure.

Path resolution is always relative to this script's own location, never to
the caller's current working directory -- `--source` (default
`src/proms/eeprom_28c.cpp`) and any value passed to it are resolved against
the repository root derived from `__file__`, so `python3
/path/to/scripts/check_erase_no_vpp.py` behaves identically regardless of
the shell's cwd.

Exit codes:
  0 -- the matched body contains zero hazard-token occurrences AND both
       non-vacuity anchors are present (PASS:, naming the file, the
       function, the resolved body line range and the scanned line count).
  1 -- at least one hazard-token occurrence was found in the matched body
       (FAIL:, per-violation summary with line numbers and the offending
       source line).
  2 -- fail-closed: the source file is missing or unreadable, the function
       definition could not be located or brace-balanced, or (only when
       zero hazard tokens were found) a required non-vacuity anchor is
       absent (ERROR:, naming the specific failure).

Usage:
    python3 scripts/check_erase_no_vpp.py
    python3 scripts/check_erase_no_vpp.py --function eeprom28c_check_chip_id
    python3 scripts/check_erase_no_vpp.py --source tests/fixtures/planted_erase_no_vpp_ctrl_write.cpp
"""

import argparse
import re
import sys
from pathlib import Path

# Resolve the repo root from this file's own location, never from the
# caller's cwd (mirrors check_orphan_provisional.py:163,
# check_cmake_manifest.py:110).
_SCRIPT_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _SCRIPT_DIR.parent

_DEFAULT_SOURCE = "src/proms/eeprom_28c.cpp"
_DEFAULT_FUNCTION = "eeprom28c_erase_execute"

# The hazard tokens: VPP/VPE control-register bit prefixes, the
# control-register setter, the chip-enable/disable bracket calls, and the
# two bus-config-bypassing flash_utils helper prefixes (see the module
# docstring's "Proximity, not absence" section). Each is a (label, compiled
# pattern) pair; label is used verbatim in violation output.
_HAZARD_PATTERNS = (
    ("CTRL_VPE", re.compile(r"\bCTRL_VPE\w*")),
    ("CTRL_VPP", re.compile(r"\bCTRL_VPP\w*")),
    ("firestarter_set_control_register", re.compile(r"\bfirestarter_set_control_register\b")),
    ("rurp_chip_enable", re.compile(r"\brurp_chip_enable\b")),
    ("rurp_chip_disable", re.compile(r"\brurp_chip_disable\b")),
    ("fu_flash_", re.compile(r"\bfu_flash_\w*")),
    ("flash_util_", re.compile(r"\bflash_util_\w*")),
)

# Non-vacuity anchors -- both must be present in the matched body before a
# zero-violation result is trusted as a PASS (see module docstring).
_ANCHOR_SET_DATA = re.compile(r"handle->firestarter_set_data\s*\(")
_ANCHOR_TEC_DELAY = re.compile(r"\bdelay\s*\(\s*AT28C_TEC_MAX_MS\s*\)")


def _func_def_pattern(func_name: str) -> "re.Pattern[str]":
    """Build a function-DEFINITION-only pattern (body-opening `{`), never
    matching a `;`-terminated forward declaration. Tolerates an optional
    leading `static` -- every target function in this module (the erase
    op, the chip-id check, the lock op) is declared `static`."""
    return re.compile(r"\b(?:static\s+)?void\s+" + re.escape(func_name) + r"\s*\([^)]*\)\s*\{")


def _strip_comments(text: str) -> str:
    """Blank `//` and `/* */` comment spans, preserving length and newline
    positions, so brace-matching (which runs on this cleaned view) cannot be
    desynchronized by a brace inside a comment, while every character
    offset still maps 1:1 onto the original `text` -- the hazard/anchor
    scan deliberately runs on the ORIGINAL text at those same offsets (see
    module docstring)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i : i + 2]
        if two == "//":
            j = text.find("\n", i)
            if j == -1:
                j = n
            out.append(" " * (j - i))
            i = j
        elif two == "/*":
            j = text.find("*/", i + 2)
            if j == -1:
                j = n
            else:
                j += 2
            out.append("".join(c if c == "\n" else " " for c in text[i:j]))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def _line_of(text: str, pos: int) -> int:
    return text.count("\n", 0, pos) + 1


class ResolveError(Exception):
    """Fail-closed: the function definition could not be located or
    brace-balanced. Caught only at the entry point, converted to an
    ERROR: message and exit 2 -- never a silent pass."""


def find_function_body(source_text: str, func_name: str) -> tuple[int, int]:
    """Locate `func_name`'s definition in `source_text` and brace-match its
    body. Returns `(body_start, body_end)` character offsets into
    `source_text` (inclusive of both braces). Raises `ResolveError`
    (fail-closed) if the definition cannot be found or the braces do not
    balance -- brace-matching itself runs against a comment-blanked view so
    a brace inside a comment cannot desynchronize the depth counter, but
    the returned offsets index into `source_text` unchanged (the blanking
    transform is length-preserving)."""
    cleaned = _strip_comments(source_text)
    m = _func_def_pattern(func_name).search(cleaned)
    if m is None:
        raise ResolveError(
            f"function definition for {func_name}() not found (or is only "
            "forward-declared, never defined) in the scanned source"
        )
    depth = 0
    body_start = m.end() - 1  # position of the matched '{'
    i = body_start
    n = len(cleaned)
    while i < n:
        ch = cleaned[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return body_start, i
        i += 1
    raise ResolveError(f"{func_name}() body is not brace-balanced -- unbalanced braces from offset {body_start}")


def scan_hazards(source_text: str, body_start: int, body_end: int) -> list[tuple[int, str, str]]:
    """Scan `source_text[body_start:body_end+1]` (inclusive, COMMENTS
    INCLUDED deliberately -- see module docstring) for every hazard token
    in `_HAZARD_PATTERNS`. Returns a list of `(line_no, label, line_text)`
    tuples, sorted by source position, one entry per match."""
    body_text = source_text[body_start : body_end + 1]
    hits: list[tuple[int, str]] = []
    for label, pattern in _HAZARD_PATTERNS:
        for m in pattern.finditer(body_text):
            hits.append((body_start + m.start(), label))
    hits.sort(key=lambda pair: pair[0])

    source_lines = source_text.splitlines()
    violations = []
    for abs_pos, label in hits:
        line_no = _line_of(source_text, abs_pos)
        line_text = source_lines[line_no - 1].strip() if 0 < line_no <= len(source_lines) else ""
        violations.append((line_no, label, line_text))
    return violations


def check_anchors(source_text: str, body_start: int, body_end: int) -> list[str]:
    """Return the list of missing anchor names (empty if both are present)
    for `source_text[body_start:body_end+1]`."""
    body_text = source_text[body_start : body_end + 1]
    missing = []
    if _ANCHOR_SET_DATA.search(body_text) is None:
        missing.append("handle->firestarter_set_data(")
    if _ANCHOR_TEC_DELAY.search(body_text) is None:
        missing.append("delay(AT28C_TEC_MAX_MS)")
    return missing


def _resolve_source_path(source_arg: str) -> Path:
    """Resolve `--source` against the repository root derived from this
    script's own location -- never against the caller's cwd (module
    docstring)."""
    p = Path(source_arg)
    return p if p.is_absolute() else (_REPO_ROOT / p)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else "")
    parser.add_argument(
        "--source",
        default=_DEFAULT_SOURCE,
        help=f"source file to scan, resolved against the repo root (default: {_DEFAULT_SOURCE})",
    )
    parser.add_argument(
        "--function",
        default=_DEFAULT_FUNCTION,
        help=f"function to brace-match and scan (default: {_DEFAULT_FUNCTION})",
    )
    args = parser.parse_args()

    source_path = _resolve_source_path(args.source)
    if not source_path.is_file():
        print(f"ERROR: source file not found: {source_path}", file=sys.stderr)
        return 2
    try:
        source_text = source_path.read_text(encoding="utf-8")
    except OSError as e:
        print(f"ERROR: could not read source file {source_path}: {e}", file=sys.stderr)
        return 2

    try:
        body_start, body_end = find_function_body(source_text, args.function)
    except ResolveError as e:
        print(f"ERROR: {e} ({source_path})", file=sys.stderr)
        return 2

    start_line = _line_of(source_text, body_start)
    end_line = _line_of(source_text, body_end)
    scanned_lines = end_line - start_line + 1

    violations = scan_hazards(source_text, body_start, body_end)
    if violations:
        print(
            f"FAIL: {len(violations)} hazard token(s) found in {args.function}()'s "
            f"body ({source_path}, lines {start_line}-{end_line}):"
        )
        for line_no, label, line_text in violations[:20]:
            print(f"  line {line_no}: [{label}] {line_text}")
        if len(violations) > 20:
            print(f"  ... and {len(violations) - 20} more")
        return 1

    missing_anchors = check_anchors(source_text, body_start, body_end)
    if missing_anchors:
        print(
            f"ERROR: anchor check failed for {args.function}() ({source_path}, "
            f"lines {start_line}-{end_line}): zero hazard tokens found, but this "
            f"body is missing required non-vacuity anchor(s) {missing_anchors} -- "
            "it is not the AT28C erase op this checker exists to guard, so a "
            "clean scan here would prove nothing",
            file=sys.stderr,
        )
        return 2

    print(
        f"PASS: {args.function}() in {source_path} (lines {start_line}-{end_line}, "
        f"{scanned_lines} lines scanned) contains no VPP/VPE control-register, "
        "chip-enable/disable, or bus-config-bypassing hazard token"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
