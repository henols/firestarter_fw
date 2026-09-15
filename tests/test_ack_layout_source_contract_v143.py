"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 143 Plan 03 (HOST-01, wire half; BF-1 closure) -- pins the
operation-setup ack's pack layout in src/firestarter.cpp: the CAP-01 buffer
size, the PORTED (not invented) CAP-02 hardware-revision / firmware-identity
tail, and this plan's own CAP-03 per-block write-time budget field, all
inside one LOG_OK_ID_BYTES(MSG_OK_READY, ...) emit in init_programmer_framed.

Requirements: HOST-01 (wire half only -- 143-03-PLAN.md's own frontmatter
`requirements: []` is deliberate; this plan marks no requirement Complete).

Defect class this closes: a wire-layout change on ONE side of a two-repo
protocol, with NOTHING comparing the two sides -- BF-1's exact shape.
143-RESEARCH.md's own BF-1 finding is that the v1.31 firmware branch shipped
(and, absent this plan, would have kept shipping) a bare 2-byte MSG_OK_READY
ack while the v1.31 host branch's `_decode_id_frame` already expected a
CAP-02 identity tail at a computed offset -- so a v1.31 firmware build and
the v1.31 host COULD NOT CONNECT AT ALL, and no gate in either repository
noticed, because nothing compared the two sides of this one wire field
(143-PATTERNS.md's "No Analog Found" table names this gap explicitly).

This module closes the FIRMWARE half of that gap: it pins the pack layout
this file emits, byte for byte, so a future edit that narrows, reorders or
mis-sizes the pack cannot land silently. It does NOT perform a live
cross-repo comparison against firestarter_app/firestarter/serial_comm.py's
decoder -- that standing gate is handed to Phase 144 / TEST-07
(143-RESEARCH.md Open Question 4). The HOST half of this same layout is
independently pinned by plan 143-02's `_cap03_params` fixture in
firestarter_app/tests/test_hw_revision_gate.py, decoded by the real
`_decode_id_frame` at two identity lengths -- this module never imports or
reads anything from that other repository.

Coverage:
  1. test_the_retired_two_byte_ready_emit_is_gone -- the retired
     two-byte-only ack emit macro, paired with MSG_OK_READY, appears ZERO
     times in the comment-stripped init_programmer_framed body. Needle
     concatenation-built so this module's own prose cannot satisfy it by
     accident. BF-1: this is the exact emit shape that made a v1.31
     firmware build unreachable by the v1.31 host.
  2. test_exactly_one_byte_blob_ready_emit_exists -- the byte-blob form of
     the same ack, paired with MSG_OK_READY, appears EXACTLY once in that
     body. Two emits would make the ack's shape depend on a code path,
     defeating the host's one and only length-based discriminator.
  3. test_the_ready_pack_buffer_has_room_for_the_budget -- the pack buffer
     is declared with room for the identity tail AND the two budget bytes.
     A buffer sized for the identity tail alone would overflow by two
     bytes on a 32-character identity once the budget is written; nothing
     here may be sized from frame content.
  4. test_the_budget_is_written_at_a_computed_offset_not_a_literal -- the
     budget's two bytes are written at the COMPUTED offset (never a
     literal), and no other index into the pack buffer, anywhere in the
     body, is a bare decimal greater than 3. D-08's hazard: a fixed index
     works for one identity length and silently misreads the next.
  5. test_the_emitted_length_accounts_for_the_budget -- the emitted byte
     count includes the budget's two bytes. Omitting them would silently
     truncate the budget off the wire and leave the host's corresponding
     attribute None forever -- a SILENT capability loss, not a loud one.
  6. test_the_budget_comes_from_the_shipped_budget_function -- the budget
     is computed by CALLING the shipped budget function (D-07), and none
     of three named hand-rolled-restatement needles (each concatenation-
     built) appears anywhere in the body: the arithmetic (already padded
     per D-09) lives in src/proms/eprom_budget.cpp and is unit-tested
     there, never duplicated at the ack site.
  7. test_the_revision_byte_is_emitted_on_every_build_configuration -- the
     hardware-revision byte is assigned on BOTH the detection-compiled-in
     arm and the no-detection arm, so the ack's byte count depends only on
     the identity-string length, never on build configuration.
  8. test_scan_targets_are_non_vacuous -- self-protection, two halves. The
     FIRST half recomputes the DEFAULT scan target fresh from the repo
     root, never via the environment seam, and checks it is sane. The
     SECOND half runs the body-extractor against whatever the (seam-aware)
     scan target CURRENTLY resolves to and requires a non-empty body --
     this is the half a planted EMPTY scratch file turns RED, proving a
     silently-empty extraction cannot make every negative-assertion leg
     above (1 and 6) pass vacuously.
  9. test_this_module_cannot_be_silently_skipped -- this module's own
     source contains no skip-bypass call, no skip-marker decorator and no
     import-or-skip call anywhere, each needle concatenation-built.
  10. test_own_needles_do_not_appear_verbatim_in_this_module -- every
      concatenation-built needle from Coverage 1, 6 and 9 (seven needles
      total) appears NOWHERE verbatim in this module's own source.

