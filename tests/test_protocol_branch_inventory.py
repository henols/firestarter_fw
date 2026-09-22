"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 140 Plan 02 -- TABLE-05 (D-12, D-13): the firmware half of the "no
second algorithm selector" gate. Pins a two-tier inventory of every branch
predicate in the EPROM write path that reads a handle field, so a NEW
selector -- or an existing one changing shape -- fails an exit code rather
than surviving an inspection. TABLE-05's own wording rejects verification
by inspection; this module is the mechanizable half of that requirement.

Requirements: TABLE-05

Defect class this closes: a second protocol/algorithm selector, or any new
handle-field branch, entering src/proms/eprom.cpp (in this phase, or in
Phase 141 or 142) without anyone noticing. A green build or a line-count
diff cannot see that class of change; only a positional re-parse against a
pinned, reasoned inventory can.

Coverage:
  1. test_blob_shas_match_the_recorded_inventory -- `git rev-parse
     HEAD:<path>` for both scanned source files equals the recorded
     meta.blob_shas entry.
  2. test_branch_sites_match_the_recorded_inventory -- the live re-parse of
     src/proms/eprom.cpp equals the recorded `sites` array positionally on
     (line, predicate, keyed_on, tier); the assertion names the FIRST
     divergence.
  3. test_exactly_three_protocol_keyed_sites_at_the_pinned_lines -- the
     live re-parse yields exactly three tier-"protocol" sites, at lines
     71, 145 and 218.
  4. test_inventory_is_non_vacuous -- the recorded inventory has >= 24
     sites, every one carries a non-empty predicate and reason, both scan
     targets exist and are non-empty, and the live re-parse of eprom.cpp
     returns a non-zero predicate count.
  5. test_params_table_has_no_second_selector -- src/proms/eprom_params.cpp
     is comment-stripped and shown to contain zero `switch` statements,
     exactly one `pgm_read_byte(&EPROM_PARAM_KEYS[...]) == protocol`
     comparison, and EPROM_PARAM_KEYS initialised with exactly the three
     keys 0x07 / 0x08 / 0x0B.
  6. test_default_targets_resolve_inside_this_repository -- the DEFAULT
     scan targets (recomputed from _REPO_ROOT, never from the environment)
     exist, are non-empty, and resolve inside this repository -- the
     check_permitted_claims.py `_HERE`-resolves-to-the-wrong-directory
     landmine, closed for this module by construction.
  7. test_git_is_required_not_optional -- this module's own source
     contains no runtime skip-bypass call and no skip-marker decorator
     anywhere.

Environment seams (this repository has no central environment-variable
inventory -- this docstring is the only place a reader can discover them):
  - FIRESTARTER_BRANCH_SCAN_SOURCE -- overrides the scanned
    src/proms/eprom.cpp path. Consulted by tests 2, 3 and 4.
  - FIRESTARTER_BRANCH_SCAN_PARAMS_SOURCE -- overrides the scanned
    src/proms/eprom_params.cpp path. Consulted by tests 4 and 5.
  Both bind at IMPORT time (module-level `Path(os.environ.get(...))`
  expressions below), so a planted-violation run must set them in a CHILD
  PROCESS environment before this module is imported, never via a
  post-import monkeypatch. Both seams exist ONLY so a planted violation can
  be scanned without touching either repository (D-15). Tests 1 and 6
  ignore them BY CONSTRUCTION: test 1 resolves blobs via `git` against the
  real repository tree regardless of any seam, and test 6 recomputes the
  default target paths directly from _REPO_ROOT without ever reading
  os.environ -- so a stray seam value left set in CI cannot make either
  test pass vacuously; at worst it makes tests 2/3/4/5 fail loudly.

