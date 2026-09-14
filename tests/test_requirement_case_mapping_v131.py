"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 144 Plan 01 (D-01) -- authors the machine-checked requirement->case
mapping gate: a pytest module that parses the three v131 Unity suites under
test/native/avr/, extracts every RUN_TEST(...) case name, and asserts that
each of TEST-01...TEST-05 names cases which actually exist.

Requirements: TEST-01, TEST-02, TEST-03, TEST-04, TEST-05 (this plan does
NOT mark any of them Complete -- 144-01-PLAN.md's own scope note assigns
all eight requirement flips of Phase 144 to plan 144-07; this module is
evidence for that later flip, not the flip itself).

Defect class this closes: a requirement flipped Complete against a native
case that is later renamed or deleted, with nothing re-checking that the
case named in prose still exists. 144-CONTEXT.md's own D-01 names the
concrete instance: CONTEXT.md's prose nominates "the two fallback cases"
for TEST-05, which names no existing pair at all -- the real shape is SIX
cases in two families of three (C-04, corrected in _REQUIREMENT_CASES
below). A prose-only mapping table is the same shape as the hollow parity
legs a prior phase (120) had to rebuild -- this module exists so the map is
machine-checked instead.

This module maps requirements onto pre-existing coverage; it authors no new
native RUN_TEST case and edits no file under src/ (D-04). The 88 existing
cases across the three suites mapped here (test_loop_eprom_v131 47,
test_vpp_eprom_v131 32, test_eprom_params_v131 9) were authored across
Phases 140 and 141 and are exercised by `pio test -e native_loop_v131` and
`pio test -e native_params_v131` -- neither of which this module invokes;
this module scans their SOURCE TEXT only, independently of whatever a
PlatformIO run reports, so a case that compiles but was silently renamed in
a way that still compiles is still caught here.

