#!/usr/bin/env python3
"""scripts/check_no_heap_or_64bit_symbols.py -- Phase 155 Plan 02 / DEAD-01,
DEAD-03 link-time symbol-absence gate.

Requirements: DEAD-01, DEAD-03

Asserts, by reading the LINKED ELF's symbol table (never source text), that
none of the AVR targets named in the recorded baseline's `avr_targets` keys
carries a heap-allocator symbol or a 64-bit-runtime-division symbol. DEAD-01
and DEAD-03 are link-time properties: a source grep for `malloc(` or a
`uint64_t` declaration cannot see the linker. It would still pass while the
allocator was pulled in by a different translation unit, and it fails open
on inlining and renaming -- exactly what a size-reduction phase does. Only
the linked `.elf` can witness symbol absence. No script or test in either
repository has ever invoked a toolchain binary before this module; that
narrow capability is new and is documented below rather than silently
assumed.

**Why the listing is parsed in Python, not a shelled counting grep.** A
counting `grep -c PATTERN` prints `0` and *exits 1* when nothing matches --
and nothing matching is exactly this gate's SUCCESS case. Wrapping that in
`|| true` (155-RESEARCH.md's own illustrative sketch) throws away the one
signal that distinguishes "avr-nm itself failed" from "avr-nm found zero
matches", which is precisely the fail-open/fail-closed inversion this module
exists to avoid. Every symbol-table line is instead parsed with `str.split()`
in this module, and the resulting exit code is chosen deliberately by this
script's own logic, never inherited from a shell pipeline's exit status.

**This is not backlog 999.15 / gh#8's "avr-nm symbol capture" todo.**
`platformio.ini` records that todo as deliberately not attempted (Phase 119).
Locating and invoking the AVR toolchain here is a narrow, unrelated use --
proving the absence of ELEVEN named symbols across three ELFs -- and must
not be presented as discharging that broader, differently-scoped backlog
item.

**OQ-2 (locked): all ELEVEN 64-bit symbols are asserted, not the eight
DEAD-03's requirement text names.** The eight named symbols (`__muldi3`,
`__muldi3_6`, `__umoddi3`, `__udivdi3`, `__udivdi3_umoddi3`, `__udivmod64`,
`__lshrdi3`, `__adddi3`) sum to exactly 438 B. But the image contains three
more symbols in the SAME contiguous 0x6036-0x6246 blob that the requirement
text omits: `__umulsidi3` (2 B), `__umulsidi3_helper` (84 B) and `__ashrdi3`
(4 B) -- 90 B total. The full eleven-symbol blob sums to exactly 528 B. A
gate over only the eight named symbols could therefore PASS while 90 B of
64-bit runtime is still linked, if some future caller pulled only
`__umulsidi3_helper` (or either of its two companions) back in. This module
asserts on all eleven for that reason; both totals (438 B named-subset,
528 B full blob) are recorded here as this gate's own documentation, per
`.planning/v1.33/155-before-figures.md` section 4.

One env seam, read with a committed default and overridable by an argv flag
(argv wins over env, env wins over the default):

  - `FIRESTARTER_PIO_BUILD_ROOT` (default `<repo>/.pio/build`). It exists for
    the recorded reason that `firestarter/.gitignore`
    line 1 is the bare pattern `.pio`, which matches at any depth, so a
    committed fixture cannot live under a real `.pio/build/...` path.

One new seam, with no precedent in either repository because locating an
AVR toolchain binary from a gate script is itself new:

  - `FIRESTARTER_AVR_NM` (default
    `$HOME/.platformio/packages/toolchain-atmelavr/bin/avr-nm`). This module
    never falls back to a bare `avr-nm` resolved off `$PATH` -- the seam's
    own default is an absolute path, and if that path is not present and
    executable, that IS the failure (exit 2), not a silent PATH search.

A fourth seam, `--nm-output TARGET=PATH` (repeatable), lets the gate read a
committed, captured TEXT listing for a target instead of invoking the
toolchain at all. This is the seam the paired pytest uses to stay hermetic
in CI leg 3, where no AVR toolchain need exist -- mirroring the dominant
fixture family in this repo (committed text captures of tool output, never
binaries). No binary ELF fixture is committed.

**House convention: a manual argv parser, not argparse.** (`check_erase_no_vpp.py`
uses `argparse` instead -- both precedents exist in this repo.)

Exit codes:
  0 -- every resolved target's listing contains zero heap-set matches, zero
       64-bit-set matches, and both required non-vacuity anchors (PASS:,
       naming every target compared, its counts, and its listing source).
  1 -- at least one heap-set or 64-bit-set symbol was found, on any target
       (FAIL:, one indented line per violation naming the target, the
       symbol, its type and its size) -- OR the resolved target list is
       genuinely empty (the never-vacuous guard: a gate that requires
       nothing must not report success).
  2 -- fail-closed: the baseline could not be read/parsed, is not a JSON
       object, has no object-valued `avr_targets` key; a named `--nm-output`
       listing is missing or unreadable; an ELF is missing under the build
       root; `avr-nm` could not be resolved/invoked or exited non-zero; a
       zero-violation target is missing a required non-vacuity anchor
       (proving nothing, because the listing scanned may not be the real
       firmware); or the CLI invocation itself is malformed (unknown flag,
       a flag missing its value, or a malformed `--nm-output` value) -- a
       tool/format failure, categorically distinct from a real violation,
       and never silently reported as a pass.

Anti-hollow contract: this checker's mandatory paired pytest is
`tests/test_check_no_heap_or_64bit_symbols.py`, which invokes this script as
a real subprocess (list-form argv, never `shell=True`) against the committed
fixture `tests/fixtures/planted_no_heap_or_64bit_symbols_prechange_uno/avr-nm-uno.txt`
-- the VERBATIM `avr-nm` stdout for the real pre-change `uno` image, captured
at `FW_PRE_SHA` `2ad5b322a37ba4a88afd09cc946f5c4114e51483`, containing
exactly 7 heap-set matches and 11 sixty-four-bit-set matches and both
anchors. A passing pytest suite proves THIS script goes RED against a real
pre-change listing and reaches exit 0 on a clean one -- not merely that a
test asserts it should.

Non-claim: a green run proves that, for every target compared, the scanned
listing contains no heap-allocator symbol, no 64-bit-runtime-division
symbol, and both `mem_util_blank_check` and `rurp_read_voltage_mv` are
present in it. It proves nothing about whether that listing was produced by
a clean build, whether the underlying source was actually edited to remove
the allocator/64-bit calls (a symbol can vanish from a listing for reasons
unrelated to source correctness), or any board's runtime behaviour --
no AVR board is read by this script.

Usage:
    python3 scripts/check_no_heap_or_64bit_symbols.py
    python3 scripts/check_no_heap_or_64bit_symbols.py --build-root .pio/build
    python3 scripts/check_no_heap_or_64bit_symbols.py --nm-output uno=tests/fixtures/planted_no_heap_or_64bit_symbols_prechange_uno/avr-nm-uno.txt
    python3 scripts/check_no_heap_or_64bit_symbols.py --nm /path/to/avr-nm
"""
import json
import os
import subprocess
import sys
from pathlib import Path