This module never imports check_*.py machinery: it is a standalone pytest
module, self-contained, with NO conftest.py (firestarter/tests/ has no
conftest.py anywhere in the repo -- a recorded house-rule pattern decision,
not an omission). It deliberately lives under tests/, is not named
check_*.py, and adds no fixtures under tests/fixtures/, so it stays outside
test_checker_convention.py's glob and incurs none of that convention's
extra obligations (a paired fixtures directory, a raised FLOOR /
FIXTURE_FLOOR, etc. -- see tests/test_checker_convention.py). It also does
not parameterise, import from, or edit any pre-existing gate module in any
way -- the extraction logic below is its own independent re-derivation of
this plan's <gate_specification>, not a shared helper.
"""

import json
import os
import re
import shutil
import subprocess
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_EPROM_REL = "src/proms/eprom.cpp"
_PARAMS_REL = "src/proms/eprom_params.cpp"
_INVENTORY_JSON = _HERE / "golden" / "protocol_branch_inventory.json"

# Environment seams -- bind at IMPORT time. See module docstring
# "Environment seams" section above. Tests 1 and 6 deliberately never read
# either of these.
_SCAN_EPROM = Path(
    os.environ.get("FIRESTARTER_BRANCH_SCAN_SOURCE", _REPO_ROOT / _EPROM_REL)
)
_SCAN_PARAMS = Path(
    os.environ.get(
        "FIRESTARTER_BRANCH_SCAN_PARAMS_SOURCE", _REPO_ROOT / _PARAMS_REL
    )
)

_HANDLE_FIELD_RE = re.compile(r"handle->(\w+)")
_KEYWORD_RE = re.compile(r"\b(if|while|switch)\b")
_FOR_RE = re.compile(r"\bfor\b")
_SWITCH_TOKEN_RE = re.compile(r"\bswitch\b")
_KEY_COMPARE_RE = re.compile(
    r"pgm_read_byte\s*\(\s*&\s*EPROM_PARAM_KEYS\s*\[[^\]]*\]\s*\)\s*==\s*protocol"
    r"|protocol\s*==\s*pgm_read_byte\s*\(\s*&\s*EPROM_PARAM_KEYS\s*\[[^\]]*\]\s*\)"
)
_KEYS_DECL_RE = re.compile(
    r"EPROM_PARAM_KEYS\s*\[\s*\]\s*PROGMEM\s*=\s*\{([^}]*)\}"
)


def _resolve_git():
    """Resolve the `git` binary, fail-closed.

    Deliberately never bypassed via any decorator or runtime call that
    would mark this outcome as skipped, anywhere in this module: a missing
    `git` turning this gate into a silent skip would be exactly the defect
    class TABLE-05 exists to close. If `git` (or $GIT) cannot be resolved
    via shutil.which, this raises via a plain assert, which the test
    runner reports as a FAILURE, never a skipped outcome.
    """
    git_bin = shutil.which(os.environ.get("GIT", "git"))
    assert git_bin is not None, (
        "git not found on PATH (checked $GIT, falling back to 'git'). This "
        "must FAIL the suite, never be silently skipped -- a missing git "
        "would otherwise turn TABLE-05's blob-identity leg into a silent "
        "no-op."
    )
    return git_bin


def _git(*args):
    """Run `git <args>` as a real subprocess (list-form argv, invoked
    directly rather than through a shell) against _REPO_ROOT -- NEVER
    either env-seam path -- and assert a clean exit. Returns stdout,
    stripped."""
    git_bin = _resolve_git()
    result = subprocess.run(
        [git_bin, *args],
        cwd=str(_REPO_ROOT),
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, (
        f"git {' '.join(args)} failed (exit {result.returncode}).\n"
        f"stderr:\n{result.stderr}"
    )
    return result.stdout.strip()


def _strip_comments_and_literals(text):
    """Strip `//` and `/* */` comments and the contents of string/char
    literals, replacing every stripped span with whitespace of the same
    shape so every line number is preserved exactly. This is a lexical
    scan, not a preprocessor -- `#ifdef` / `#endif` are left as plain text
    and participate in no special handling."""
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


def _find_matching_paren(text, open_idx):
    """Bracket-matched (not flat-regex) scan for the `)` closing the `(` at
    `open_idx` -- branch conditions in this file nest parentheses."""
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def _line_of(text, idx):
    return text.count("\n", 0, idx) + 1


def _normalize_ws(s):
    return re.sub(r"\s+", " ", s).strip()


def _split_top_level_semicolons(s):
    """Split a for(...)'s inner text on `;` at paren/bracket/brace depth 0,
    so a function call inside the init or step clause is not mistaken for
    a clause separator."""
    parts = []
    depth = 0
    current = []
    for ch in s:
        if ch in "([{":
            depth += 1
            current.append(ch)
        elif ch in ")]}":
            depth -= 1
            current.append(ch)
        elif ch == ";" and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(ch)
    parts.append("".join(current))
    return parts


def _keyed_on_for(span_text):
    """The sorted set of `handle-><field>` names in `span_text`, plus the
    literal tokens this plan's <gate_specification> assigns to the three
    handle-taking predicate helpers."""
    fields = set(_HANDLE_FIELD_RE.findall(span_text))
    if "is_flag_set(" in span_text:
        fields.add("ctrl_flags")
    if "using_p1_as_vpp(" in span_text:
        fields.add("pins+bus_config.vpp_line")
    if "is_operation_in_progress(" in span_text:
        fields.add("operation_state")
    return sorted(fields)


def _is_relevant(span_text):
    if "handle->" in span_text:
        return True
    return any(
        call in span_text
        for call in ("is_flag_set(", "using_p1_as_vpp(", "is_operation_in_progress(")
    )


def _extract_predicates(text):
    """Independent, live re-derivation of every branch-condition span in
    `text`, per this plan's <gate_specification> extraction rule: the
    parenthesised condition of if / else-if / while / switch, the middle
    clause of `for (init; COND; step)`, and every ternary `?:` condition --
    kept only when it references a handle field or one of the three
    predicate helpers. Returns an ascending-line-ordered list of
    {line, predicate, keyed_on, tier} dicts."""
    stripped = _strip_comments_and_literals(text)
    n = len(stripped)
    raw_spans = []  # (start_idx, line, predicate_text, detect_text)

    for m in _KEYWORD_RE.finditer(stripped):
        keyword = m.group(1)
        j = m.end()
        while j < n and stripped[j].isspace():
            j += 1
        if j >= n or stripped[j] != "(":
            continue
        close = _find_matching_paren(stripped, j)
        if close == -1:
            continue
        cond = stripped[j + 1 : close]
        raw_spans.append(
            (m.start(), _line_of(stripped, m.start()),
             _normalize_ws(f"{keyword} ({cond})"), cond)
        )

    for m in _FOR_RE.finditer(stripped):
        j = m.end()
        while j < n and stripped[j].isspace():
            j += 1
        if j >= n or stripped[j] != "(":
            continue
        close = _find_matching_paren(stripped, j)
        if close == -1:
            continue
        parts = _split_top_level_semicolons(stripped[j + 1 : close])
        if len(parts) != 3:
            continue  # not a classic 3-clause for; skip defensively
        cond = parts[1]
        raw_spans.append(
            (m.start(), _line_of(stripped, m.start()), _normalize_ws(cond), cond)
        )

    i = 0
    while i < n:
        is_ternary_q = (
            stripped[i] == "?"
            and (i + 1 >= n or stripped[i + 1] != ":")
            and (i == 0 or stripped[i - 1] != "?")
        )
        if is_ternary_q:
            j = i - 1
            depth = 0
            start = 0
            while j >= 0:
                ch = stripped[j]
                if ch in ")]":
                    depth += 1
                elif ch in "([":
                    depth -= 1
                    if depth < 0:
                        start = j + 1
                        break
                elif depth == 0 and ch in ";{},":
                    start = j + 1
                    break
                j -= 1
            else:
                start = 0
            cond = stripped[start:i]
            raw_spans.append((i, _line_of(stripped, i), _normalize_ws(cond), cond))
        i += 1

    kept = []
    for start_idx, line, predicate, detect_text in raw_spans:
        if _is_relevant(detect_text):
            kept.append(
                {
                    "line": line,
                    "predicate": predicate,
                    "keyed_on": _keyed_on_for(detect_text),
                    "start_idx": start_idx,
                }
            )

    kept.sort(key=lambda s: (s["line"], s["start_idx"]))
    for s in kept:
        s["tier"] = "protocol" if "protocol" in s["keyed_on"] else "other"
        del s["start_idx"]
    return kept


def _scan_params_table(text):
    """Comment-stripped scan of eprom_params.cpp: switch-statement count,
    `pgm_read_byte(&EPROM_PARAM_KEYS[...]) == protocol` comparison count,
    and the literal keys EPROM_PARAM_KEYS is initialised with. Comment
    stripping matters here: the real file's own docstring uses the English
    word "switch" twice while explaining that it contains none."""
    stripped = _strip_comments_and_literals(text)
    switch_count = len(_SWITCH_TOKEN_RE.findall(stripped))
    key_comparisons = len(_KEY_COMPARE_RE.findall(stripped))
    m = _KEYS_DECL_RE.search(stripped)
    keys = [k.strip() for k in m.group(1).split(",") if k.strip()] if m else []
    return {
        "switch_statements": switch_count,
        "key_comparisons": key_comparisons,
        "keys": keys,
    }


def _load_inventory():
    return json.loads(_INVENTORY_JSON.read_text())


# Tests


def test_blob_shas_match_the_recorded_inventory():
    inventory = _load_inventory()
    for rel_path, recorded_sha in inventory["meta"]["blob_shas"].items():
        observed_sha = _git("rev-parse", f"HEAD:{rel_path}")
        extra = (
            " -- a mismatch here also means D-10's byte-unchanged guarantee "
            "for src/proms/eprom.cpp has been broken this phase."
            if rel_path == _EPROM_REL
            else ""
        )
        assert observed_sha == recorded_sha, (
            f"{rel_path} blob SHA changed -- recorded={recorded_sha} "
            f"observed={observed_sha}. If this file legitimately changed, "
            "re-derive tests/golden/protocol_branch_inventory.json from the "
            "new file (never hand-edit the SHA) and state in the commit "
            f"message which site changed and why.{extra}"
        )


def test_branch_sites_match_the_recorded_inventory():
    inventory = _load_inventory()
    recorded = [
        (s["line"], s["predicate"], s["keyed_on"], s["tier"])
        for s in inventory["sites"]
    ]
    live = [
        (s["line"], s["predicate"], s["keyed_on"], s["tier"])
        for s in _extract_predicates(_SCAN_EPROM.read_text())
    ]

    n = min(len(recorded), len(live))
    for i in range(n):
        if recorded[i] != live[i]:
            raise AssertionError(
                f"first divergence at index {i} -- recorded={recorded[i]!r}, "
                f"live={live[i]!r}. A NEW branch site, a changed predicate "
                "spelling, or a changed keyed_on/tier is exactly the class "
                "of change TABLE-05's gate exists to catch."
            )
    assert len(recorded) == len(live), (
        f"site count diverged after {n} matching entries -- "
        f"recorded_count={len(recorded)} live_count={len(live)}"
    )


def test_exactly_one_protocol_keyed_site_at_the_pinned_line():
    """Phase 142 Plan 04 (D-05): the eprom_hv_route_mask resolver collapsed
    the two duplicated protocol-keyed VPP-route forks (formerly tier-1
    sites at :190 and :340) into a single call site apiece, leaving only
    the pulse-fallback switch as a protocol-keyed branch. This locator is
    now STRICTLY STRONGER than its three-site predecessor: with only one
    legitimate tier-1 site, ANY second protocol-keyed branch is a
    violation, where before three were permitted.

    Phase 204 Plan 03 (FWCMD-01): the pinned line moved from 70 to 67 --
    configure_eprom's `case CMD_BLANK_CHECK:` arm (three lines, no
    predicate of its own) was deleted above the pulse-fallback switch when
    the standalone blank-check command's wire ordinal was retired in
    3.1.0. The switch itself is untouched; only its line number shifted.
    A number is a measurement of the live source, not a constant to be
    carried forward by habit -- re-derive it with the extractor at commit
    time if this file's diff ever grows again."""
    live = _extract_predicates(_SCAN_EPROM.read_text())
    protocol_lines = sorted(s["line"] for s in live if s["tier"] == "protocol")
    assert protocol_lines == [67], (
        "expected exactly one tier-protocol site, at line [67], found "
        f"{protocol_lines} instead. More than [67] means a SECOND "
        "protocol-keyed branch has appeared -- a second algorithm selector "
        "and a TABLE-05 violation, to be fixed in src/proms/eprom.cpp, "
        "never here. An empty list means the pulse-fallback switch was "
        "removed without updating this inventory."
    )


