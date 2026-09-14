"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson

Permission is hereby granted under MIT license.

Host-side numerical oracle and source-contract scan for
`rurp_read_voltage_mv` (src/boards/rurp_common.cpp) -- DEAD-04's committed
proof that the 32-bit reformulation (this phase) is equivalent to the
64-bit form it replaces, bound to the shipped C so the Python model cannot
drift from the firmware silently.

Requirements: DEAD-03, DEAD-04, DEAD-05
Decisions covered: OQ-1, OQ-3, D-02

Coverage ceiling. `rurp_read_voltage_mv` compiles in no native environment
-- [env:native]'s and [env:native_nodevtools]'s shared `build_src_filter`
admits only `src/proms/`, `boards/rurp_serial_utils.cpp`, `json_parser.c`
and `operation_utils.cpp`, and widening it was measured unavailable
because the translation unit is gated on the three ARDUINO_AVR_* board
macros and its body writes AVR ADC registers. So, in exactly these terms:
`rurp_read_voltage_mv` is proven by a committed host-side numerical oracle
over a stated input grid, bound to the shipped C by a source-contract
scan; no native and no bench coverage exists. This phase creates neither.
The named residual risk, stated and NOT presented as covered: avr-gcc
miscompiling the 32-bit multiply/divide. No artefact of this phase
mitigates that risk; it is mitigated only by that being AVR's
most-exercised code-generation path, and by this phase's change reducing
rather than increasing codegen complexity, since it deletes the 64-bit
path rather than adding one.

The hollow-gate tension, answered rather than dodged.
`tests/test_config_storage_dualslot.py`'s own docstring explicitly rejects
"an independent fake reimplementation of the algorithm living only in
this test", naming it as the exact hollow-gate shape Phases 118 and 124
each had to unwind -- and this module's numerical half IS such a
reimplementation, so a reviewer will raise this. The answer: the
source-contract half below is what converts "a copy behaves" into "the
shipped text is the formula the copy models" -- the three model
constants (_SCALE_NUMERATOR, _GUARD_SUM_MAX, _GUARD_K_MAX) and the five
transcribed constructs are asserted to appear, verbatim or by construct,
in the comment-stripped shipped body, so a change on either side turns
this module red. `test_config_storage_dualslot.py`'s own alternative --
host-compile the real C -- is unavailable here: `src/boards/rurp_common.cpp`
wraps its entire body in a preprocessor test of the three ARDUINO_AVR_*
board macros with an unsupported-board #error fall-through, and the
function's body writes the ADMUX and ADCSRA registers and calls
analogRead, analogReference and rurp_get_config. Forcing a board macro
would drag in AVR register headers and make the register mocking the
thing under test.

Non-claim: a green run of this module proves that two integer formulas
agree over the stated grid, and that the shipped C is textually the
formula this module models. It does not execute AVR code.

Where this runs: CI leg 3 (`pytest tests/ -v`), adding zero AVR bytes,
zero native test cases, and moving no count any gate asserts by exact
equality.
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent

_RURP_COMMON_REL = "src/boards/rurp_common.cpp"
_SHIELD_HEADER_REL = "include/rurp_shield.h"

# Environment seam -- binds at IMPORT time, following the convention in
# test_write_path_source_contract_v131.py. No conftest.py exists anywhere
# in this repo (a recorded house rule), so every path is resolved at
# module level.
_SCAN_RURP_COMMON = Path(
    os.environ.get(
        "FIRESTARTER_VOLTAGE_ORACLE_SCAN_SOURCE",
        str(_REPO_ROOT / _RURP_COMMON_REL),
    )
)
_SHIELD_HEADER = _REPO_ROOT / _SHIELD_HEADER_REL

# The ONLY place the model's constants live. A later leg (Coverage:
# test_model_constants_appear_verbatim_in_the_shipped_body) asserts each of
# these appears verbatim, as a decimal literal, in the comment-stripped
# shipped body -- this is the anti-drift binding: a change to either side
# turns this module red.
_SCALE_NUMERATOR = 1100
_GUARD_SUM_MAX = 3900000
_GUARD_K_MAX = 4194303