Coverage:
  1. test_every_mapped_requirement_names_only_existing_cases -- D-01's core
     assertion: every case name each of TEST-01...TEST-05 is mapped against
     is proven to exist in the union of RUN_TEST names extracted from the
     three mapped suites. A missing case fails naming the requirement id,
     the missing case name, and the mapped-suite files scanned -- never a
     bare "lists differ".
  2. test_each_mapped_suite_meets_its_hardcoded_case_floor -- per-suite
     extracted count >= its hardcoded floor (47 / 32 / 9). A future
     deletion that still leaves every MAPPED case present (because the
     deleted case belonged to none of TEST-01...05) would slip past
     Coverage 1 alone; this floor catches it.
  3. test_extracted_case_names_are_unique -- a duplicate RUN_TEST name
     would silently inflate a suite's extracted count and could mask an
     unrelated rename hiding behind the duplicate's presence.
  4. test_trace_suite_is_deliberately_out_of_scope -- test_trace_eprom_v131
     is asserted absent from _MAPPED_SUITES, and (when its source is
     reachable under the current scan target) its source is asserted to
     still contain the #ifdef guard this exclusion's reasoning depends on --
     so the exclusion reason stays machine-checked rather than becoming
     folklore. test_trace_eprom_v131's sixth RUN_TEST sits inside
     `#ifdef EPROM_V131_TRACE_DUMP`, which no env defines (C-05); it proves
     TEST-06, not TEST-01...05, and belongs to a different plan entirely.
  5. test_scan_targets_are_non_vacuous -- self-protection, two halves. Part
     (a) recomputes the DEFAULT scan root fresh from _REPO_ROOT WITHOUT
     reading os.environ at all, and checks it is sane. Part (b) runs the
     extractor against whatever _SCAN_SUITES CURRENTLY resolves to (the
     SAME seam-aware target every leg above scans) and requires the union
     to meet the hardcoded _TOTAL_FLOOR (88) -- this is the half an
     emptied or misdirected scan root turns RED, proving that scenario
     fails closed here instead of making every membership check in
     Coverage 1 pass vacuously over an empty set.
  6. test_this_module_cannot_be_silently_skipped -- this module's own
     source contains no skip-bypass call, no skip-marker decorator, and no
     import-or-skip call anywhere, each needle concatenation-built.
  7. test_own_needles_do_not_appear_verbatim_in_this_module -- every
     concatenation-built needle from Coverage 6 appears NOWHERE verbatim in
     this module's own source.
  8. test_planted_renamed_case_is_detected -- D-18 Plant A. A case
     TEST-02 is mapped against is renamed in a SCRATCH copy (never the
     real tree) of test_loop_eprom_v131, run through Coverage 1's own leg
     in a CHILD PROCESS via FIRESTARTER_CASE_MAP_SCAN_ROOT, and the RED
     transcript must name both the missing case and TEST-02 -- proving a
     renamed or deleted native case fails the gate with a locating
     message, not a silent pass.
  9. test_planted_emptied_scan_root_fails_the_non_vacuity_leg -- D-18
     Plant B. An EMPTY scratch scan root, run through Coverage 5's own leg
     in a CHILD PROCESS, must fail naming the hardcoded floor (88) and the
     observed count (0) -- proving an emptied or misdirected scan root
     fails the non-vacuity leg instead of making every membership check
     pass vacuously over an empty set.

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover them,
mirroring tests/test_ack_layout_source_contract_v143.py's own convention)
  - FIRESTARTER_CASE_MAP_SCAN_ROOT -- overrides the scanned suite ROOT
    (test/native/avr) ONLY -- never a floor literal, never a case name,
    never the mapped-suite set. Consulted by Coverage 1, 2, 3, 4's second
    half, and the SECOND half of Coverage 5. Binds at IMPORT time (the
    module-level `Path(os.environ.get(...))` expression below), so a
    planted-violation run must set it in a CHILD PROCESS environment
    before this module is imported, never via a post-import monkeypatch
    (monkeypatch.setenv has no effect on an already-bound module-level
    value). Coverage 8 and 9 are the two legs that set it, always through
    `_run_gate_in_subprocess`, never through `monkeypatch.setenv` (S6).
  - FIRESTARTER_144_GATE_CHILD -- a child-process recursion guard only,
    read (never written by anything but `_run_gate_in_subprocess` itself)
    to refuse spawning a nested gate subprocess from inside one. It is not
    a behavioural seam: it carries no path, no floor and no case name, and
    no test above reads it for any other purpose. Copied structurally from
    tests/test_flash_path_record_sync.py's own FIRESTARTER_129_GATE_CHILD.
  - Coverage 5's FIRST half, Coverage 6 and Coverage 7 deliberately never
    read either seam: Coverage 5's first half recomputes the default
    target directly from the repository root without ever reading
    os.environ (the check_permitted_claims.py `_HERE`-resolves-to-the-
    wrong-directory landmine, closed here by construction); 6 and 7 read
    only this module's own source. A stray seam value left set after a
    planted run cannot make Coverage 5's first half, 6 or 7 pass
    vacuously -- at worst Coverage 1-4 and Coverage 5's second half
    redirect to whatever the seam happens to point at, which fails loudly
    rather than silently.

CI framing, stated honestly: `pytest tests/ -v` appears in
`.github/workflows/build.yml:161` and `.github/workflows/beta-build.yml:134`,
so this module -- like every other file under tests/ -- WILL run in CI once
this branch reaches `main` or `beta`. It does not run in any CI leg on the
milestone branch itself; that is a statement about this branch's current CI
wiring, not a claim that this module is permanently invisible to CI.