def test_inventory_is_non_vacuous():
    inventory = _load_inventory()
    sites = inventory["sites"]
    assert len(sites) >= 21, (
        f"non-vacuous guard: expected >= 21 recorded sites, got "
        f"{len(sites)} -- an empty or truncated inventory must FAIL, not "
        "silently pass."
    )
    for s in sites:
        assert s.get("predicate") and s.get("reason"), (
            f"non-vacuous guard: site at line {s.get('line')!r} is missing "
            "a non-empty predicate or reason."
        )

    targets = (_SCAN_EPROM, _SCAN_PARAMS)
    existing_nonempty = [p for p in targets if p.is_file() and p.stat().st_size > 0]
    sizes = {str(p): (p.stat().st_size if p.is_file() else None) for p in targets}
    assert len(existing_nonempty) == 2, (
        "non-vacuous guard: expected exactly 2 scan targets to exist and "
        f"be non-empty, found {len(existing_nonempty)} of 2 -- sizes="
        f"{sizes}. A vacuous (missing or empty) scan target must FAIL, "
        "never silently pass as if nothing needed checking."
    )

    live = _extract_predicates(_SCAN_EPROM.read_text())
    assert len(live) > 0, (
        f"non-vacuous guard: the live re-parse of {_SCAN_EPROM} returned "
        f"{len(live)} predicates -- a zero-predicate scan must FAIL, never "
        "read as 'nothing to report'."
    )