_MANDATED_PHRASING = (
    "proven by a committed host-side numerical oracle over a stated input "
    "grid, bound to the shipped C by a source-contract scan; no native and "
    "no bench coverage exists."
)


def _normalise_ws(text):
    return re.sub(r"\s+", " ", text or "").strip()


# _strip_comments / _line_of -- copied verbatim from
# test_write_path_source_contract_v131.py:223-256. This repo duplicates
# this helper rather than sharing it (a second, functionally equivalent
# copy already lives at scripts/check_erase_no_vpp.py:161-197), so
# duplicating it again here is the established idiom.
def _strip_comments(text):
    """Strip `//` line comments and `/* ... */` block comments, replacing
    each stripped span with whitespace of the SAME SHAPE (a newline stays a
    newline, everything else becomes a single space) so every line number
    in the result matches the original file exactly."""
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


def _extract_function_body(text, name):
    """Brace-matched extraction of `name`'s definition body from
    comment-stripped `text` (including the enclosing braces). Fails with a
    message that names the function and dumps the stripped source when the
    definition cannot be located or brace-balanced -- a missing or
    malformed scan target must FAIL, never silently pass."""
    pattern = re.compile(r"\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{")
    m = pattern.search(text)
    assert m, (
        f"could not locate the definition of {name} in the "
        f"comment-stripped source -- expected a match for "
        f"{pattern.pattern!r} (started scanning at line "
        f"{_line_of(text, 0)}).\nGot (comment-stripped source):\n{text}"
    )
    start = m.end() - 1  # index of the opening brace
    depth = 0
    i = start
    n = len(text)
    while i < n:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
        i += 1
    raise AssertionError(
        f"could not brace-balance the body of {name} starting at line "
        f"{_line_of(text, start)} -- source may be malformed.\nGot "
        f"(comment-stripped source):\n{text}"
    )


_VALUE_R1_RE = re.compile(r"#define\s+VALUE_R1\s+(\d+)")
_VALUE_R2_RE = re.compile(r"#define\s+VALUE_R2\s+(\d+)")


def _calibration_from_header():
    """Parse VALUE_R1 and VALUE_R2 out of include/rurp_shield.h with a
    compiled regex, failing loudly if either is absent -- the oracle's
    calibration is bound to the shipped header rather than hardcoded."""
    text = _SHIELD_HEADER.read_text()
    r1_match = _VALUE_R1_RE.search(text)
    r2_match = _VALUE_R2_RE.search(text)
    assert r1_match, f"VALUE_R1 not found in {_SHIELD_HEADER}"
    assert r2_match, f"VALUE_R2 not found in {_SHIELD_HEADER}"
    return int(r1_match.group(1)), int(r2_match.group(1))