Environment seams: (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover them,
mirroring tests/test_hv_routing_source_contract_v142.py's own convention)
  - FIRESTARTER_ACK_SCAN_DISPATCH_SOURCE -- overrides the scanned
    src/firestarter.cpp path ONLY. Consulted by Coverage 1-7 and the SECOND
    half of Coverage 8. Binds at IMPORT time (the module-level
    `Path(os.environ.get(...))` expression below), so a planted-violation
    run must set it in a CHILD PROCESS environment before this module is
    imported, never via a post-import monkeypatch.
  - Coverage 8's FIRST half, Coverage 9 and Coverage 10 deliberately never
    read this seam: Coverage 8's first half recomputes the default target
    directly from the repository root without ever reading os.environ (the
    check_permitted_claims.py `_HERE`-resolves-to-the-wrong-directory
    landmine, closed here by construction, the same technique
    test_hv_routing_source_contract_v142.py's own Coverage 14 uses); 9 and
    10 read only this module's own source. A stray seam value left set
    after a planted run cannot make Coverage 8's first half, 9 or 10 pass
    vacuously -- at worst Coverage 1-7 and Coverage 8's second half redirect
    to whatever the seam happens to point at, which fails loudly rather
    than silently.

Naming note (mirrors test_hv_routing_source_contract_v142.py's own): no test
name above spells out any of the four forbidden-restatement needles or the
three skip needles verbatim -- each is named after what it forbids (a
retired emit shape, a hand-rolled restatement, a skip mechanism), never
after its exact token, so this module's own test names cannot become the
thing Coverage 10 has to catch.

CI framing, stated honestly (mirrors the analog's own section): `pytest
tests/ -v` appears in `.github/workflows/build.yml:161` and
`.github/workflows/beta-build.yml:134`, so this module -- like every other
file under tests/ -- WILL run in CI once this branch reaches `main` or
`beta`. It does not run in any CI leg on the milestone branch itself; that
is a statement about this branch's current CI wiring, not a claim that this
module is permanently invisible to CI.

This module is a standalone pytest module: it is not named check_*.py, it
adds no shared pytest configuration or fixture-registration file anywhere
(firestarter/tests/ has none, by house convention, and this module does not
introduce one), and it imports nothing beyond the Python standard library
(os, re, pathlib). It never imports, parametrizes against, or edits
tests/test_hv_routing_source_contract_v142.py or any other existing gate
module -- the scanning logic below is its own independent re-derivation of
this plan's own gate specification.
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_DISPATCH_REL = "src/firestarter.cpp"

# Environment seam -- binds at IMPORT time. See the module docstring's
# "Environment seams" section above.
_SCAN_DISPATCH = Path(
    os.environ.get("FIRESTARTER_ACK_SCAN_DISPATCH_SOURCE", str(_REPO_ROOT / _DISPATCH_REL))
)


def _strip_comments(text):
    """Strip `//` line comments and `/* ... */` block comments, replacing
    each stripped span with whitespace of the SAME SHAPE (a newline stays a
    newline, everything else becomes a single space) so every line number
    in the result matches the original file exactly -- copied verbatim from
    tests/test_write_path_source_contract_v131.py:203-235 via
    tests/test_hv_routing_source_contract_v142.py's own copy, itself
    narrowed from test_protocol_branch_inventory.py's own comment-and-
    literal stripper. The scanned function body carries no string or
    character literal outside of a comment or an #include/#ifdef directive
    (confirmed by inspection at authoring time), so literal-stripping is not
    needed here either."""
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
# verbatim anywhere in this module's own source -- see the module
# docstring and the plan's own warning: a gate that quotes its forbidden
# tokens verbatim matches itself and can never pass.
_NEEDLE_RETIRED_U16_EMIT = "LOG_OK_ID_" + "U16"
_NEEDLE_MAX_PULSES_RESTATEMENT = "max_" + "pulses"
_NEEDLE_ENERGY_CAP_US_RESTATEMENT = "energy_cap" + "_us"
_NEEDLE_PGM_READ_RESTATEMENT = "pgm_read" + "_"
_NEEDLE_SKIP_CALL = "pytest" + ".skip"
_NEEDLE_SKIPIF_MARKER = "mark" + ".skipif"
_NEEDLE_DEPENDENCY_SKIP_CALL = "importor" + "skip"

_ALL_SELF_CHECK_NEEDLES = (
    ("the retired two-byte-only ack emit macro", _NEEDLE_RETIRED_U16_EMIT),
    (
        "a hand-rolled restatement of the shipped worst-case pulse-count column",
        _NEEDLE_MAX_PULSES_RESTATEMENT,
    ),
    (
        "a hand-rolled restatement of the shipped energy-cap column",
        _NEEDLE_ENERGY_CAP_US_RESTATEMENT,
    ),
    (
        "a hand-rolled PROGMEM-byte-read-based restatement",
        _NEEDLE_PGM_READ_RESTATEMENT,
    ),
    ("a pytest skip call", _NEEDLE_SKIP_CALL),
    ("a pytest skipif marker", _NEEDLE_SKIPIF_MARKER),
    ("a pytest dependency-skip call", _NEEDLE_DEPENDENCY_SKIP_CALL),
)

# init_programmer_framed() body extraction.
_INIT_PROGRAMMER_FRAMED_DEF_RE = re.compile(
    r"\bbool\s+init_programmer_framed\s*\(\s*firestarter_handle_t\s*\*\s*handle\s*\)\s*\{"
)

# Ack pack-layout patterns (Coverage 1-7).
_RETIRED_U16_EMIT_RE = re.compile(
    re.escape(_NEEDLE_RETIRED_U16_EMIT) + r"\s*\(\s*MSG_OK_READY\b"
)
_BYTES_EMIT_RE = re.compile(r"\bLOG_OK_ID_BYTES\s*\(\s*MSG_OK_READY\b")
_READY_BUF_DECL_RE = re.compile(r"\b_ready\s*\[\s*4\s*\+\s*32\s*\+\s*2\s*\]")
_BUDGET_HI_OFFSET_RE = re.compile(r"\b_ready\s*\[\s*4\s*\+\s*_vlen\s*\]")
_BUDGET_LO_OFFSET_RE = re.compile(r"\b_ready\s*\[\s*4\s*\+\s*_vlen\s*\+\s*1\s*\]")
_READY_BARE_INDEX_RE = re.compile(r"\b_ready\s*\[\s*(\d+)\s*\]")
_EMIT_LENGTH_RE = re.compile(
    r"LOG_OK_ID_BYTES\s*\(\s*MSG_OK_READY\s*,\s*_ready\s*,\s*\(uint8_t\)\(\s*4\s*\+\s*_vlen\s*\+\s*2\s*\)\s*\)"
)
_BUDGET_FN_CALL_RE = re.compile(r"\beprom_block_budget_s\s*\(")
_MAX_PULSES_RESTATEMENT_RE = re.compile(re.escape(_NEEDLE_MAX_PULSES_RESTATEMENT))
_ENERGY_CAP_US_RESTATEMENT_RE = re.compile(re.escape(_NEEDLE_ENERGY_CAP_US_RESTATEMENT))
_PGM_READ_RESTATEMENT_RE = re.compile(re.escape(_NEEDLE_PGM_READ_RESTATEMENT))
_HW_REV_BOTH_ARMS_RE = re.compile(
    r"#ifdef\s+HARDWARE_REVISION\s*"
    r"_ready\s*\[\s*2\s*\]\s*=\s*\(uint8_t\)\s*rurp_get_hardware_revision\s*\(\s*\)\s*;\s*"
    r"#else\s*"
    r"_ready\s*\[\s*2\s*\]\s*=\s*0xFE\s*;"
)


def _extract_ack_pack_body(stripped):
    """Locate init_programmer_framed's single definition and extract its
    body by brace-depth counting on the comment-stripped text (the same
    discipline test_hv_routing_source_contract_v142.py's own
    `_extract_command_done_body` uses), so a pack block written ANYWHERE
    ELSE in the file cannot satisfy Coverage 1-7. Returns (body,
    def_match); asserts exactly one definition exists and that the
    brace-matching scan actually closes."""
    matches = list(_INIT_PROGRAMMER_FRAMED_DEF_RE.finditer(stripped))
    assert len(matches) == 1, (
        "expected exactly 1 definition of bool init_programmer_framed("
        "firestarter_handle_t* handle) in the comment-stripped "
        f"{_DISPATCH_REL}, found {len(matches)} -- the ack pack layout can "
        "only be pinned if there is exactly one function body to pin.\n"
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
        "brace-matching scan for init_programmer_framed's body reached EOF "
        f"before the opening brace at {_DISPATCH_REL}:{_line_of(stripped, m.start())} "
        "ever closed -- unbalanced braces in the scanned text."
    )
    return stripped[start : i - 1], m


# Tests -- ack pack-layout source contract (Coverage 1-7).


def test_the_retired_two_byte_ready_emit_is_gone():
    """Coverage 1 -- BF-1's exact defect: the retired two-byte-only ack
    emit must be gone from the pack site. That emit shape is what made a
    v1.31 firmware build unreachable by the v1.31 host: `_probe_port`
    raises `FirmwareOutdatedError` when `firmware_identity` is None, and
    `tests/test_fwguard.py::test_absent_identity_refuses` asserts that
    refusal on purpose. Needle built by concatenation so this module's own
    prose cannot satisfy this check by accident."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_ack_pack_body(stripped)
    hits = list(_RETIRED_U16_EMIT_RE.finditer(body))
    assert hits == [], (
        "found the retired two-byte-only MSG_OK_READY emit still present in "
        f"init_programmer_framed's body ({_DISPATCH_REL}) -- BF-1's exact "
        "defect: a v1.31 firmware build emitting only that ack shape is "
        "refused at connect by the v1.31 host "
        "(test_fwguard.py::test_absent_identity_refuses).\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_exactly_one_byte_blob_ready_emit_exists():
    """Coverage 2 -- exactly one byte-blob-form MSG_OK_READY emit. Two
    emits would make the ack's shape depend on a code path, defeating the
    host's one and only length-based discriminator."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_ack_pack_body(stripped)
    hits = list(_BYTES_EMIT_RE.finditer(body))
    assert len(hits) == 1, (
        "expected exactly 1 byte-blob-form MSG_OK_READY emit inside "
        f"init_programmer_framed's body, found {len(hits)} -- two emits "
        "would make the ack's shape depend on a code path, defeating the "
        "host's one and only length-based discriminator.\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_the_ready_pack_buffer_has_room_for_the_budget():
    """Coverage 3 -- the pack buffer must be sized for the identity tail
    PLUS the two budget bytes. A buffer sized for the identity tail alone
    would overflow by two bytes on a 32-character identity once CAP-03's
    budget is written -- nothing here may be sized from frame content."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_ack_pack_body(stripped)
    hits = list(_READY_BUF_DECL_RE.finditer(body))
    assert len(hits) == 1, (
        "expected exactly 1 '_ready[4 + 32 + 2]'-shaped buffer declaration "
        f"inside init_programmer_framed's body, found {len(hits)} -- a "
        "buffer sized for the identity tail alone would overflow by two "
        "bytes on a 32-character identity once CAP-03's two budget bytes "
        "are written.\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_the_budget_is_written_at_a_computed_offset_not_a_literal():
    """Coverage 4 -- the budget bytes must be written at the COMPUTED
    offset (4 + _vlen / 4 + _vlen + 1), never a literal. D-08's hazard: a
    fixed index works on every board whose identity string happens to be
    one length and silently misreads on the next -- offsets 0-3 are
    genuinely fixed (CAP-01's two bytes plus CAP-02's revision and length
    prefix), so a bare-decimal '_ready[' index above 3 anywhere in the
    body is exactly that hazard."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_ack_pack_body(stripped)
    hi_hits = list(_BUDGET_HI_OFFSET_RE.finditer(body))
    lo_hits = list(_BUDGET_LO_OFFSET_RE.finditer(body))
    assert len(hi_hits) == 1, (
        "expected exactly 1 '_ready[4 + _vlen]' (the budget high byte) "
        f"inside init_programmer_framed's body, found {len(hi_hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    assert len(lo_hits) == 1, (
        "expected exactly 1 '_ready[4 + _vlen + 1]' (the budget low byte) "
        f"inside init_programmer_framed's body, found {len(lo_hits)}.\n"
        f"Body (comment-stripped):\n{body}"
    )
    bare_over_3 = [
        int(match.group(1))
        for match in _READY_BARE_INDEX_RE.finditer(body)
        if int(match.group(1)) > 3
    ]
    assert bare_over_3 == [], (
        "found a bare-decimal '_ready[N]' index with N > 3 inside "
        f"init_programmer_framed's body: {bare_over_3} -- D-08's hazard: a "
        "fixed index works on every board whose identity string happens to "
        "be one length and silently misreads on the next. Indices 0-3 are "
        "genuinely fixed (CAP-01's two bytes plus CAP-02's revision and "
        "length prefix); anything past the variable-length identity tail "
        "must be expressed as 4 + _vlen [+ 1], never a literal.\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_the_emitted_length_accounts_for_the_budget():
    """Coverage 5 -- the byte-blob emit's third argument (the emitted byte
    count) must be (uint8_t)(4 + _vlen + 2). Emitting 4 + _vlen with a
    2-byte budget tail present would silently truncate CAP-03 off the wire
    and leave the host's corresponding attribute None forever -- a SILENT
    capability loss, not a loud one."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_ack_pack_body(stripped)
    hits = list(_EMIT_LENGTH_RE.finditer(body))
    assert len(hits) == 1, (
        "expected exactly 1 MSG_OK_READY byte-blob emit whose third "
        "argument is (uint8_t)(4 + _vlen + 2), inside "
        f"init_programmer_framed's body, found {len(hits)} -- emitting "
        "4 + _vlen with a 2-byte budget tail present would silently "
        "truncate CAP-03 off the wire and leave the host's corresponding "
        "attribute None forever, a SILENT capability loss.\n"
        f"Body (comment-stripped):\n{body}"
    )


def test_the_budget_comes_from_the_shipped_budget_function():
    """Coverage 6 -- D-07: no datasheet-derived value may be duplicated at
    the ack site. The budget must come from CALLING the shipped budget
    function (src/proms/eprom_budget.cpp, unit-tested there), never from a
    hand-rolled restatement. All three forbidden-restatement needles are
    built by concatenation so this module's own source cannot satisfy them
    by accident."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_ack_pack_body(stripped)
    call_hits = list(_BUDGET_FN_CALL_RE.finditer(body))
    assert len(call_hits) >= 1, (
        "expected at least 1 call to the shipped budget function inside "
        f"init_programmer_framed's body, found {len(call_hits)} -- D-07: "
        "the budget must be computed by calling the shipped function, "
        "never restated at the ack site.\n"
        f"Body (comment-stripped):\n{body}"
    )
    for label, pattern in (
        (
            "a hand-rolled restatement of the shipped worst-case pulse-count column",
            _MAX_PULSES_RESTATEMENT_RE,
        ),
        (
            "a hand-rolled restatement of the shipped energy-cap column",
            _ENERGY_CAP_US_RESTATEMENT_RE,
        ),
        (
            "a hand-rolled PROGMEM-byte-read-based restatement",
            _PGM_READ_RESTATEMENT_RE,
        ),
    ):
        hits = list(pattern.finditer(body))
        assert hits == [], (
            f"found {label} inside init_programmer_framed's body -- D-07: "
            "no datasheet-derived value may be duplicated at the ack site; "
            "the arithmetic (already padded per D-09) lives in "
            "src/proms/eprom_budget.cpp and is unit-tested there.\n"
            f"Body (comment-stripped):\n{body}"
        )


def test_the_revision_byte_is_emitted_on_every_build_configuration():
    """Coverage 7 -- the hardware-revision #ifdef/#else arm must assign
    the live revision on one side and a fixed literal on the other, so the
    ack's byte count depends only on the identity-string length. The
    literal (rather than the REVISION_UNKNOWN symbol) is used in the
    no-detection arm because that symbol is itself declared inside the
    same #ifdef and cannot be named outside it."""
    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_ack_pack_body(stripped)
    hits = list(_HW_REV_BOTH_ARMS_RE.finditer(body))
    assert len(hits) >= 1, (
        "expected the ack pack block's HARDWARE_REVISION-gated arm to "
        "assign the live hardware revision and its opposite arm to assign "
        "the fixed 0xFE literal, inside init_programmer_framed's body -- "
        f"found {len(hits)} such pairs. The literal (not the "
        "REVISION_UNKNOWN symbol) is used in the opposite arm because that "
        "symbol is itself declared inside the same #ifdef and cannot be "
        "named there.\n"
        f"Body (comment-stripped):\n{body}"
    )


# Tests -- self-protection (Coverage 8-10).


def test_scan_targets_are_non_vacuous():
    """Coverage 8 -- structural self-check, two halves.

    Part (a): the DEFAULT scan target is recomputed fresh from _REPO_ROOT
    WITHOUT reading os.environ (the check_permitted_claims.py
    `_HERE`-resolves-to-the-wrong-directory landmine, closed here by
    construction, the same technique
    test_hv_routing_source_contract_v142.py's own Coverage 14 uses):
    exists, non-empty, resolves inside this repository, and has non-empty
    comment-stripped text.

    Part (b): the body-extractor, run against whatever `_SCAN_DISPATCH`
    currently resolves to (the SAME seam-aware target every leg above
    scans), must itself yield a non-empty body. This is the half a
    planted EMPTY scratch file (pointed at via
    FIRESTARTER_ACK_SCAN_DISPATCH_SOURCE) turns RED, proving a
    brace-matcher that silently returned "" cannot make every
    negative-assertion leg above (1 and 6's needle-absence halves) pass
    vacuously."""
    default_dispatch = _REPO_ROOT / _DISPATCH_REL
    assert default_dispatch.is_file(), (
        f"default {_DISPATCH_REL} scan target {default_dispatch} does not "
        "exist on disk -- a missing scan target must FAIL, never silently "
        "pass."
    )
    assert default_dispatch.stat().st_size > 0, (
        f"default {_DISPATCH_REL} scan target {default_dispatch} is empty"
    )
    assert default_dispatch.resolve().is_relative_to(_REPO_ROOT), (
        f"default {_DISPATCH_REL} scan target {default_dispatch} resolves "
        f"outside _REPO_ROOT ({_REPO_ROOT}) -- a naive future copy of this "
        "module into another directory must fail loudly here, not scan "
        "nothing and exit 0."
    )
    default_stripped = _strip_comments(default_dispatch.read_text())
    assert default_stripped.strip() != "", (
        f"comment-stripped {_DISPATCH_REL} (default target) is empty -- "
        "nothing would ever be scanned by this module's other legs."
    )

    stripped = _strip_comments(_SCAN_DISPATCH.read_text())
    body, _ = _extract_ack_pack_body(stripped)
    assert body.strip() != "", (
        "the extracted init_programmer_framed body (from the CURRENT scan "
        f"target {_SCAN_DISPATCH}) is empty -- a brace-matcher that "
        "silently returns an empty body would make every negative-"
        "assertion leg in this module (Coverage 1 and 6's needle-absence "
        "halves) pass VACUOUSLY."
    )


def test_this_module_cannot_be_silently_skipped():
    """Coverage 9 -- this module's own source contains no runtime
    skip-bypass call, no skip-marker decorator, and no dependency-skip
    call anywhere, so the fail-closed contract this module documents is
    self-enforcing rather than merely stated. All three needles are built
    via concatenation, the same technique
    test_hv_routing_source_contract_v142.py's own Coverage 15 uses, so
    this test's own source and its own failure messages cannot match its
    own check."""
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
    """Coverage 10 -- the concatenated-needle self-check: each of the
    seven needles built by concatenation above (Coverage 1, 6 and 9) must
    appear NOWHERE verbatim in this module's own source, including inside
    this very test's failure messages. Without this leg, a future edit
    could silently un-concatenate one of the needles (for example while
    "simplifying" the code) and this discipline would quietly stop being
    machine-checked."""
    own_text = Path(__file__).read_text()
    for label, needle in _ALL_SELF_CHECK_NEEDLES:
        assert needle not in own_text, (
            f"the concatenation-built needle for {label} appears verbatim "
            "in this module's own source -- rebuild it from at least two "
            "literal pieces so this gate cannot match itself."
        )