This module is a standalone pytest module: it is not named check_*.py (see
tests/test_checker_convention.py's FLOOR=6 / FIXTURE_FLOOR=15, both at
exactly the current counts -- authoring a scripts/check_*.py checker here
would obligate raising both in the same commit, which this plan does not
do), it adds no shared pytest configuration or fixture-registration file
anywhere (firestarter/tests/ has none, by house convention, and this module
does not introduce one), and it imports nothing beyond the Python standard
library: os, re and pathlib for the mapping and self-protection legs
(Coverage 1-7); subprocess, sys and shutil for the D-18 planted-violation
legs' child-process runs and fail-closed git resolution (Coverage 8-9). It
never imports, parametrizes against, or edits any other existing test
module -- the scanning logic below is its own independent re-derivation of
this plan's own gate specification, never a shared helper.

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo; a recorded house-rule
pattern, not an omission). Stdlib and pytest only.
"""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_SUITES_REL = "test/native/avr"

# Environment seam -- binds at IMPORT time. See the module docstring's
# "Environment seams" section above. Overrides the scan ROOT only.
_SCAN_SUITES = Path(
    os.environ.get("FIRESTARTER_CASE_MAP_SCAN_ROOT", str(_REPO_ROOT / _SUITES_REL))
)

_MAPPED_SUITES = (
    "test_loop_eprom_v131",
    "test_vpp_eprom_v131",
    "test_eprom_params_v131",
)
_EXCLUDED_TRACE_SUITE = "test_trace_eprom_v131"

_SUITE_FLOORS = {
    "test_loop_eprom_v131": 47,
    "test_vpp_eprom_v131": 32,
    "test_eprom_params_v131": 9,
}
_TOTAL_FLOOR = 88  # sum(_SUITE_FLOORS.values()) -- this gate's own denominator, not 79.


def _strip_comments(text):
    """Strip `//` line comments and `/* ... */` block comments, replacing
    each stripped span with whitespace of the SAME SHAPE (a newline stays a
    newline, everything else becomes a single space) so every line number
    in the result matches the original file exactly -- copied verbatim from
    tests/test_ack_layout_source_contract_v143.py's own copy (itself
    carried from tests/test_write_path_source_contract_v131.py:203-235 via
    tests/test_hv_routing_source_contract_v142.py). The three mapped
    suites carry no string or character literal outside of a comment or an
    #include/#ifdef directive (confirmed by inspection at authoring time),
    so literal-stripping is not needed here either."""
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


# Concatenation-built needles. Coverage 7 asserts none of these appear
# verbatim anywhere in this module's own source -- see the module
# docstring: a gate that quotes its forbidden tokens verbatim matches
# itself and can never pass.
_NEEDLE_SKIP_CALL = "pytest" + ".skip"
_NEEDLE_SKIPIF_MARKER = "mark" + ".skipif"
_NEEDLE_DEPENDENCY_SKIP_CALL = "importor" + "skip"

_ALL_SELF_CHECK_NEEDLES = (
    ("a pytest skip call", _NEEDLE_SKIP_CALL),
    ("a pytest skipif marker", _NEEDLE_SKIPIF_MARKER),
    ("a pytest dependency-skip call", _NEEDLE_DEPENDENCY_SKIP_CALL),
)

_RUN_TEST_RE = re.compile(r"RUN_TEST\(\s*([A-Za-z0-9_]+)\s*\)")


def _suite_path(suite):
    """Resolve `<suite>/<suite>.cpp` beneath the CURRENT (seam-aware) scan
    root."""
    return _SCAN_SUITES / suite / f"{suite}.cpp"


def _extract_run_test_names(path):
    """Read `path`, strip C-style comments, and return the ordered list of
    RUN_TEST(...) case names -- an independent re-derivation, never
    imported from any golden or other gate module.

    A MISSING scan target yields an empty list rather than raising: the
    higher-level floor and membership checks
    (test_scan_targets_are_non_vacuous, test_every_mapped_requirement_
    names_only_existing_cases) are what turn that emptiness into a
    locating, fail-closed AssertionError naming the observed count against
    the hardcoded floor -- an uncaught FileNotFoundError here would hide
    that locating message behind a bare traceback instead."""
    if not path.is_file():
        return []
    stripped = _strip_comments(path.read_text())
    return _RUN_TEST_RE.findall(stripped)


def _extract_all_mapped_names():
    """Return {suite: [case names]} for the three mapped suites, extracted
    fresh from whatever _SCAN_SUITES currently resolves to. Never cached at
    import time -- every leg below re-derives this on each call so a
    planted scan root is picked up correctly by a child-process run."""
    return {suite: _extract_run_test_names(_suite_path(suite)) for suite in _MAPPED_SUITES}


_REQUIREMENT_CASES = {
    "TEST-01": (
        "test_each_protocol_resolves_to_its_own_distinct_row",
        "test_unknown_protocol_returns_null",
        "test_row_values_match_the_frozen_table",
    ),
    "TEST-02": (
        "test_loop01_pulse_width_never_grows_between_attempts",
        "test_loop01_each_byte_gets_exactly_the_seeded_number_of_fixed_width_pulses",
        "test_loop01_verify_read_follows_every_pulse",
        "test_loop01_a_byte_that_converges_on_its_last_permitted_pulse_succeeds",
    ),
    "TEST-03": (
        "test_loop03_overprogram_duration_is_three_times_the_pulse_count_times_the_width",
        "test_loop03_overprogram_is_zero_when_the_factor_is_zero",
        "test_loop03_overprogram_clamps_at_the_cap_rather_than_refusing",
        "test_loop03_overprogram_is_32_bit_safe_at_the_uint16_ceiling",
        "test_loop03_a_zero_cap_yields_no_overprogram_pulse",
        "test_loop04_no_live_row_emits_an_overprogram_pulse",
    ),
    "TEST-04": (
        "test_loop05_a_byte_that_misses_within_max_pulses_aborts_the_block",
        "test_loop05_the_loops_own_strobes_disable_the_high_voltage_route",
        "test_loop05_a_successful_block_does_not_disable_the_route",
        "test_vpp02_x3_the_energy_cap_exit_disables_the_route",
        "test_vpp02_x4_the_final_pass_verify_failure_disables_the_route",
        "test_vpp02_e1_write_init_error_exit_leaves_no_route_asserted",
    ),
    "TEST-05": (
        "test_loop06_an_ff_target_byte_is_never_read_and_never_pulsed",
        "test_loop06_an_already_matching_byte_is_read_once_and_never_pulsed",
        "test_loop06_a_block_of_only_skipped_bytes_emits_no_pulse_at_all",
        "test_loop06_the_ff_rule_does_not_suppress_the_final_verify_pass",
        "test_0x07_zero_pulse_delay_takes_the_1000us_fallback",
        "test_0x08_zero_pulse_delay_takes_the_100us_fallback",
        "test_0x0B_zero_pulse_delay_takes_the_500us_fallback",
        "test_0x07_nonzero_pulse_delay_is_left_alone",
        "test_0x08_nonzero_pulse_delay_is_left_alone",
        "test_0x0B_nonzero_pulse_delay_is_left_alone",
    ),
}


# Tests -- requirement -> case membership (Coverage 1-4).


def test_every_mapped_requirement_names_only_existing_cases():
    """Coverage 1 -- D-01's core assertion. Union the extracted RUN_TEST
    names across the three mapped suites, then for each requirement id
    assert every mapped case name is present in that union. A missing case
    fails naming the requirement id, the missing case name, and the
    mapped-suite files scanned -- never a bare "lists differ"."""
    per_suite = _extract_all_mapped_names()
    all_names = set()
    for names in per_suite.values():
        all_names.update(names)
    scanned_files = [str(_suite_path(suite)) for suite in _MAPPED_SUITES]

    for requirement_id, case_names in _REQUIREMENT_CASES.items():
        for case_name in case_names:
            assert case_name in all_names, (
                f"{requirement_id} names case {case_name!r}, which does "
                "not exist in any of the three mapped suites' RUN_TEST "
                "sites -- it may have been renamed or deleted.\n"
                f"Scanned: {scanned_files}"
            )


def test_each_mapped_suite_meets_its_hardcoded_case_floor():
    """Coverage 2 -- per-suite extracted count >= its hardcoded floor
    (47 / 32 / 9). A future deletion that still leaves every MAPPED case
    present (because the deleted case belonged to none of TEST-01...05)
    would slip past Coverage 1 alone; this floor catches it."""
    per_suite = _extract_all_mapped_names()
    for suite, floor in _SUITE_FLOORS.items():
        observed = len(per_suite[suite])
        assert observed >= floor, (
            f"suite {suite!r} extracted {observed} RUN_TEST case(s) from "
            f"{_suite_path(suite)}, expected >= {floor} (hardcoded floor) "
            "-- a case may have been silently removed."
        )


def test_extracted_case_names_are_unique():
    """Coverage 3 -- len(set(names)) == len(names) across the union; a
    duplicate would silently inflate a suite's extracted count and could
    mask an unrelated rename hiding behind the duplicate's presence."""
    per_suite = _extract_all_mapped_names()
    all_names = []
    for names in per_suite.values():
        all_names.extend(names)

    if len(set(all_names)) == len(all_names):
        return

    seen = set()
    dup_locations = []
    for suite in _MAPPED_SUITES:
        path = _suite_path(suite)
        if not path.is_file():
            continue
        stripped = _strip_comments(path.read_text())
        for match in _RUN_TEST_RE.finditer(stripped):
            case_name = match.group(1)
            if case_name in seen:
                dup_locations.append(f"{suite}:{_line_of(stripped, match.start())}:{case_name}")
            seen.add(case_name)

    raise AssertionError(
        f"duplicate RUN_TEST case name(s) found across the three mapped "
        f"suites -- {len(all_names)} extracted, {len(set(all_names))} "
        f"unique. Second occurrence(s): {dup_locations}. A duplicate would "
        "inflate the extracted count and could mask a rename."
    )


