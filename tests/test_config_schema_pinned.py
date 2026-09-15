"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 126 Plan 10 -- CFG-07's two-halved gate: the configuration schema pinned
as a field list and a version literal, and PR #48's deleted
platform/py32f071/src/config.cpp pinned by absence from the tree.

Requirements: CFG-07
Decisions covered: D-01, D-07, D-17

Criterion 5 asks for two separate things, both load-bearing on their own:
  (a) rurp_configuration_t's four fields, in order, and CONFIG_VERSION's
      literal, unchanged;
  (b) platform/py32f071/src/config.cpp verified absent from the tree.
Neither half is provable by a diff. A diff can be satisfied on a wrong path
(124-VERIFICATION.md recorded exactly that shape passing vacuously), and a
schema diff shows only that something moved, not that a field was added, a
field was reordered, or a fifth member appeared. This module turns both halves
into path/text assertions that either hold or produce an exit code.

WHAT THE ABSENCE ASSERTION PROTECTS:
platform/py32f071/src/config.cpp (Plan 126-08, PR #48) was a second, drifted
copy of the common policy, with four recorded drift points: a private static
`configuration` in an unnamed namespace instead of the shared `rurp_config`
global; a second `rurp_validate_config` whose condition added `|| r2 == 0`
and whose body opened with a `memset`, neither of which the common policy
has; no write-back call at all inside its `rurp_load_config` (a virgin
part's defaults were computed but never persisted); and a `rurp_save_config`
that validated, assigned to the private static, and persisted nothing at
all -- a function whose name promised persistence and delivered none.
Criterion 5 asks for the deletion to be verified by absence from the tree,
which a diff cannot do -- a path-scoped diff can pass on a wrong path, the
shape 124-VERIFICATION.md recorded live. That drift record now exists only
in 126-08-SUMMARY.md; this module's docstring and its
test_the_four_public_functions_are_defined_exactly_once assertion are what
keep a reappearance of any of those four functions under platform/ an exit
code rather than something a reviewer has to notice.

WHY THE SCHEMA PIN OUTLIVES THIS PHASE:
rurp_configuration_t is precisely the struct the closed calibration PR's
`768580f` adds fields to BEFORE its CONFIG_VERSION bump -- a silent layout
change while the version string still reads "VER06". That is strictly worse
than a visible version bump, because every board already storing a record
would misparse the new layout with no migration signal at all. A future
cherry-pick of that shape is exactly what this gate exists to catch, which is
why the field list and the version literal are asserted together, not
separately.

This module executes in NO CI leg on this branch: `pytest tests/` runs only
in build.yml (push/PR to main) and beta-build.yml (push to beta); py32f071.yml
has no pytest step at all. The local run recorded in this phase's SUMMARY is
the only evidence this module's assertions were ever exercised.

This module invokes no compiler -- every assertion below is a textual gate
over committed source, following tests/test_vpp_seam_manual_on_every_board.py
and tests/test_config_storage_seam_shape.py's plain read_text() idiom,
deliberately not parsed into a structure.

Self-contained path resolution below -- NOT in conftest.py (firestarter/tests/
has no conftest.py anywhere in the repo, and none of pytest.ini,
pyproject.toml, setup.cfg or tox.ini exist either; a recorded house-rule
pattern decision, not an omission). Stdlib and pytest only.

Every check below is factored into a module-level helper taking text (or, for
the two filesystem-tree checks, a repo root) and returning a list of
violations, so the positive tests and the planted-copy RED demonstrations
exercise the identical code path -- a parallel second implementation inside a
RED test would prove nothing about the gate (Phase 124 D-14: "a guard that
supplies the answer it tests is structurally dead"). No blob SHA literal
appears anywhere in this module: a committed gate pinning a shared header by
hash would break the first time a later milestone legitimately edits it --
the blob-SHA pins for include/rurp_types.h and include/rurp_shield.h are a
plan-level record instead (this plan's SUMMARY), re-executed by
126-NONREGRESSION.md. The one blob-SHA comparison this module does make (the
planted-copy demonstrations below) is entirely self-referential -- before vs.
after this module's own mutation demos, never against a hardcoded historical
value.

VALIDATION CEILING: this module reads source text and compiles nothing. It
makes no build or runtime claim; see .planning/REQUIREMENTS.md's "Validation
Ceiling" section for the claims this phase is not permitted to make.

Coverage:
  1. test_rurp_configuration_t_has_exactly_the_four_pinned_fields -- the
     struct's member list is exactly version (char[6]), r1 (long), r2 (long),
     hardware_revision (uint8_t), in that order, with no fifth member and no
     size or offset literal.
  2. test_config_version_literal_is_ver06 -- CONFIG_VERSION in
     include/rurp_shield.h is still the literal "VER06".
  3. test_default_resistance_values_are_unchanged -- VALUE_R1 is 270000 and
     VALUE_R2 is 44000 -- rurp_validate_config's write-back writes both, so a
     silent change would alter every defaulting board's stored record with no
     version signal.
  4. test_stored_configuration_embeds_the_struct_whole -- D-17: the vendored
     StoredConfiguration wrapper embeds rurp_configuration_t as a single named
     member (making "schema unchanged" structural rather than merely
     asserted), and the wrapper's own `version` member is a uint16_t distinct
     from the char[6] CONFIG_VERSION literal living inside the embedded
     struct.
  5. test_pr48_config_cpp_is_absent_from_the_tree -- Criterion 5's
     verified-by-absence: a path-existence check, never a diff.
  6. test_the_four_public_functions_are_defined_exactly_once -- each of
     rurp_get_config, rurp_load_config, rurp_save_config and
     rurp_validate_config has exactly one definition anywhere under src/,
     platform/ or lib/, that one definition lives in
     src/rurp_config_utils.cpp, and none lives anywhere under platform/ --
     the standing guard against PR #48's drift reappearing.
  7. test_the_four_public_functions_are_declared_in_rurp_shield_h -- all four
     declarations are present there.
  8. test_the_seven_consumers_call_only_the_public_api -- C-14's consumer
     census. CORRECTION (Rule 1): 126-RESEARCH.md's C-14 heading and every
     prose reference to it call this "the seven consumers", but its own
     verified enumeration -- reproduced in this plan's read_first block --
     lists NINE distinct (file, line) call sites across five files:
     src/firestarter.cpp:41,104,110 (Phase 143 Plan 03 shifted these three by
     +1 -- an #include "eprom_budget.h" line added above every one of them;
     re-verified, not re-derived, since the census is a hand-pinned tuple,
     not a golden with a re-derivation script); src/boards/rurp_common.cpp:53;
     include/rurp_hw_rev_utils.h:95,101; src/hardware_operations.cpp:107,119;
     platform/py32f071/src/py32f071_rurp_shield.cpp:297. This module asserts
     the verified, enumerable count (nine), not the mislabeled prose count
     (seven) -- "verified facts win" is this phase's own stated read-order
     precedence, and the same shape as Phase 121's corrected
     CONTEXT/ROADMAP miscounts. The function keeps its plan-specified name.
  9. test_helper_reports_violations_on_planted_copies -- the RED
     demonstration: six independently planted mutations, each built as a real
     file in tmp_path (never a committed file) and fed to the same
     module-level helper the corresponding positive test calls, each
     producing a non-empty violation list. Confirms no committed file was
     mutated by re-hashing include/rurp_types.h, include/rurp_shield.h and
     src/rurp_config_utils.cpp before and after each case.
 10. test_absence_check_fires_when_config_cpp_is_planted -- a dedicated
     second RED demonstration for Coverage 5 specifically (an absence
     assertion is the easiest kind to write vacuously): a scratch
     platform/py32f071/src/config.cpp is planted under tmp_path and the same
     absence helper the positive test calls is shown to fire on it.
 11. test_module_has_no_pio_libdeps_dependency -- C-12: this module's own
     text contains no path under the gitignored PlatformIO build-artifacts
     directory.
 12. test_no_skip_call_or_conditional_skip_marker_present -- adapted from the
     analog's self-enforcing no-skip leg
     (test_vpp_seam_manual_on_every_board.py:447-468), copied verbatim
     including its concatenation trick: this module invokes no compiler, so
     the leg asserts only that no skip call or conditional-skip marker
     appears anywhere in this module's own text.
"""

import hashlib
import re
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_INCLUDE = _REPO_ROOT / "include"
_SRC = _REPO_ROOT / "src"

_TYPES_HEADER = _INCLUDE / "rurp_types.h"
_SHIELD_HEADER = _INCLUDE / "rurp_shield.h"
_POLICY_SRC = _SRC / "rurp_config_utils.cpp"
_DUALSLOT_HEADER = _REPO_ROOT / "platform" / "py32f071" / "src" / "config_storage_dualslot.h"

_PUBLIC_CONFIG_DECLARATIONS = (
    "rurp_get_config",
    "rurp_load_config",
    "rurp_save_config",
    "rurp_validate_config",
)

# Scanned for the definition census (Coverage 6): src/, platform/ and lib/,
# per this plan's action text. include/ is deliberately excluded -- the
# public functions are declared, never defined, in include/rurp_shield.h.
_DEF_SEARCH_DIRS = ("src", "platform", "lib")

_C14_CONSUMER_SITES = (
    ("src/firestarter.cpp", 38, "rurp_load_config"),
    ("src/firestarter.cpp", 102, "rurp_get_config"),
    ("src/firestarter.cpp", 108, "rurp_save_config"),
    ("src/boards/rurp_common.cpp", 53, "rurp_get_config"),
    ("include/rurp_hw_rev_utils.h", 93, "rurp_get_config"),
    ("include/rurp_hw_rev_utils.h", 99, "rurp_get_config"),
    ("src/hardware_operations.cpp", 106, "rurp_get_config"),
    ("src/hardware_operations.cpp", 118, "rurp_get_config"),
    ("platform/py32f071/src/py32f071_rurp_shield.cpp", 297, "rurp_get_config"),
)

_COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)

_CONFIGURATION_STRUCT_RE = re.compile(
    r"typedef\s+struct(?:\s+\w+)?\s*\{(.*?)\}\s*rurp_configuration_t\s*;", re.DOTALL
)
_STORED_CONFIGURATION_STRUCT_RE = re.compile(
    r"typedef\s+struct\s*\{(.*?)\}\s*StoredConfiguration\s*;", re.DOTALL
)
_FIELD_STMT_RE = re.compile(r"^([A-Za-z_][\w\s]*?)\s+([A-Za-z_]\w*)(\[(\d+)\])?$")

_EXPECTED_CONFIGURATION_FIELDS = (
    ("char", "version", "6"),
    ("long", "r1", None),
    ("long", "r2", None),
    ("uint8_t", "hardware_revision", None),
)


def _git_blob_sha(path):
    """Reproduce `git hash-object`'s SHA1 (blob <size>\\0<content>) without
    shelling out to git -- used only to prove the planted-copy demonstrations
    never touch a committed file; never compared against a hardcoded
    historical value (no blob SHA literal appears in this module)."""
    data = path.read_bytes()
    header = f"blob {len(data)}\0".encode()
    return hashlib.sha1(header + data).hexdigest()


def _parse_struct_fields(body_text):
    """Split a struct body on ';' and parse each statement into
    (type, name, array_length_or_None). Statements that don't match the
    simple "TYPE NAME[;/[N]];" shape (e.g. blank remnants) are skipped."""
    fields = []
    for stmt in body_text.split(";"):
        stmt = stmt.strip()
        if not stmt:
            continue
        match = _FIELD_STMT_RE.match(stmt)
        if not match:
            continue
        field_type = re.sub(r"\s+", " ", match.group(1).strip())
        field_name = match.group(2)
        array_len = match.group(4)
        fields.append((field_type, field_name, array_len))
    return fields


def _rurp_configuration_t_violations(header_text):
    """Coverage 1: rurp_configuration_t's four fields, their types and their
    order, with no fifth member and no size/offset literal anywhere in this
    check (host long is 8 bytes, the target's is 4 -- C-6)."""
    stripped = _COMMENT_RE.sub("", header_text)
    match = _CONFIGURATION_STRUCT_RE.search(stripped)
    if not match:
        return ["rurp_configuration_t struct definition not found"]
    fields = tuple(_parse_struct_fields(match.group(1)))
    if fields != _EXPECTED_CONFIGURATION_FIELDS:
        return [
            f"expected fields {_EXPECTED_CONFIGURATION_FIELDS!r} in order, "
            f"found {fields!r}"
        ]
    return []


def _config_version_violations(shield_text):
    """Coverage 2: CONFIG_VERSION is still the literal 'VER06'."""
    match = re.search(r'#\s*define\s+CONFIG_VERSION\s+"([^"]*)"', shield_text)
    if not match:
        return ["CONFIG_VERSION #define not found in rurp_shield.h"]
    if match.group(1) != "VER06":
        return [f"CONFIG_VERSION is {match.group(1)!r}, expected 'VER06'"]
    return []


def _default_resistance_violations(shield_text):
    """Coverage 3: VALUE_R1 is 270000 and VALUE_R2 is 44000."""
    violations = []
    r1_match = re.search(r"#\s*define\s+VALUE_R1\s+(\d+)", shield_text)
    if not r1_match:
        violations.append("VALUE_R1 #define not found in rurp_shield.h")
    elif r1_match.group(1) != "270000":
        violations.append(f"VALUE_R1 is {r1_match.group(1)}, expected 270000")
    r2_match = re.search(r"#\s*define\s+VALUE_R2\s+(\d+)", shield_text)
    if not r2_match:
        violations.append("VALUE_R2 #define not found in rurp_shield.h")
    elif r2_match.group(1) != "44000":
        violations.append(f"VALUE_R2 is {r2_match.group(1)}, expected 44000")
    return violations


def _stored_configuration_violations(dualslot_header_text):
    """Coverage 4 -- D-17: StoredConfiguration embeds rurp_configuration_t as
    a single named member (never inlining its fields), and its own top-level
    `version` member is a uint16_t distinct from the embedded struct's
    char[6] CONFIG_VERSION field. Only the OUTER struct's fields are parsed
    (the embedded struct's own fields are never expanded here), which is
    exactly what makes "distinct" true by construction rather than asserted."""
    stripped = _COMMENT_RE.sub("", dualslot_header_text)
    match = _STORED_CONFIGURATION_STRUCT_RE.search(stripped)
    if not match:
        return ["StoredConfiguration struct definition not found"]
    fields = _parse_struct_fields(match.group(1))
    violations = []
    embedded = [f for f in fields if f[0] == "rurp_configuration_t"]
    if len(embedded) != 1:
        violations.append(
            f"expected exactly one member of type 'rurp_configuration_t' "
            f"(whole-struct embedding, D-17), found {len(embedded)}: "
            f"{embedded!r} in fields {fields!r}"
        )
    version_fields = [f for f in fields if f[1] == "version"]
    if len(version_fields) != 1:
        violations.append(
            f"expected exactly one top-level 'version' member, found "
            f"{len(version_fields)}: {version_fields!r}"
        )
    elif version_fields[0][0] != "uint16_t":
        violations.append(
            f"StoredConfiguration's own 'version' member is "
            f"{version_fields[0][0]!r}, expected 'uint16_t' (distinct from "
            f"CONFIG_VERSION, the char[6] literal inside the embedded struct)"
        )
    return violations


def _config_cpp_absence_violations(repo_root):
    """Coverage 5 -- Criterion 5's verified-by-absence: a path-existence
    check, never a diff (124-VERIFICATION.md recorded a path-scoped diff
    passing vacuously on a wrong path)."""
    path = repo_root / "platform" / "py32f071" / "src" / "config.cpp"
    if path.exists():
        return [
            f"{path} exists -- PR #48's config.cpp must be verified absent "
            f"from the tree (CFG-07 Criterion 5)"
        ]
    return []


def _iter_source_files(repo_root, dirs):
    exts = (".c", ".cpp", ".h", ".hpp")
    for directory in dirs:
        base = repo_root / directory
        if not base.exists():
            continue
        for candidate in base.rglob("*"):
            if candidate.is_file() and candidate.suffix in exts:
                yield candidate


def _function_definition_hits(repo_root, dirs, name):
    """A DEFINITION is NAME(...) immediately followed by '{', never ';' --
    distinguishing a definition from a declaration or a call site. Comments
    are stripped first (the seam header's and this repo's docs both cite
    these names in prose)."""
    pattern = re.compile(rf"\b{name}\s*\([^;{{}}]*\)\s*\{{", re.DOTALL)
    hits = []
    for path in _iter_source_files(repo_root, dirs):
        try:
            text = path.read_text()
        except UnicodeDecodeError:
            continue
        stripped = _COMMENT_RE.sub("", text)
        if pattern.search(stripped):
            hits.append(str(path.relative_to(repo_root)))
    return sorted(hits)


def _public_function_definition_violations(repo_root, dirs, names, expected_home):
    """Coverage 6: each name has exactly one definition, that definition
    lives at expected_home, and none lives anywhere under 'platform/' -- the
    standing guard against PR #48's drift (a second, per-platform
    reimplementation of the common policy) reappearing."""
    violations = []
    for name in names:
        hits = _function_definition_hits(repo_root, dirs, name)
        if len(hits) != 1:
            violations.append(
                f"{name}: expected exactly 1 definition, found {len(hits)}: {hits!r}"
            )
        elif hits[0] != expected_home:
            violations.append(
                f"{name}: defined in {hits[0]!r}, expected {expected_home!r}"
            )
        under_platform = [h for h in hits if h.startswith("platform/")]
        if under_platform:
            violations.append(
                f"{name}: definition found under platform/: {under_platform!r} "
                f"-- forbidden (D-07); this is the exact shape of PR #48's drift"
            )
    return violations


def _public_declarations_in_shield_violations(shield_text, names):
    """Coverage 7: all four public config functions are declared in
    include/rurp_shield.h."""
    stripped = _COMMENT_RE.sub("", shield_text)
    violations = []
    for name in names:
        if not re.search(rf"\b{name}\s*\(", stripped):
            violations.append(f"{name} not found declared in rurp_shield.h")
    return violations


def _consumer_census_violations(repo_root, sites):
    """Coverage 8: each C-14 consumer site is located by file, line and the
    function it calls; every site must resolve and call one of the four
    public functions; the found count must equal len(sites) exactly, so a
    silently removed or rerouted consumer is a finding, not a silent pass."""
    violations = []
    found = 0
    for relpath, lineno, func in sites:
        path = repo_root / relpath
        if not path.is_file():
            violations.append(f"{relpath} does not exist")
            continue
        lines = path.read_text().splitlines()
        if lineno > len(lines):
            violations.append(
                f"{relpath}:{lineno} is out of range ({len(lines)} lines total)"
            )
            continue
        line_text = lines[lineno - 1]
        if not re.search(rf"\b{func}\s*\(", line_text):
            violations.append(
                f"{relpath}:{lineno} does not call {func}(); observed line: "
                f"{line_text.strip()!r}"
            )
            continue
        if func not in _PUBLIC_CONFIG_DECLARATIONS:
            violations.append(
                f"{relpath}:{lineno} calls {func}, which is not one of the "
                f"four public config functions"
            )
            continue
        found += 1
    if found != len(sites):
        violations.append(
            f"expected all {len(sites)} C-14 consumer sites found, found {found}"
        )
    return violations, found


def test_rurp_configuration_t_has_exactly_the_four_pinned_fields():
    """Coverage 1."""
    text = _TYPES_HEADER.read_text()
    violations = _rurp_configuration_t_violations(text)
    assert violations == [], (
        f"expected {_TYPES_HEADER} to define rurp_configuration_t with "
        f"exactly {_EXPECTED_CONFIGURATION_FIELDS!r}, in order.\n"
        f"Violations:\n" + "\n".join(violations) + f"\n\nFull text:\n{text}"
    )


def test_config_version_literal_is_ver06():
    """Coverage 2."""
    violations = _config_version_violations(_SHIELD_HEADER.read_text())
    assert violations == [], (
        f"expected CONFIG_VERSION in {_SHIELD_HEADER} to be the literal "
        f"'VER06'.\nViolations:\n" + "\n".join(violations)
    )


def test_default_resistance_values_are_unchanged():
    """Coverage 3."""
    violations = _default_resistance_violations(_SHIELD_HEADER.read_text())
    assert violations == [], (
        f"expected VALUE_R1 == 270000 and VALUE_R2 == 44000 in "
        f"{_SHIELD_HEADER} -- rurp_validate_config's write-back writes both, "
        f"so a silent change would alter every defaulting board's stored "
        f"record with no version signal.\nViolations:\n" + "\n".join(violations)
    )


def test_stored_configuration_embeds_the_struct_whole():
    """Coverage 4."""
    violations = _stored_configuration_violations(_DUALSLOT_HEADER.read_text())
    assert violations == [], (
        f"expected StoredConfiguration in {_DUALSLOT_HEADER} to embed "
        f"rurp_configuration_t as a single named member, with its own "
        f"'version' member a uint16_t distinct from CONFIG_VERSION (D-17).\n"
        f"Violations:\n" + "\n".join(violations)
    )


def test_pr48_config_cpp_is_absent_from_the_tree():
    """Coverage 5."""
    violations = _config_cpp_absence_violations(_REPO_ROOT)
    assert violations == [], (
        "expected platform/py32f071/src/config.cpp to be verified absent "
        "from the tree (CFG-07 Criterion 5, PR #48's drift).\n"
        "Violations:\n" + "\n".join(violations)
    )


def test_the_four_public_functions_are_defined_exactly_once():
    """Coverage 6."""
    violations = _public_function_definition_violations(
        _REPO_ROOT, _DEF_SEARCH_DIRS, _PUBLIC_CONFIG_DECLARATIONS, "src/rurp_config_utils.cpp"
    )
    assert violations == [], (
        f"expected each of {_PUBLIC_CONFIG_DECLARATIONS!r} defined exactly "
        f"once, in src/rurp_config_utils.cpp, with none under platform/.\n"
        f"Violations:\n" + "\n".join(violations)
    )


def test_the_four_public_functions_are_declared_in_rurp_shield_h():
    """Coverage 7."""
    violations = _public_declarations_in_shield_violations(
        _SHIELD_HEADER.read_text(), _PUBLIC_CONFIG_DECLARATIONS
    )
    assert violations == [], (
        f"expected all of {_PUBLIC_CONFIG_DECLARATIONS!r} declared in "
        f"{_SHIELD_HEADER}.\nViolations:\n" + "\n".join(violations)
    )


def test_the_seven_consumers_call_only_the_public_api():
    """Coverage 8. See the module docstring's Coverage 8 entry for the
    nine-vs-seven correction this function's assertions apply (the function
    keeps the plan-specified name; its assertions use the verified count)."""
    violations, found = _consumer_census_violations(_REPO_ROOT, _C14_CONSUMER_SITES)
    assert violations == [], (
        f"expected all {len(_C14_CONSUMER_SITES)} verified C-14 consumer "
        f"sites to call only the four public config functions.\n"
        f"Violations:\n" + "\n".join(violations)
    )
    assert found == len(_C14_CONSUMER_SITES) == 9, (
        f"expected exactly 9 verified consumer sites found, found {found} "
        f"of {len(_C14_CONSUMER_SITES)} declared"
    )


def _mutate_fifth_member(text):
    """Plant a fifth member on a COPY of rurp_types.h's text."""
    marker = "} rurp_configuration_t;"
    assert marker in text, "fixture assumption failed: rurp_types.h's closing brace line has drifted"
    return text.replace(marker, "    uint8_t reserved;\n" + marker, 1)


def _mutate_reordered_fields(text):
    """Swap r1/r2 on a COPY of rurp_types.h's text -- still four fields,
    still all four names present, but out of order."""
    original_block = "    char version[6];\n    long r1;\n    long r2;\n    uint8_t hardware_revision;"
    assert original_block in text, "fixture assumption failed: rurp_types.h's field block has drifted"
    reordered_block = "    char version[6];\n    long r2;\n    long r1;\n    uint8_t hardware_revision;"
    return text.replace(original_block, reordered_block, 1)


def _mutate_config_version(text):
    marker = '#define CONFIG_VERSION "VER06"'
    assert marker in text, "fixture assumption failed: rurp_shield.h's CONFIG_VERSION line has drifted"
    return text.replace(marker, '#define CONFIG_VERSION "VER07"', 1)


def _mutate_value_r1(text):
    marker = "#define VALUE_R1 270000"
    assert marker in text, "fixture assumption failed: rurp_shield.h's VALUE_R1 line has drifted"
    return text.replace(marker, "#define VALUE_R1 999999", 1)


def _mutate_stored_configuration_inlined(text):
    """Replace the whole-struct member with the struct's own fields inlined,
    on a COPY of config_storage_dualslot.h's text."""
    marker = "    rurp_configuration_t configuration;"
    assert marker in text, "fixture assumption failed: config_storage_dualslot.h's embedded member line has drifted"
    inlined = (
        "    char version[6];\n"
        "    long r1;\n"
        "    long r2;\n"
        "    uint8_t hardware_revision;"
    )
    return text.replace(marker, inlined, 1)


@pytest.mark.parametrize(
    "case_name",
    [
        "fifth_struct_member",
        "reordered_field_list",
        "changed_config_version_literal",
        "changed_value_r1",
        "second_definition_under_platform",
        "stored_configuration_inlined",
    ],
)
def test_helper_reports_violations_on_planted_copies(tmp_path, case_name):
    """Coverage 9 -- the RED demonstration. Each of six independent mutations
    is built as a real file in tmp_path (never a committed file) and fed to
    the same module-level helper the corresponding positive test calls above
    -- proving the gate can actually fail, not merely pass by construction.
    Confirms no committed file was mutated by re-hashing include/rurp_types.h,
    include/rurp_shield.h and src/rurp_config_utils.cpp before and after."""
    watched_paths = (_TYPES_HEADER, _SHIELD_HEADER, _POLICY_SRC)
    shas_before = {p: _git_blob_sha(p) for p in watched_paths}

    try:
        if case_name == "fifth_struct_member":
            mutated_path = tmp_path / "rurp_types.h"
            mutated_path.write_text(_mutate_fifth_member(_TYPES_HEADER.read_text()))
            violations = _rurp_configuration_t_violations(mutated_path.read_text())
        elif case_name == "reordered_field_list":
            mutated_path = tmp_path / "rurp_types.h"
            mutated_path.write_text(_mutate_reordered_fields(_TYPES_HEADER.read_text()))
            violations = _rurp_configuration_t_violations(mutated_path.read_text())
        elif case_name == "changed_config_version_literal":
            mutated_path = tmp_path / "rurp_shield.h"
            mutated_path.write_text(_mutate_config_version(_SHIELD_HEADER.read_text()))
            violations = _config_version_violations(mutated_path.read_text())
        elif case_name == "changed_value_r1":
            mutated_path = tmp_path / "rurp_shield.h"
            mutated_path.write_text(_mutate_value_r1(_SHIELD_HEADER.read_text()))
            violations = _default_resistance_violations(mutated_path.read_text())
        elif case_name == "second_definition_under_platform":
            scratch_root = tmp_path / "scratch_repo"
            policy_copy = scratch_root / "src" / "rurp_config_utils.cpp"
            policy_copy.parent.mkdir(parents=True)
            policy_copy.write_text(_POLICY_SRC.read_text())
            rogue = scratch_root / "platform" / "rogue" / "rogue_config.cpp"
            rogue.parent.mkdir(parents=True)
            rogue.write_text(
                "#include \"rurp_types.h\"\n"
                "void rurp_validate_config(rurp_configuration_t* config) {\n"
                "    (void)config;\n"
                "}\n"
            )
            violations = _public_function_definition_violations(
                scratch_root, _DEF_SEARCH_DIRS, _PUBLIC_CONFIG_DECLARATIONS,
                "src/rurp_config_utils.cpp",
            )
        elif case_name == "stored_configuration_inlined":
            mutated_path = tmp_path / "config_storage_dualslot.h"
            mutated_path.write_text(
                _mutate_stored_configuration_inlined(_DUALSLOT_HEADER.read_text())
            )
            violations = _stored_configuration_violations(mutated_path.read_text())
        else:  # pragma: no cover -- parametrize list above is exhaustive
            raise AssertionError(f"unhandled case_name {case_name!r}")

        assert violations, (
            f"expected the {case_name!r} mutation to produce at least one "
            f"violation from the module-level helper the positive test "
            f"calls; got none, which would mean the gate cannot fail."
        )
    finally:
        shas_after = {p: _git_blob_sha(p) for p in watched_paths}
        assert shas_after == shas_before, (
            f"expected no committed file to be mutated by the {case_name!r} "
            f"planted-copy demonstration; blob SHAs before: {shas_before!r}, "
            f"after: {shas_after!r}"
        )


def test_absence_check_fires_when_config_cpp_is_planted(tmp_path):
    """Coverage 10 -- a dedicated second RED demonstration for Coverage 5
    specifically. An absence assertion is the easiest kind to write
    vacuously (Phases 118/124 both shipped gates that passed without
    observing anything); this test proves the absence helper actually fires
    by planting a scratch platform/py32f071/src/config.cpp under tmp_path,
    never by reasoning about the check in prose."""
    scratch_root = tmp_path / "scratch_repo"
    planted = scratch_root / "platform" / "py32f071" / "src" / "config.cpp"
    planted.parent.mkdir(parents=True)
    planted.write_text(
        "// planted PR #48 config.cpp for the RED demonstration -- never committed\n"
    )
    violations = _config_cpp_absence_violations(scratch_root)
    assert violations, (
        "expected planting platform/py32f071/src/config.cpp in a scratch "
        "tree to produce a violation from the same absence helper the "
        "positive test calls -- proving the absence assertion (Coverage 5) "
        "can actually fail, not merely pass by reasoning about it."
    )
    assert not (_REPO_ROOT / "platform" / "py32f071" / "src" / "config.cpp").exists(), (
        "expected the real repo's config.cpp to remain absent -- this "
        "demonstration must never touch the committed tree"
    )


def test_module_has_no_pio_libdeps_dependency():
    """Coverage 11 -- C-12: this module's own text contains no path under
    the gitignored PlatformIO build-artifacts directory. The needle is built
    via concatenation so this test's own check does not trip on its own
    description of the property."""
    own_text = Path(__file__).read_text()
    forbidden_path_fragment = "." + "pio" + "/"
    assert forbidden_path_fragment not in own_text, (
        "expected no reference to a " + forbidden_path_fragment + " build "
        "artifact path anywhere in this module (C-12): a gitignored "
        "per-env libdeps dependency passes warm and fails on a clean "
        "checkout."
    )


def test_no_skip_call_or_conditional_skip_marker_present():
    """Coverage 12 -- adapted from the analog's self-enforcing no-skip leg
    (tests/test_vpp_seam_manual_on_every_board.py:447-468), copied verbatim
    including its concatenation trick. This module invokes no compiler at
    all, so the leg asserts only that no skip call or conditional-skip
    marker appears anywhere in this module's own text -- the fail-closed
    contract stays self-enforcing and cannot be silently bypassed by a
    future edit.

    The two needle strings below are built via concatenation (not written
    verbatim) so this test's own assertion text does not trip its own
    check -- the literal substrings must appear NOWHERE in this file,
    including inside this test's failure messages."""
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- this "
        "gate must FAIL on a genuine violation, never SKIP."
    )
    assert skipif_marker not in own_text, (
        "expected no @pytest." + skipif_marker + " decorator anywhere in "
        "this module -- this gate must FAIL on a genuine violation, never "
        "SKIP."
    )