# Resolve the repo root from this file's location so the gate behaves
# identically regardless of the caller's working directory.
REPO_ROOT = Path(__file__).resolve().parent.parent

# Accepted and ignored: the size baseline this once read is deleted. Kept only
# so an existing --baseline argument or FIRESTARTER_SIZE_BASELINE export stays
# non-fatal.
FIRESTARTER_SIZE_BASELINE = os.environ.get("FIRESTARTER_SIZE_BASELINE")

# A
# fixture tree cannot contain a real .pio/build/... layout because
# .gitignore line 1 is the bare pattern ".pio", which matches at any depth.
FIRESTARTER_PIO_BUILD_ROOT = os.environ.get(
    "FIRESTARTER_PIO_BUILD_ROOT", str(REPO_ROOT / ".pio" / "build")
)

# New seam -- no precedent in either repository (module docstring). Default
# is an absolute path; this module never falls back to a bare "avr-nm"
# resolved off $PATH.
FIRESTARTER_AVR_NM = os.environ.get(
    "FIRESTARTER_AVR_NM",
    os.path.expanduser("~/.platformio/packages/toolchain-atmelavr/bin/avr-nm"),
)

# The single place the invocation form lives, so a committed listing and a
# live listing are always the same shape.
NM_ARGS = ("--print-size", "--size-sort", "-C")