def test_trace_suite_is_deliberately_out_of_scope():
    """Coverage 4 -- test_trace_eprom_v131 is asserted absent from
    _MAPPED_SUITES, and (only when its source is reachable under the
    CURRENT scan target -- a planted scratch root need not carry it) its
    source is asserted to still contain the `#ifdef EPROM_V131_TRACE_DUMP`
    guard this exclusion's reasoning depends on, so the exclusion reason
    stays machine-checked rather than becoming folklore (C-05)."""
    assert _EXCLUDED_TRACE_SUITE not in _MAPPED_SUITES, (
        f"{_EXCLUDED_TRACE_SUITE!r} must never be added to _MAPPED_SUITES "
        "-- its case set is build-flag dependent (its sixth RUN_TEST sits "
        "inside `#ifdef EPROM_V131_TRACE_DUMP`, which no env defines); it "
        "proves TEST-06, not TEST-01...05 (C-05)."
    )

    trace_path = _SCAN_SUITES / _EXCLUDED_TRACE_SUITE / f"{_EXCLUDED_TRACE_SUITE}.cpp"
    if not trace_path.is_file():
        # Skipped-free by construction -- no runtime skip call or marker
        # decorator of any kind is used here. A planted scratch root built
        # by a later planted-violation leg legitimately carries only the
        # three MAPPED suites, so this half of the check simply has
        # nothing to assert against there.
        return

    guard_needle = "#ifdef" + " EPROM_V131_TRACE_DUMP"
    text = trace_path.read_text()
    assert guard_needle in text, (
        f"{trace_path} no longer contains the {guard_needle!r} guard this "
        "exclusion's reasoning depends on -- if test_trace_eprom_v131's "
        "sixth RUN_TEST is no longer build-flag-guarded, the exclusion "
        "reason above must be re-examined, not silently kept."
    )