def test_params_table_has_no_second_selector():
    inventory = _load_inventory()
    recorded = inventory["params_table"]
    live = _scan_params_table(_SCAN_PARAMS.read_text())

    assert live["switch_statements"] == 0 == recorded["switch_statements"], (
        f"{_SCAN_PARAMS} contains {live['switch_statements']} switch "
        "statement(s) after comment-stripping -- a switch in the params "
        "table's own translation unit IS the second dispatch selector "
        "TABLE-05 forbids."
    )
    assert live["key_comparisons"] == 1 == recorded["key_comparisons"], (
        "expected exactly 1 pgm_read_byte(&EPROM_PARAM_KEYS[...]) == "
        f"protocol comparison, found {live['key_comparisons']} in "
        f"{_SCAN_PARAMS}."
    )
    assert live["keys"] == ["0x07", "0x08", "0x0B"] == recorded["keys"], (
        f"EPROM_PARAM_KEYS is initialised with {live['keys']!r}, expected "
        "['0x07', '0x08', '0x0B'] -- a key added or removed here changes "
        "which protocols the table covers without changing the dispatch "
        "switch at eprom.cpp:71, which TABLE-05's inventory would then "
        "miss."
    )


def test_default_targets_resolve_inside_this_repository():
    """The check_permitted_claims.py `_HERE`-resolves-to-the-wrong-
    directory landmine, closed for this module: recompute the DEFAULT
    targets fresh from _REPO_ROOT, WITHOUT reading os.environ at all, and
    assert they are real, non-empty files located inside this repository.
    """
    default_eprom = _REPO_ROOT / _EPROM_REL
    default_params = _REPO_ROOT / _PARAMS_REL

    for label, p in (
        ("eprom.cpp", default_eprom),
        ("eprom_params.cpp", default_params),
    ):
        assert p.is_file(), f"default {label} target {p} does not exist on disk"
        assert p.stat().st_size > 0, f"default {label} target {p} is empty"
        assert p.resolve().is_relative_to(_REPO_ROOT), (
            f"default {label} target {p} resolves outside _REPO_ROOT "
            f"({_REPO_ROOT}) -- this is exactly the _HERE-resolves-to-the-"
            "wrong-directory trap; a naive future copy of this module into "
            "another directory must fail loudly here, not scan nothing and "
            "exit 0."
        )

    assert _INVENTORY_JSON.parent == _HERE / "golden", (
        f"_INVENTORY_JSON ({_INVENTORY_JSON}) does not resolve under "
        f"{_HERE / 'golden'}"
    )
    assert _INVENTORY_JSON.resolve().is_relative_to(_HERE.resolve()), (
        f"_INVENTORY_JSON ({_INVENTORY_JSON}) resolves outside this "
        f"module's own directory ({_HERE})"
    )


def test_git_is_required_not_optional():
    """Self-checking: this module's own source contains no runtime
    skip-bypass call and no skip-marker decorator, i.e. the fail-closed
    contract for a missing `git` (see _resolve_git) is self-enforcing
    rather than merely documented."""
    this_source = Path(__file__).read_text()
    for line in this_source.splitlines():
        stripped = line.strip()
        # Real usage of either construct always starts a line -- never
        # embedded mid-string -- so startswith() correctly identifies
        # actual code while never self-matching this very check's own
        # prose (docstring text and f-string messages never START a line
        # with either literal).
        assert not stripped.startswith("pytest.skip"), (
            f"found a skip-bypass call at: {line!r} -- git absence must "
            "FAIL this suite, never take this bypass."
        )
        assert not stripped.startswith("@pytest.mark.skipif"), (
            f"found a skip-marker decorator at: {line!r} -- git absence "
            "must FAIL this suite, never skip it."
        )