# The heap set: the four allocator entry points plus the five avr-libc
# allocator globals (.bss/.data). realloc/calloc are asserted absent too,
# even though 155-before-figures.md section 3 confirms they are ALREADY
# absent today -- a future caller pulling either back in must also trip
# this gate.
HEAP_SYMBOLS = frozenset(
    {
        "malloc",
        "free",
        "realloc",
        "calloc",
        "__brkval",
        "__flp",
        "__malloc_heap_start",
        "__malloc_heap_end",
        "__malloc_margin",
    }
)

# The 64-bit runtime set -- ALL ELEVEN symbols in the contiguous
# 0x6036-0x6246 blob (155-before-figures.md section 4), not the eight
# DEAD-03's requirement text names. See "OQ-2 (locked)" in the module
# docstring above for the 438 B / 528 B / 90 B accounting this comment
# summarises: the eight named symbols sum to 438 B; the three this gate
# additionally asserts -- __umulsidi3 (2 B), __umulsidi3_helper (84 B),
# __ashrdi3 (4 B) -- add the remaining 90 B, for a full-blob total of 528 B.
DI64_SYMBOLS = frozenset(
    {
        "__muldi3",
        "__muldi3_6",
        "__umulsidi3",
        "__umulsidi3_helper",
        "__umoddi3",
        "__udivdi3",
        "__udivdi3_umoddi3",
        "__udivmod64",
        "__ashrdi3",
        "__lshrdi3",
        "__adddi3",
    }
)

# Non-vacuity anchors (check_erase_no_vpp.py's rationale, restated): a zero
# count on a listing that is not the real firmware proves nothing. Both
# must be present in a target's listing before a zero-violation result on
# that target is trusted as PASS.
REQUIRED_ANCHORS = frozenset({"mem_util_blank_check", "rurp_read_voltage_mv"})


class GateError(Exception):
    """Fail-closed: raised for every exit-2 condition below (missing ELF,
    unresolvable avr-nm, unreadable/missing --nm-output listing, a
    non-zero avr-nm invocation, or a missing non-vacuity anchor). Caught
    only in main(), converted to an ERROR: message and exit 2 -- never a
    silent pass."""


def _parse_argv(argv):
    """Manual argv parser -- house convention (check_erase_no_vpp.py uses
    argparse instead; both precedents exist in this repo).

    Recognises --baseline PATH, --build-root PATH, --nm PATH, and a
    repeatable --nm-output TARGET=PATH. Raises SystemExit(2) on a malformed
    invocation (unknown flag, a flag missing its value, or a malformed
    --nm-output value) -- a CLI usage error is itself a tool/format
    failure, not a symbol-presence finding.
    """
    baseline = None
    build_root = None
    nm = None
    nm_output = {}
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--baseline":
            i += 1
            if i >= len(argv):
                print("ERROR: --baseline requires a PATH argument", file=sys.stderr)
                raise SystemExit(2)
            baseline = argv[i]
        elif arg == "--build-root":
            i += 1
            if i >= len(argv):
                print("ERROR: --build-root requires a PATH argument", file=sys.stderr)
                raise SystemExit(2)
            build_root = argv[i]
        elif arg == "--nm":
            i += 1
            if i >= len(argv):
                print("ERROR: --nm requires a PATH argument", file=sys.stderr)
                raise SystemExit(2)
            nm = argv[i]
        elif arg == "--nm-output":
            i += 1
            if i >= len(argv):
                print("ERROR: --nm-output requires a TARGET=PATH argument", file=sys.stderr)
                raise SystemExit(2)
            value = argv[i]
            target, sep, path = value.partition("=")
            if not sep or not target or not path:
                print(
                    f"ERROR: --nm-output value must be TARGET=PATH, got: {value!r}",
                    file=sys.stderr,
                )
                raise SystemExit(2)
            nm_output[target] = path
        else:
            print(f"ERROR: unrecognized argument: {arg}", file=sys.stderr)
            raise SystemExit(2)
        i += 1
    return {"baseline": baseline, "build_root": build_root, "nm": nm, "nm_output": nm_output}