# Tests -- self-protection (Coverage 5-7).


def test_scan_targets_are_non_vacuous():
    """Coverage 5 -- structural self-check, two halves.

    Part (a): the DEFAULT scan root is recomputed fresh from _REPO_ROOT
    WITHOUT reading os.environ (the check_permitted_claims.py
    `_HERE`-resolves-to-the-wrong-directory landmine, closed here by
    construction): exists, and each mapped suite file beneath it exists and
    is non-empty.

    Part (b): the extractor, run against whatever _SCAN_SUITES currently
    resolves to (the SAME seam-aware target every leg above scans), must
    yield a union of at least _TOTAL_FLOOR (88) names. This is the half an
    emptied or misdirected scratch root turns RED, proving that scenario
    fails the gate's non-vacuity leg instead of making every membership
    check in Coverage 1 pass vacuously over an empty set."""
    # Part (a) -- default target, no os.environ read.
    default_root = _REPO_ROOT / _SUITES_REL
    assert default_root.is_dir(), (
        f"default scan root {default_root} does not exist on disk -- a "
        "missing scan root must FAIL, never silently pass."
    )
    assert default_root.resolve().is_relative_to(_REPO_ROOT), (
        f"default scan root {default_root} resolves outside _REPO_ROOT "
        f"({_REPO_ROOT}) -- a naive future copy of this module into "
        "another directory must fail loudly here, not scan nothing and "
        "exit 0."
    )
    for suite in _MAPPED_SUITES:
        default_suite_file = default_root / suite / f"{suite}.cpp"
        assert default_suite_file.is_file(), (
            f"default mapped suite file {default_suite_file} does not "
            "exist on disk."
        )
        assert default_suite_file.stat().st_size > 0, (
            f"default mapped suite file {default_suite_file} is empty."
        )

    # Part (b) -- seam-aware target, must extract >= _TOTAL_FLOOR names.
    per_suite = _extract_all_mapped_names()
    all_names = set()
    for names in per_suite.values():
        all_names.update(names)
    assert len(all_names) >= _TOTAL_FLOOR, (
        f"union of extracted RUN_TEST names from the CURRENT scan root "
        f"{_SCAN_SUITES} is {len(all_names)}, expected >= {_TOTAL_FLOOR} "
        "-- an emptied or misdirected scan root must fail HERE, not make "
        "every requirement-to-case membership check above pass vacuously "
        "over an empty set."
    )