# The two models
def _v64(adc, bg, r1, r2):
    """Models the 64-bit form being replaced -- the product of the ADC
    reading, the scale numerator (1100) and the summed divider (r1+r2),
    plus half the denominator, divided by the denominator (bg*r2), all in
    unbounded Python integers with floor division. Transcribes:
    `uint64_t numerator = (uint64_t)voltage_adc_reading * 1100UL * (r1 +
    r2); uint64_t denominator = (uint64_t)bandgap_adc_reading * r2; return
    (numerator + (denominator / 2)) / denominator;` from the pre-change
    `rurp_common.cpp`. The pre-existing zero sentinel (r2==0 or bg==0)
    sits ABOVE this arithmetic in both the shipped C and this model."""
    if r2 == 0 or bg == 0:
        return 0
    numerator = adc * _SCALE_NUMERATOR * (r1 + r2)
    denominator = bg * r2
    return (numerator + denominator // 2) // denominator


def _v32(adc, bg, r1, r2):
    """Models the 32-bit reformulation -- the pre-existing zero sentinel
    first (mirroring the early return that sits ABOVE the new guards in
    the shipped C), then the summed divider, guard A, the folded scale
    factor, guard B, then the ADC-times-scale-factor-plus-half-bandgap
    over bandgap. Neither this model nor _v64 applies the final narrowing
    cast to uint16_t -- both the old and the new C narrow their result
    identically, so comparing the pre-narrowing values is a strictly
    stronger equivalence claim. The silent-narrowing hazard for an
    absurd-but-guard-passing calibration is UNCHANGED from today's
    behaviour, is not fixed by this phase, and is outside DEAD-04's stated
    scope."""
    if r2 == 0 or bg == 0:
        return 0
    total = r1 + r2
    if total > _GUARD_SUM_MAX:
        return 0
    k = (_SCALE_NUMERATOR * total) // r2
    if k > _GUARD_K_MAX:
        return 0
    bg32 = bg
    return (adc * k + bg32 // 2) // bg32


# Numeric legs
def test_scale_factor_is_exact_at_the_shipped_calibration():
    """DEAD-04: at VALUE_R1/VALUE_R2 (read from include/rurp_shield.h,
    never hardcoded here), the folded scale factor k is 7850 exactly --
    lossless, because VALUE_R2 divides 1100*(VALUE_R1+VALUE_R2) evenly."""
    r1, r2 = _calibration_from_header()
    k = (_SCALE_NUMERATOR * (r1 + r2)) // r2
    assert k == 7850, (
        f"expected k == 7850 at the shipped calibration (r1={r1}, "
        f"r2={r2}), got {k}"
    )
    assert (_SCALE_NUMERATOR * (r1 + r2)) % r2 == 0, (
        "expected the shipped calibration to divide evenly (lossless "
        f"folding); r1={r1}, r2={r2}"
    )


def test_named_single_reading_agrees_in_both_forms():
    """DEAD-04's named reading: ADC 1023, bandgap 225 gives 35691 mV in
    both the 64-bit and the 32-bit forms, at the shipped calibration."""
    r1, r2 = _calibration_from_header()
    adc, bg = 1023, 225
    v64 = _v64(adc, bg, r1, r2)
    v32 = _v32(adc, bg, r1, r2)
    assert v64 == 35691, f"expected V64(1023, 225) == 35691, got {v64}"
    assert v32 == 35691, f"expected V32(1023, 225) == 35691, got {v32}"


def test_bit_identity_at_the_shipped_calibration_over_the_stated_bandgap_range():
    """DEAD-04: 0 mismatches between the 64-bit and 32-bit forms at the
    shipped calibration, over bandgap 200..250 crossed with the full ADC
    range 0..1023."""
    r1, r2 = _calibration_from_header()
    mismatches = []
    for bg in range(200, 251):
        for adc in range(0, 1024):
            v64 = _v64(adc, bg, r1, r2)
            v32 = _v32(adc, bg, r1, r2)
            if v64 != v32:
                mismatches.append((adc, bg, v64, v32))
    assert not mismatches, (
        "expected 0 mismatches over bandgap 200..250 x ADC 0..1023 at "
        f"the shipped calibration, found {len(mismatches)}; first: "
        f"{mismatches[0] if mismatches else None}"
    )


def test_bit_identity_at_the_shipped_calibration_over_the_full_bandgap_range():
    """DEAD-04's stronger bit-identity: 0 mismatches over bandgap 1..1023
    (the full plausible ADC range for the bandgap channel) crossed with
    ADC 0..1023, at the shipped calibration."""
    r1, r2 = _calibration_from_header()
    mismatches = []
    for bg in range(1, 1024):
        for adc in range(0, 1024):
            v64 = _v64(adc, bg, r1, r2)
            v32 = _v32(adc, bg, r1, r2)
            if v64 != v32:
                mismatches.append((adc, bg, v64, v32))
    assert not mismatches, (
        "expected 0 mismatches over bandgap 1..1023 x ADC 0..1023 at the "
        f"shipped calibration, found {len(mismatches)}; first: "
        f"{mismatches[0] if mismatches else None}"
    )


def test_worst_deviation_over_the_stated_grid_is_exactly_five_and_never_over_reads():
    """DEAD-04: over R2 39000..47000 (step 1000) crossed with bandgap
    200..250 and ADC 0..1023 -- 470016 evaluations, walked ONCE in this
    one loop, combining the deviation-bound and the direction checks so
    the grid is not walked twice -- the worst absolute deviation is
    exactly 5 mV, and the 32-bit form is never greater than the 64-bit
    form (it can only ever under-read, never over-read)."""
    r1, _unused_r2 = _calibration_from_header()
    worst = -1
    worst_args = None
    over_reads = []
    for r2 in range(39000, 47001, 1000):
        for bg in range(200, 251):
            for adc in range(0, 1024):
                v64 = _v64(adc, bg, r1, r2)
                v32 = _v32(adc, bg, r1, r2)
                if v32 > v64:
                    over_reads.append((adc, bg, r2, v64, v32))
                deviation = v64 - v32
                if deviation > worst:
                    worst = deviation
                    worst_args = (adc, bg, r2, v64, v32)
    assert not over_reads, (
        "expected the 32-bit form to never over-read; found "
        f"{len(over_reads)} counter-example(s), first: {over_reads[0]}"
    )
    assert worst == 5, (
        "expected the worst absolute deviation over the stated grid to "
        f"be exactly 5, got {worst}; argmax (adc, bg, r2, v64, v32) = "
        f"{worst_args}"
    )


def test_guard_a_boundary_pair():
    """DEAD-04 guard A: r1=3856000, r2=44000 (sum exactly 3900000, AT the
    guard) returns non-zero; r1=3856001, r2=44000 (sum 3900001, one past
    the guard) returns 0. These exact pairs are computed and fixed, not
    searched at runtime."""
    adc, bg = 512, 225
    r1_at, r2_at = 3856000, 44000
    r1_over, r2_over = 3856001, 44000
    assert r1_at + r2_at == 3900000, (
        f"expected the guard-A-at-boundary sum to be exactly 3900000, "
        f"got {r1_at + r2_at}"
    )
    assert r1_over + r2_over == 3900001, (
        f"expected the guard-A-over-boundary sum to be exactly 3900001, "
        f"got {r1_over + r2_over}"
    )
    v32_at = _v32(adc, bg, r1_at, r2_at)
    v32_over = _v32(adc, bg, r1_over, r2_over)
    assert v32_at != 0, (
        f"expected a non-zero result AT the guard-A boundary "
        f"(sum=3900000, r1={r1_at}, r2={r2_at}), got {v32_at}"
    )
    assert v32_over == 0, (
        f"expected 0 one past the guard-A boundary (sum=3900001, "
        f"r1={r1_over}, r2={r2_over}), got {v32_over}"
    )


def test_guard_b_boundary_pair():
    """DEAD-04 guard B: r1=3812003, r2=1000 gives a scale factor of
    exactly 4194303 (AT the guard) and returns non-zero; r1=3812004,
    r2=1000 gives exactly 4194304 (one past the guard) and returns 0.
    Both cases keep the sum at or below 3900000, so guard A is provably
    not the guard that fired. These exact pairs are computed and fixed,
    not searched at runtime."""
    adc, bg = 512, 225
    r1_at, r2_at = 3812003, 1000
    r1_over, r2_over = 3812004, 1000
    sum_at = r1_at + r2_at
    sum_over = r1_over + r2_over
    k_at = (_SCALE_NUMERATOR * sum_at) // r2_at
    k_over = (_SCALE_NUMERATOR * sum_over) // r2_over
    assert sum_at <= _GUARD_SUM_MAX, (
        f"expected the guard-B-at-boundary sum to be at or below "
        f"{_GUARD_SUM_MAX}, got {sum_at} -- guard A must provably not be "
        "the guard that fires here"
    )
    assert sum_over <= _GUARD_SUM_MAX, (
        f"expected the guard-B-over-boundary sum to be at or below "
        f"{_GUARD_SUM_MAX}, got {sum_over} -- guard A must provably not "
        "be the guard that fires here"
    )
    assert k_at == 4194303, f"expected k == 4194303 AT the boundary, got {k_at}"
    assert k_over == 4194304, (
        f"expected k == 4194304 one past the boundary, got {k_over}"
    )
    v32_at = _v32(adc, bg, r1_at, r2_at)
    v32_over = _v32(adc, bg, r1_over, r2_over)
    assert v32_at != 0, (
        f"expected a non-zero result AT the guard-B boundary (k=4194303, "
        f"r1={r1_at}, r2={r2_at}), got {v32_at}"
    )
    assert v32_over == 0, (
        f"expected 0 one past the guard-B boundary (k=4194304, "
        f"r1={r1_over}, r2={r2_over}), got {v32_over}"
    )


def test_guard_b_bound_keeps_the_product_inside_thirty_two_bits():
    """DEAD-04: at scale factor 4194303 and the maximum ADC reading 1023,
    the product is exactly 4290771969, which is below the 32-bit ceiling
    4294967295 (2**32 - 1) -- confirming guard B's bound is exactly
    right."""
    product = 1023 * 4194303
    assert product == 4290771969, f"expected 4290771969, got {product}"
    assert product < 4294967295, (
        f"expected the product to be below the 32-bit ceiling "
        f"(4294967295), got {product}"
    )


def test_zero_sentinels_still_return_zero():
    """DEAD-04: the pre-existing zero sentinels (r2==0, bandgap==0) are
    unchanged by the reformulation -- both sit ABOVE the new guards in
    the model, mirroring the shipped C's early return."""
    r1, r2 = _calibration_from_header()
    assert _v32(512, 225, r1, 0) == 0, "expected r2==0 to return 0"
    assert _v32(512, 0, r1, r2) == 0, "expected bandgap==0 to return 0"


# Source-contract legs -- bind the model to the shipped C
_SUM_ASSIGN_RE = re.compile(r"uint32_t\s+sum\s*=\s*r1\s*\+\s*r2\s*;")
_GUARD_A_RE = re.compile(r"sum\s*>\s*3900000UL")
_SCALE_ASSIGN_RE = re.compile(
    r"uint32_t\s+k\s*=\s*\(\s*1100UL\s*\*\s*sum\s*\)\s*/\s*r2\s*;"
)
_GUARD_B_RE = re.compile(r"k\s*>\s*4194303UL")
_FINAL_EXPR_RE = re.compile(r"voltage_adc_reading\s*\*\s*k")
_UINT64_RE = re.compile(r"\buint64_t\b")


def test_shipped_c_matches_the_transcribed_formula():
    """Coverage: the five constructs this module's model transcribes --
    the summed-divider assignment, guard A's comparison, the folded
    scale-factor assignment, guard B's comparison, and the final
    ADC-times-scale expression -- must each appear at least once in the
    comment-stripped body of rurp_read_voltage_mv in the shipped C.
    RED by construction until task 2 of this same plan lands the 32-bit
    reformulation -- this failure is what proves this leg is reachable,
    not pre-satisfied."""
    text = _strip_comments(_SCAN_RURP_COMMON.read_text())
    body = _extract_function_body(text, "rurp_read_voltage_mv")
    for label, rx in (
        ("the summed-divider assignment", _SUM_ASSIGN_RE),
        ("guard A's comparison", _GUARD_A_RE),
        ("the folded scale-factor assignment", _SCALE_ASSIGN_RE),
        ("guard B's comparison", _GUARD_B_RE),
        ("the final ADC-times-scale expression", _FINAL_EXPR_RE),
    ):
        count = len(rx.findall(body))
        assert count > 0, (
            f"expected at least one occurrence of {label} in the "
            f"comment-stripped body of rurp_read_voltage_mv, found "
            f"{count}.\nGot (comment-stripped body):\n{body}"
        )


def test_no_sixty_four_bit_type_remains_in_the_function_body():
    """Coverage: the CI-resident source-level oracle for DEAD-03 -- asserts
    the 64-bit type name is absent from the comment-stripped body of
    rurp_read_voltage_mv.

    This was the SECOND oracle; the link-time avr-nm symbol gate that was
    the first (check_no_heap_or_64bit_symbols.py) was retired on 2026-09-13
    as a never-run gate, so this is now the only automated DEAD-03 check --
    and it is much narrower. It reads one function's source text for one
    type name, where the retired gate read three linked ELFs for eleven
    symbols. A 64-bit runtime pulled in from any other translation unit,
    or through inlining, is no longer caught by anything."""
    text = _strip_comments(_SCAN_RURP_COMMON.read_text())
    body = _extract_function_body(text, "rurp_read_voltage_mv")
    matches = _UINT64_RE.findall(body)
    assert not matches, (
        "expected zero uint64_t occurrences in the comment-stripped body "
        f"of rurp_read_voltage_mv, found {len(matches)}.\nGot "
        f"(comment-stripped body):\n{body}"
    )


def test_model_constants_appear_verbatim_in_the_shipped_body():
    """Coverage: the leg that makes model drift impossible to land
    silently -- each of _SCALE_NUMERATOR, _GUARD_SUM_MAX and
    _GUARD_K_MAX must appear as a decimal literal in the comment-stripped
    body of rurp_read_voltage_mv. RED by construction until task 2 of
    this same plan lands (the shipped body does not yet carry
    _GUARD_SUM_MAX or _GUARD_K_MAX)."""
    text = _strip_comments(_SCAN_RURP_COMMON.read_text())
    body = _extract_function_body(text, "rurp_read_voltage_mv")
    for label, value in (
        ("_SCALE_NUMERATOR", _SCALE_NUMERATOR),
        ("_GUARD_SUM_MAX", _GUARD_SUM_MAX),
        ("_GUARD_K_MAX", _GUARD_K_MAX),
    ):
        needle = str(value)
        assert needle in body, (
            f"expected the literal {needle!r} ({label}) to appear "
            f"verbatim in the comment-stripped body of "
            f"rurp_read_voltage_mv.\nGot (comment-stripped body):\n{body}"
        )


# Structural legs
def test_scan_target_is_non_vacuous():
    """Structural: the scan target exists, is non-empty, resolves under
    the repo root, and is non-empty after comment stripping. A missing
    scan target must FAIL, never silently pass."""
    default_target = _REPO_ROOT / _RURP_COMMON_REL
    assert default_target.is_file(), (
        f"default scan target {default_target} does not exist on disk -- "
        "a missing scan target must FAIL, never silently pass."
    )
    assert default_target.stat().st_size > 0, (
        f"default scan target {default_target} is empty"
    )
    assert default_target.resolve().is_relative_to(_REPO_ROOT), (
        f"default scan target {default_target} resolves outside "
        f"_REPO_ROOT ({_REPO_ROOT}) -- a naive future copy of this "
        "module into another directory must fail loudly here, not scan "
        "nothing and exit 0."
    )
    stripped = _strip_comments(default_target.read_text())
    assert stripped.strip() != "", (
        f"comment-stripped {default_target} is empty -- nothing would "
        "ever be scanned by this module's other legs."
    )


def test_this_module_cannot_be_silently_skipped():
    """Structural: this module's own source contains no runtime
    skip-bypass call, no skip-marker decorator, and no dependency-skip
    call anywhere -- the needle strings below are built via concatenation
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


def test_module_docstring_states_the_coverage_ceiling():
    """Coverage: DEAD-05's mandated phrasing must appear verbatim (modulo
    whitespace) in this module's own docstring, and none of the six
    forbidden coverage phrasings enumerated in 155-VALIDATION.md item 5
    may appear. Each forbidden needle is built by concatenation so this
    leg's own source text cannot match a needle it exists to detect."""
    doc = _normalise_ws(__doc__)
    assert _MANDATED_PHRASING in doc, (
        "expected the mandated coverage-ceiling phrasing verbatim (modulo "
        f"whitespace) in this module's docstring.\nGot (normalised): "
        f"{doc}"
    )
    forbidden = (
        "test" + "ed",
        "unit-" + "test" + "ed",
        "covered" + " by native",
        "verified" + " on hardware",
        "bench-" + "verified",
        "proven" + " at runtime",
    )
    lowered = doc.lower()
    for needle in forbidden:
        assert needle not in lowered, (
            f"forbidden coverage phrasing {needle!r} found in this "
            f"module's docstring -- see 155-VALIDATION.md item 5.\nGot "
            f"(normalised): {doc}"
        )