def _resolve_avr_nm(path_str):
    """Return the resolved, executable avr-nm Path, or raise GateError
    (fail closed) naming the path tried. Never falls back to a bare
    "avr-nm" resolved off $PATH -- the seam's own default is an absolute
    path, and an absent/non-executable file there IS the failure."""
    p = Path(path_str)
    if not p.is_file() or not os.access(p, os.X_OK):
        raise GateError(
            f"avr-nm not found or not executable at {p} "
            "(set FIRESTARTER_AVR_NM or pass --nm)"
        )
    return p


def _listing_for_target(target, nm_output, build_root, nm_path):
    """Return (listing_text, source_description) for `target`.

    Two modes. If `--nm-output` names this target, read that committed text
    listing and never invoke the toolchain. Otherwise resolve
    `<build_root>/<target>/firestarter_<target>.elf`, fail closed if it is
    missing/unreadable, and run avr-nm with NM_ARGS via list-form
    subprocess.run -- never a shell. A non-zero return from the tool is a
    GateError (exit 2), not a violation (exit 1).
    """
    if target in nm_output:
        listing_path = Path(nm_output[target])
        if not listing_path.is_file():
            raise GateError(f"{target}: --nm-output listing not found: {listing_path}")
        try:
            return listing_path.read_text(encoding="utf-8"), f"--nm-output {listing_path}"
        except OSError as e:
            raise GateError(f"{target}: could not read --nm-output listing {listing_path}: {e}")

    elf_path = Path(build_root) / target / f"firestarter_{target}.elf"
    if not elf_path.is_file():
        raise GateError(f"{target}: ELF not found: {elf_path}")

    try:
        result = subprocess.run(
            [str(nm_path), *NM_ARGS, str(elf_path)],
            capture_output=True,
            text=True,
        )
    except OSError as e:
        raise GateError(f"{target}: could not invoke avr-nm ({nm_path}) on {elf_path}: {e}")

    if result.returncode != 0:
        raise GateError(
            f"{target}: avr-nm ({nm_path}) exited {result.returncode} on {elf_path}: "
            f"{result.stderr.strip()}"
        )
    return result.stdout, str(elf_path)


def _scan_listing(text):
    """Parse an avr-nm --print-size --size-sort -C listing line by line.

    Splits each line on whitespace; the symbol NAME is the last field and
    the TYPE is the field before it. Any type other than "U" (undefined
    reference) is treated as a definition; undefined entries are never
    counted as present -- a symbol only referenced, never defined, in this
    listing does not prove the definition is linked.

    Returns (heap_found, di64_found, anchors_found): the first two are
    dicts of {name: (type, size_or_None)} for every matched forbidden
    symbol; the third is the set of REQUIRED_ANCHORS names found.
    """
    heap_found = {}
    di64_found = {}
    anchors_found = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        name = parts[-1]
        sym_type = parts[-2]
        if sym_type == "U":
            continue
        size = None
        if len(parts) >= 4:
            try:
                size = int(parts[-3], 16)
            except ValueError:
                size = None
        if name in HEAP_SYMBOLS:
            heap_found[name] = (sym_type, size)
        if name in DI64_SYMBOLS:
            di64_found[name] = (sym_type, size)
        if name in REQUIRED_ANCHORS:
            anchors_found.add(name)
    return heap_found, di64_found, anchors_found


AVR_TARGETS = ("uno", "uno328pb", "leonardo")