def test_this_module_cannot_be_silently_skipped():
    """Coverage 6 -- this module's own source contains no runtime
    skip-bypass call, no skip-marker decorator, and no dependency-skip call
    anywhere, so the fail-closed contract this module documents is
    self-enforcing rather than merely stated. All three needles are built
    via concatenation so this test's own source and its own failure
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
    """Coverage 7 -- the concatenated-needle self-check: each of the three
    needles built by concatenation above (Coverage 6) must appear NOWHERE
    verbatim in this module's own source, including inside this very
    test's failure messages. Without this leg, a future edit could
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


# A CHILD PROCESS is mandatory for both plants: _SCAN_SUITES binds at
# IMPORT time, and monkeypatch.setenv cannot reach an already-imported
# module-level value (S6). Copied structurally from
# tests/test_flash_path_record_sync.py:269-305's own
# `_run_gate_in_subprocess` and its FIRESTARTER_129_GATE_CHILD recursion
# guard.

# The three mapped suites' REAL sources -- resolved via _REPO_ROOT directly
# (never via _SCAN_SUITES/the seam), because these paths name the ONE true
# copy the S2 ceremony below must prove untouched, regardless of whatever
# scratch root a planted run happens to point the seam at.
_REAL_SUITE_PATHS = {
    suite: _REPO_ROOT / _SUITES_REL / suite / f"{suite}.cpp" for suite in _MAPPED_SUITES
}


def _resolve_git():
    """Resolve the `git` binary, fail-closed -- copied structurally from
    tests/test_golden_trace_identity_eprom_v131.py's own `_resolve_git`.
    Deliberately never bypassed via any decorator or runtime call that
    would mark this outcome as skipped: a missing `git` must FAIL this
    suite, never be silently skipped, or the S2 ceremony below could never
    prove the real suite sources are untouched."""
    git_bin = shutil.which(os.environ.get("GIT", "git"))
    assert git_bin is not None, (
        "git not found on PATH (checked $GIT, falling back to 'git'). This "
        "must FAIL the suite, never be silently skipped -- a missing git "
        "would otherwise turn the S2 real-sources-untouched ceremony into "
        "a silent no-op."
    )
    return git_bin


def _git_hash_object(path):
    """Resolve `git` fail-closed and hash-object `path` (list-form argv,
    shell=False)."""
    git_bin = _resolve_git()
    result = subprocess.run(
        [git_bin, "hash-object", str(path)],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def _git_porcelain(path):
    """Resolve `git` fail-closed and return `git status --porcelain` for
    `path` (list-form argv, shell=False). Empty output means a clean
    tree."""
    git_bin = _resolve_git()
    result = subprocess.run(
        [git_bin, "-C", str(path), "status", "--porcelain"],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout


def _hash_real_suites():
    """git hash-object every real mapped-suite source, keyed by suite
    name. Called before AND after each planted run below so the S2
    ceremony can assert none of the three real sources changed."""
    return {suite: _git_hash_object(path) for suite, path in _REAL_SUITE_PATHS.items()}


def _copy_real_suites_to(scratch_root):
    """Copy each of the three mapped suites' REAL source (read via
    _REAL_SUITE_PATHS, never through the seam) verbatim into
    `scratch_root/<suite>/<suite>.cpp`. Returns {suite: dest_path}."""
    dests = {}
    for suite, real_path in _REAL_SUITE_PATHS.items():
        dest_dir = scratch_root / suite
        dest_dir.mkdir(parents=True, exist_ok=True)
        dest_path = dest_dir / f"{suite}.cpp"
        dest_path.write_text(real_path.read_text())
        dests[suite] = dest_path
    return dests


def _run_gate_in_subprocess(env_overrides, node_id=None):
    """Run `[sys.executable, "-m", "pytest", <this module's path>, "-q",
    "-rs"]` (optionally scoped to `node_id`) with `cwd=_REPO_ROOT` and
    `os.environ` merged with `env_overrides`, capturing text output, and
    return the `CompletedProcess`.

    A child process is MANDATORY here: `_SCAN_SUITES` binds at IMPORT
    time, and `monkeypatch.setenv` cannot reach an already-imported
    module-level value (S6). `-rs` is passed so a skipped outcome (rather
    than a failure) would be visible in the captured output.

    Guards against infinite recursion by refusing to run (a plain assert,
    never a skip) when FIRESTARTER_144_GATE_CHILD is already set in the
    CURRENT process, and sets that variable in the child's environment.
    """
    assert os.environ.get("FIRESTARTER_144_GATE_CHILD") is None, (
        "refusing to spawn a nested gate subprocess -- "
        "FIRESTARTER_144_GATE_CHILD is already set in this process; this "
        "would recurse indefinitely if it were allowed to proceed."
    )
    module_path = str(_HERE / "test_requirement_case_mapping_v131.py")
    target = f"{module_path}::{node_id}" if node_id else module_path
    env = dict(os.environ)
    env.update(env_overrides)
    env["FIRESTARTER_144_GATE_CHILD"] = "1"
    return subprocess.run(
        [sys.executable, "-m", "pytest", target, "-q", "-rs"],
        cwd=str(_REPO_ROOT),
        env=env,
        capture_output=True,
        text=True,
    )


def test_planted_renamed_case_is_detected(tmp_path):
    """Coverage 8 -- D-18 Plant A. A case TEST-02 is mapped against
    (test_loop01_pulse_width_never_grows_between_attempts) is renamed in a
    SCRATCH copy of test_loop_eprom_v131 -- never the real tree (S2
    ceremony) -- and Coverage 1's own leg, run in a CHILD PROCESS (the
    seam binds at import; monkeypatch.setenv cannot reach it, S6), must
    name both the missing case and the requirement that lost it."""
    renamed_case = "test_loop01_pulse_width_never_grows_between_attempts"
    truncated_case = "test_loop01_pulse_width_never_grows"

    before_hashes = _hash_real_suites()
    real_loop_path = _REAL_SUITE_PATHS["test_loop_eprom_v131"]
    real_text = real_loop_path.read_text()

    assert real_text.count(renamed_case) == 2, (
        f"expected {renamed_case!r} to appear exactly twice in the real "
        "file (the function definition and its RUN_TEST site), found "
        f"{real_text.count(renamed_case)} -- the plant's 'both occurrences' "
        "claim would no longer hold."
    )
    mutated_text = real_text.replace(renamed_case, truncated_case)
    assert mutated_text != real_text, (
        f"planted rename did not actually change the text -- the target "
        f"{renamed_case!r} was not found (the suite may have changed)."
    )

    scratch_root = tmp_path / "scratch_suites"
    dests = _copy_real_suites_to(scratch_root)
    dests["test_loop_eprom_v131"].write_text(mutated_text)

    result = _run_gate_in_subprocess(
        {"FIRESTARTER_CASE_MAP_SCAN_ROOT": str(scratch_root)},
        node_id="test_every_mapped_requirement_names_only_existing_cases",
    )
    output = result.stdout + result.stderr

    assert result.returncode != 0, (
        "expected the mapping gate to FAIL against a scan root with a "
        f"renamed case, got returncode=0.\nOutput:\n{output}"
    )
    assert renamed_case in output, (
        f"expected the RED output to name the missing case {renamed_case!r}"
        f".\nOutput:\n{output}"
    )
    assert "TEST-02" in output, (
        f"expected the RED output to name the requirement TEST-02 that "
        f"lost its case.\nOutput:\n{output}"
    )
    # Leg isolation -- this run is scoped to ONE node id, so no OTHER
    # leg's phrase should appear, proving the RED is attributable to
    # exactly the membership check, not a coincidental floor or count
    # complaint (88 names are still extractable from the scratch root
    # minus one rename, so a count-only complaint would be misleading).
    assert "hardcoded floor" not in output, (
        f"expected no hardcoded-floor complaint (a different leg's phrase) "
        f"in this node-id-scoped run.\nOutput:\n{output}"
    )
    assert "emptied or misdirected scan root" not in output, (
        f"expected no non-vacuity-leg complaint (a different leg's phrase) "
        f"in this node-id-scoped run.\nOutput:\n{output}"
    )

    after_hashes = _hash_real_suites()
    assert after_hashes == before_hashes, (
        f"real suite source(s) changed during the planted-rename run -- "
        f"before={before_hashes} after={after_hashes}. Planted content "
        "must only ever be written under tmp_path, never the real tree."
    )
    assert _git_porcelain(_REPO_ROOT) == "", (
        "the firmware repo's working tree is no longer clean after the "
        "planted-rename test."
    )


def test_planted_emptied_scan_root_fails_the_non_vacuity_leg(tmp_path):
    """Coverage 9 -- D-18 Plant B. An EMPTY scratch scan root (no .cpp
    anywhere) must fail Coverage 5's own leg (test_scan_targets_are_non_
    vacuous), run in a CHILD PROCESS, with a message naming the hardcoded
    floor (88) and the observed count (0) -- proving an emptied or
    misdirected scan root fails the gate's non-vacuity leg instead of
    making every membership check in Coverage 1 pass vacuously over an
    empty set. Run via -rs so a skipped outcome (rather than a failure)
    would be visible."""
    before_hashes = _hash_real_suites()

    scratch_root = tmp_path / "empty_scratch_suites"
    scratch_root.mkdir(parents=True)
    assert list(scratch_root.iterdir()) == [], (
        f"expected {scratch_root} to be empty before the child run."
    )

    result = _run_gate_in_subprocess(
        {"FIRESTARTER_CASE_MAP_SCAN_ROOT": str(scratch_root)},
        node_id="test_scan_targets_are_non_vacuous",
    )
    output = result.stdout + result.stderr

    assert result.returncode != 0, (
        "expected the non-vacuity leg to FAIL against an EMPTY scan root, "
        f"got returncode=0.\nOutput:\n{output}"
    )
    assert "is 0, expected >= 88" in output, (
        "expected the RED output to name the observed count (0) and the "
        f"hardcoded floor (88) together.\nOutput:\n{output}"
    )
    assert "skipped" not in output.lower(), (
        "expected the child run NOT to report a skipped outcome (-rs "
        "makes any skip visible) -- an emptied scan root must FAIL, never "
        f"SKIP.\nOutput:\n{output}"
    )

    after_hashes = _hash_real_suites()
    assert after_hashes == before_hashes, (
        f"real suite source(s) changed during the planted-empty-root run "
        f"-- before={before_hashes} after={after_hashes}. Planted content "
        "must only ever be written under tmp_path, never the real tree."
    )
    assert _git_porcelain(_REPO_ROOT) == "", (
        "the firmware repo's working tree is no longer clean after the "
        "planted-empty-root test."
    )