def _resolve_targets(nm_output, baseline_path=None):
    """Resolve the target list: from --nm-output entries if given, otherwise
    the AVR targets this repo builds. Returns (targets_sorted, error_or_None).

    Previously read the `avr_targets` keys of scripts/baseline/size_baseline.json.
    That file is gone, along with the size-baseline gate it served, so the set is
    stated here directly. `baseline_path` is accepted and ignored so the existing
    --baseline flag and FIRESTARTER_SIZE_BASELINE env seam stay non-fatal."""
    if nm_output:
        return sorted(nm_output.keys()), None
    return sorted(AVR_TARGETS), None

    return sorted(avr_targets.keys()), None


def main(argv):
    args = _parse_argv(argv)

    baseline_path = args["baseline"] or FIRESTARTER_SIZE_BASELINE
    build_root = args["build_root"] or FIRESTARTER_PIO_BUILD_ROOT
    nm_arg = args["nm"] or FIRESTARTER_AVR_NM
    nm_output = args["nm_output"]

    targets, err = _resolve_targets(nm_output, baseline_path)
    if err is not None:
        print(f"ERROR: {err}")
        return 2

    # Never-vacuous guard, run BEFORE the per-target loop: a gate that
    # requires nothing must not print PASS:.
    if not targets:
        print(
            "FAIL: no targets resolved (baseline 'avr_targets' parsed empty, and no "
            "--nm-output given) -- never-vacuous guard (a gate that requires nothing "
            "must not report success)"
        )
        return 1

    # avr-nm is only needed for targets NOT covered by --nm-output. Resolve
    # it lazily so a fully --nm-output-covered invocation (the pytest
    # module's hermetic path) never requires the toolchain to be present.
    nm_path = None
    if any(t not in nm_output for t in targets):
        try:
            nm_path = _resolve_avr_nm(nm_arg)
        except GateError as e:
            print(f"ERROR: {e}")
            return 2

    violations = []
    anchor_failures = []
    sources = {}
    for target in targets:
        try:
            text, source = _listing_for_target(target, nm_output, build_root, nm_path)
        except GateError as e:
            print(f"ERROR: {e}")
            return 2
        sources[target] = source

        heap_found, di64_found, anchors_found = _scan_listing(text)
        for name, (sym_type, size) in sorted(heap_found.items()):
            violations.append((target, name, "heap", sym_type, size))
        for name, (sym_type, size) in sorted(di64_found.items()):
            violations.append((target, name, "64bit", sym_type, size))

        missing = REQUIRED_ANCHORS - anchors_found
        if missing:
            anchor_failures.append((target, sorted(missing)))

    # Violations are reported UNCONDITIONALLY and FIRST -- ahead of the
    # anchor check -- mirroring check_erase_no_vpp.py's ordering rule.
    if violations:
        print(f"FAIL: {len(violations)} forbidden symbol(s) found:")
        for target, name, kind, sym_type, size in violations:
            size_str = f"{size} B" if size is not None else "size unknown"
            print(f"  {target}: {name} ({kind}, type {sym_type}, {size_str})")
        return 1

    # Only on the otherwise-clean path do we check the anchors: a
    # zero-violation result on a listing that is not the real firmware
    # proves nothing (check_erase_no_vpp.py's non-vacuity-anchor rationale).
    if anchor_failures:
        for target, missing in anchor_failures:
            print(
                f"ERROR: {target}: missing required non-vacuity anchor(s) {missing} in "
                f"listing {sources[target]} -- a zero count on a listing that is not the "
                "real firmware proves nothing"
            )
        return 2

    parts = ", ".join(
        f"{t}(heap=0,64bit=0,anchors={len(REQUIRED_ANCHORS)}/{len(REQUIRED_ANCHORS)},"
        f"source={sources[t]})"
        for t in targets
    )
    resolved_nm = str(nm_path) if nm_path is not None else "(not needed -- all targets via --nm-output)"
    print(f"PASS: {parts} (build_root={build_root}, avr-nm={resolved_nm})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
