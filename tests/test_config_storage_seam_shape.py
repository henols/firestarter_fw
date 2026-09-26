"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 126 Plan 05 -- CFG-03's structural gate for the configuration-persistence
storage seam (include/rurp_config_storage.h): exactly two bool-returning
functions with C linkage, includes ordered before the extern "C" wrapper,
never reachable from include/rurp_shield.h, included by exactly the
sanctioned translation units, the four public config declarations still above
the seam, CONFIG_VERSION unchanged, CONFIG_START below the seam, and the
common policy layer carrying no platform conditional.

Requirements: CFG-03
Decisions covered: D-01, D-06, D-07, D-08, D-09

Plan 126-03 landed include/rurp_config_storage.h and proved its AVR behaviour
byte-identical via the CFG-04 regression test (Plan 126-02). Nothing yet
stops a later edit from adding a third seam function, moving a declaration,
dropping the extern "C" wrapper, or adding the one #include line D-09 exists
to forbid -- the include that would make `pio test -e native` collapse from
141 cases / 141 succeeded to 17 suites / 0 succeeded (measured, Phase 125
C-1). This module turns each of those into an exit code, locally, before the
gated ARM CI run is the only thing that would catch them.

This module executes in NO CI leg on this branch: `pytest tests/` runs only
in build.yml (push/PR to main) and beta-build.yml (push to beta);
py32f071.yml has no pytest step at all. The local run recorded in this
phase's SUMMARY is the only evidence this module's assertions were ever
exercised.

This module invokes no compiler -- every assertion below is a textual gate
over committed source, following tests/test_vpp_seam_manual_on_every_board.py
and tests/test_config_storage_eeprom_regression.py's plain read_text() idiom,
deliberately not parsed into a structure.

Self-contained path resolution below -- NOT in conftest.py (firestarter/tests/
has no conftest.py anywhere in the repo, and none of pytest.ini,
pyproject.toml, setup.cfg or tox.ini exist either; a recorded house-rule
pattern decision, not an omission). Stdlib and pytest only.

Every check below is factored into a module-level helper taking a path's text
(or, for the two census checks, a filesystem tree) and returning a list of
violations, so the positive tests and the planted-copy RED demonstration
(Coverage 9) exercise the identical code path -- a parallel second
implementation inside the RED test would prove nothing about the gate
(Phase 124 D-14: "a guard that supplies the answer it tests is structurally
dead"). No blob SHA literal appears anywhere in this module: a committed gate
pinning a shared header by hash would break the first time a later milestone
legitimately edits it (the D-09 blob pin for include/rurp_shield.h is a
plan-level record instead, re-executed by 126-NONREGRESSION.md). The one
blob-SHA comparison this module does make (Coverage 9) is entirely
self-referential -- before vs. after this module's own mutation demo, never
against a hardcoded historical value.

Coverage:
  1. test_seam_header_declares_exactly_two_functions -- D-06: the declaration
     census over include/rurp_config_storage.h finds exactly the two D-06
     bool prototypes and nothing else (no enum, no typedef, no non-guard
     object-like macro).
  2. test_seam_header_has_c_linkage_with_includes_outside_the_wrapper -- C-11
     / D-06: the extern "C" wrapper exists, both declarations lie inside it,
     and <stdbool.h>/<stddef.h> are included strictly before it opens -- the
     only local detector for a mismatch that would otherwise surface solely
     as an ARM link failure (arm-none-eabi-gcc is absent here).
  3. test_seam_header_is_not_included_by_rurp_shield_h -- D-09: a
     repo-bounded transitive reachability walk from include/rurp_shield.h
     never reaches the seam header, and the walk visits at least one header
     (so it cannot pass by visiting nothing).
  4. test_seam_header_includers_are_exactly_the_sanctioned_set -- D-09: the
     includer census over src/, include/, platform/, test/ and lib/ asserts
     every hit is sanctioned, every sanctioned path that exists in the tree
     is a hit, and the hit count is at least 2 -- stable both before and
     after Plan 126-08 lands the third includer.
  5. test_public_config_declarations_stay_in_rurp_shield_h -- D-07: all four
     of rurp_get_config, rurp_load_config, rurp_save_config and
     rurp_validate_config stay declared in include/rurp_shield.h and are
     absent from the seam header.
  6. test_config_version_literal_is_unchanged -- D-07: CONFIG_VERSION in
     include/rurp_shield.h is still the literal "VER06".
  7. test_config_start_lives_below_the_seam -- D-07: CONFIG_START is defined
     as 48 in the AVR backend translation unit and absent from the common
     policy layer.
  8. test_policy_layer_has_no_platform_conditional -- D-08: the common
     policy layer (src/rurp_config_utils.cpp) contains no preprocessor
     conditional, so "per-platform backend" is structurally true rather than
     true only because the file says so.
  9. test_helper_reports_violations_on_planted_copies -- the RED
     demonstration: four independently planted mutations (a third
     declaration; includes moved inside the extern "C" block; a
     rurp_shield.h copy carrying the forbidden include; an unsanctioned
     includer), each built in tmp_path and fed to the same module-level
     helper the corresponding positive test calls, each producing a
     non-empty violation list. Confirms no committed file was touched by
     re-hashing include/rurp_config_storage.h, include/rurp_shield.h and
     src/rurp_config_utils.cpp before and after.
 10. test_module_has_no_pio_libdeps_dependency -- C-12: this module's own
     text contains no path under the gitignored PlatformIO build-artifacts
     directory.
 11. test_compiler_is_required_not_optional -- adapted from the analog's
     self-enforcing no-skip leg (test_vpp_seam_manual_on_every_board.py:
     447-468, copied verbatim including its concatenation trick): this
     module invokes no compiler, so the leg asserts only that no skip call
     or conditional-skip marker appears anywhere in this module's own text.
"""

import hashlib
import re
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_INCLUDE = _REPO_ROOT / "include"
_SRC = _REPO_ROOT / "src"

_SEAM_HEADER = _INCLUDE / "rurp_config_storage.h"
_SEAM_HEADER_NAME = "rurp_config_storage.h"
_SHIELD_HEADER = _INCLUDE / "rurp_shield.h"
_POLICY_SRC = _SRC / "rurp_config_utils.cpp"
_AVR_BACKEND = _SRC / "boards" / "rurp_config_storage_eeprom.cpp"

# Walked for the includer census (Coverage 4). "lib" and "test" exist in this
# tree today with no seam-header includers; walked anyway so the census
# stays correct if either ever gains one without a plan author remembering to
# widen this tuple.
_SEARCH_DIRS = ("src", "include", "platform", "test", "lib")

_SANCTIONED_INCLUDERS = (
    "src/rurp_config_utils.cpp",
    "src/boards/rurp_config_storage_eeprom.cpp",
    "platform/py32f071/src/config_storage_flash.cpp",
)

_PUBLIC_CONFIG_DECLARATIONS = (
    "rurp_get_config",
    "rurp_load_config",
    "rurp_save_config",
    "rurp_validate_config",
)

_COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
_PROTO_RE = re.compile(
    r'(?m)^[ \t]*([A-Za-z_][A-Za-z0-9_ \t]*?)\s+([A-Za-z_]\w*)\s*\(([^;{}()]*)\)\s*;[ \t]*$'
)
_GUARD_RE = re.compile(r'#\s*ifndef\s+(\w+)')
_MACRO_RE = re.compile(r'(?m)^[ \t]*#\s*define\s+(\w+)')
_INCLUDE_RE = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')
_CONDITIONAL_RE = re.compile(r'(?m)^[ \t]*#\s*(if|ifdef|ifndef|elif)\b')

_EXPECTED_DECLARATIONS = {
    "rurp_config_storage_load": ("bool", re.compile(r'^void\s*\*\s*\w+\s*,\s*size_t\s+\w+$')),
    "rurp_config_storage_save": ("bool", re.compile(r'^const\s+void\s*\*\s*\w+\s*,\s*size_t\s+\w+$')),
}


def _git_blob_sha(path):
    """Reproduce `git hash-object`'s SHA1 (blob <size>\\0<content>) without
    shelling out to git -- used only to prove Coverage 9's planted mutations
    never touch a committed file; never compared against a hardcoded
    historical value (no blob SHA literal appears in this module)."""
    data = path.read_bytes()
    header = f"blob {len(data)}\0".encode()
    return hashlib.sha1(header + data).hexdigest()


def _declaration_census_violations(header_text):
    """D-06: exactly two bool prototypes, named and shaped as the storage
    seam requires, with no enum, no typedef and no object-like macro beyond
    the include guard. Returns a list of violation strings (empty = clean)."""
    violations = []
    stripped = _COMMENT_RE.sub("", header_text)

    guard_match = _GUARD_RE.search(stripped)
    guard_name = guard_match.group(1) if guard_match else None

    protos = _PROTO_RE.findall(stripped)
    names = [p[1] for p in protos]

    if len(protos) != 2:
        violations.append(
            f"expected exactly 2 function prototypes, found {len(protos)}: {names!r}"
        )

    for ret, name, params in protos:
        ret = ret.strip()
        if name not in _EXPECTED_DECLARATIONS:
            violations.append(
                f"unexpected/extra function prototype declared: {name!r} "
                f"(return {ret!r}, params {params!r})"
            )
            continue
        expected_ret, expected_params_re = _EXPECTED_DECLARATIONS[name]
        if ret != expected_ret:
            violations.append(f"{name} returns {ret!r}, expected {expected_ret!r}")
        normalized_params = re.sub(r"\s+", " ", params.strip())
        if not expected_params_re.match(normalized_params):
            violations.append(
                f"{name}'s parameter list is {params!r} (normalized "
                f"{normalized_params!r}), which does not match the D-06 shape"
            )

    for missing in set(_EXPECTED_DECLARATIONS) - set(names):
        violations.append(f"missing expected declaration: {missing}")

    if re.search(r"\benum\b", stripped):
        violations.append("found an 'enum' in the seam header -- D-06 rejects a richer status enum")
    if re.search(r"\btypedef\b", stripped):
        violations.append("found a 'typedef' in the seam header -- D-06 permits none")

    macros = _MACRO_RE.findall(stripped)
    extra_macros = [m for m in macros if m != guard_name]
    if extra_macros:
        violations.append(
            f"unexpected object-like macro(s) beyond the include guard {guard_name!r}: {extra_macros!r}"
        )

    return violations


def _linkage_violations(header_text):
    """C-11 / D-06: the extern "C" wrapper (open + close) exists, both
    declarations lie strictly between them, and <stdbool.h>/<stddef.h> are
    included at a line index strictly before the wrapper opens. Returns a
    list of violation strings (empty = clean)."""
    violations = []
    lines = header_text.splitlines()

    ifdef_cpp_indices = [
        i for i, line in enumerate(lines) if re.search(r"#\s*ifdef\s+__cplusplus", line)
    ]
    if len(ifdef_cpp_indices) < 2:
        violations.append(
            f"expected two '#ifdef __cplusplus' guards (open + close) wrapping "
            f"extern \"C\", found {len(ifdef_cpp_indices)}"
        )
        return violations

    open_idx, close_idx = ifdef_cpp_indices[0], ifdef_cpp_indices[-1]

    extern_open_idx = None
    for i in range(open_idx, min(open_idx + 4, len(lines))):
        if re.search(r'extern\s*"C"\s*\{', lines[i]):
            extern_open_idx = i
            break
    if extern_open_idx is None:
        violations.append(
            f'expected \'extern "C" {{\' shortly after the first #ifdef '
            f"__cplusplus guard (line {open_idx + 1})"
        )

    extern_close_idx = None
    for i in range(close_idx, min(close_idx + 4, len(lines))):
        if re.search(r"^\s*\}\s*$", lines[i]):
            extern_close_idx = i
            break
    if extern_close_idx is None:
        violations.append(
            f"expected a closing '}}' shortly after the second #ifdef "
            f"__cplusplus guard (line {close_idx + 1})"
        )

    decl_indices = [
        i
        for i, line in enumerate(lines)
        if re.search(r"rurp_config_storage_(load|save)\s*\(", line) and line.strip().endswith(";")
    ]
    if not decl_indices:
        violations.append("found no rurp_config_storage_load/save declaration lines at all")
    elif extern_open_idx is not None and extern_close_idx is not None:
        outside = [
            i for i in decl_indices if not (extern_open_idx < i < extern_close_idx)
        ]
        if outside:
            violations.append(
                f"declaration(s) at line(s) {[i + 1 for i in outside]} lie outside "
                f"the extern \"C\" {{ ... }} block "
                f"(open at line {extern_open_idx + 1}, close at line {extern_close_idx + 1})"
            )

    stdbool_idx = next((i for i, line in enumerate(lines) if "#include <stdbool.h>" in line), None)
    stddef_idx = next((i for i, line in enumerate(lines) if "#include <stddef.h>" in line), None)
    if stdbool_idx is None:
        violations.append("expected '#include <stdbool.h>' somewhere in the header, found none")
    elif stdbool_idx >= open_idx:
        violations.append(
            f"'#include <stdbool.h>' at line {stdbool_idx + 1} does not precede the "
            f"extern \"C\" wrapper opening at line {open_idx + 1}"
        )
    if stddef_idx is None:
        violations.append("expected '#include <stddef.h>' somewhere in the header, found none")
    elif stddef_idx >= open_idx:
        violations.append(
            f"'#include <stddef.h>' at line {stddef_idx + 1} does not precede the "
            f"extern \"C\" wrapper opening at line {open_idx + 1}"
        )

    return violations


def _resolve_include(name, including_dir, include_dirs):
    """Resolve an #include target against the including file's own
    directory first, then each of include_dirs, in order. Returns None
    (a leaf, typically a system header) if nothing resolves."""
    candidates = [including_dir / name] + [d / name for d in include_dirs]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def _seam_reachability_violations(entry_header, include_dirs, seam_header_name):
    """D-09: a repo-bounded BFS from entry_header, following only
    #include directives this environment can resolve to a real file (system
    headers like <stdint.h> are leaves, never traversed). Returns
    (violations, headers_visited) -- the caller asserts headers_visited > 0
    so a walk that visited nothing cannot pass silently."""
    violations = []
    visited = set()
    stack = [entry_header.resolve()]
    headers_visited = 0

    while stack:
        current = stack.pop()
        if current in visited:
            continue
        visited.add(current)
        headers_visited += 1
        text = current.read_text()
        for included_name in _INCLUDE_RE.findall(text):
            if included_name == seam_header_name or Path(included_name).name == seam_header_name:
                violations.append(
                    f"{current} includes {included_name!r}, which resolves to the seam "
                    f"header {seam_header_name!r} -- forbidden by D-09: the seam header "
                    f"is reachable from 46 translation units (14 of them native "
                    f"host_stubs.cpp files) if it is ever included from "
                    f"include/rurp_shield.h, and Phase 125 C-1 measured that ONE such "
                    f"line collapses `pio test -e native` from 141 cases / 141 "
                    f"succeeded to 17 suites / 0 succeeded"
                )
                continue
            resolved = _resolve_include(included_name, current.parent, include_dirs)
            if resolved is not None and resolved not in visited:
                stack.append(resolved)

    return violations, headers_visited


def _includer_census(repo_root, search_dirs, seam_header_name):
    """Walk search_dirs under repo_root for any file containing an include
    of seam_header_name. Returns a sorted list of repo-relative path
    strings."""
    hits = []
    for directory in search_dirs:
        base = repo_root / directory
        if not base.exists():
            continue
        for candidate in base.rglob("*"):
            if candidate.is_file() and candidate.suffix in (".c", ".cpp", ".h", ".hpp"):
                try:
                    text = candidate.read_text()
                except UnicodeDecodeError:
                    continue
                if re.search(rf'#\s*include\s*["<]{re.escape(seam_header_name)}[">]', text):
                    hits.append(candidate.resolve())
    return sorted(str(p.relative_to(repo_root)) for p in hits)


def _includer_census_violations(repo_root, search_dirs, sanctioned, seam_header_name):
    """D-09's three-property census, stable both before and after Plan
    126-08: (a) every hit is sanctioned, (b) every sanctioned path that
    EXISTS in the tree is a hit, (c) the hit count is at least 2. Returns
    (violations, hits)."""
    hits = _includer_census(repo_root, search_dirs, seam_header_name)
    violations = []

    unsanctioned = [h for h in hits if h not in sanctioned]
    if unsanctioned:
        violations.append(
            f"unsanctioned includer(s) of {seam_header_name!r}: {unsanctioned!r}"
        )

    missing = [s for s in sanctioned if (repo_root / s).exists() and s not in hits]
    if missing:
        violations.append(
            f"sanctioned path(s) exist in the tree but do not include "
            f"{seam_header_name!r}: {missing!r}"
        )

    if len(hits) < 2:
        violations.append(
            f"includer census is vacuous or below the non-vacuity floor: only "
            f"{len(hits)} hit(s): {hits!r}"
        )

    return violations, hits


def _public_declarations_violations(shield_text, seam_text):
    """D-07: the four public config declarations stay in rurp_shield.h and
    are absent from the seam header. Both texts are comment-stripped first:
    the seam header's own FIRE-PROOF comment names rurp_validate_config() in
    prose, which must never be mistaken for a declaration."""
    violations = []
    stripped_shield = _COMMENT_RE.sub("", shield_text)
    stripped_seam = _COMMENT_RE.sub("", seam_text)
    for name in _PUBLIC_CONFIG_DECLARATIONS:
        if not re.search(rf"\b{name}\s*\(", stripped_shield):
            violations.append(f"{name} not found declared in rurp_shield.h")
        if re.search(rf"\b{name}\s*\(", stripped_seam):
            violations.append(
                f"{name} unexpectedly declared in the seam header -- D-07 keeps it above the seam"
            )
    return violations


def _config_version_violations(shield_text):
    """D-07: CONFIG_VERSION is still the literal 'VER06'."""
    match = re.search(r'#\s*define\s+CONFIG_VERSION\s+"([^"]*)"', shield_text)
    if not match:
        return ["CONFIG_VERSION #define not found in rurp_shield.h"]
    if match.group(1) != "VER06":
        return [f"CONFIG_VERSION is {match.group(1)!r}, expected 'VER06'"]
    return []


def _config_start_violations(avr_backend_text, policy_text):
    """D-07: CONFIG_START is 48 in the AVR backend translation unit and
    absent from the common policy layer (it is an EEPROM address, meaningless
    on py32)."""
    violations = []
    match = re.search(r"#\s*define\s+CONFIG_START\s+(\d+)", avr_backend_text)
    if not match:
        violations.append("CONFIG_START #define not found in the AVR backend translation unit")
    elif match.group(1) != "48":
        violations.append(
            f"CONFIG_START is {match.group(1)}, expected 48, in the AVR backend translation unit"
        )
    if re.search(r"#\s*define\s+CONFIG_START\b", policy_text):
        violations.append(
            "CONFIG_START #define found in the policy layer (src/rurp_config_utils.cpp) "
            "-- it is an EEPROM address and must live below the seam (D-07)"
        )
    return violations


def _platform_conditional_violations(policy_text):
    """D-08: the common policy layer contains no preprocessor conditional at
    all, so "per-platform backend" is structural, not declared."""
    matches = _CONDITIONAL_RE.findall(policy_text)
    if matches:
        return [
            f"src/rurp_config_utils.cpp contains {len(matches)} preprocessor "
            f"conditional directive(s) ({matches!r}) -- the per-platform split "
            f"must be structural, not declared (D-08)"
        ]
    return []


def test_seam_header_declares_exactly_two_functions():
    """Coverage 1 -- D-06: the declaration census over the real,
    committed seam header finds exactly the two D-06 bool prototypes and
    nothing else."""
    text = _SEAM_HEADER.read_text()
    violations = _declaration_census_violations(text)
    assert violations == [], (
        f"expected {_SEAM_HEADER} to declare exactly rurp_config_storage_load "
        f"and rurp_config_storage_save, both bool, and nothing else.\n"
        f"Violations:\n" + "\n".join(violations) + f"\n\nFull text:\n{text}"
    )


def test_seam_header_has_c_linkage_with_includes_outside_the_wrapper():
    """Coverage 2 -- C-11 / D-06: the extern "C" wrapper exists, both
    declarations lie inside it, and the two includes precede it."""
    text = _SEAM_HEADER.read_text()
    violations = _linkage_violations(text)
    assert violations == [], (
        f"expected {_SEAM_HEADER} to wrap both declarations in extern \"C\" "
        f"with <stdbool.h>/<stddef.h> included before the wrapper opens -- a "
        f"mismatch here would surface only as an ARM link failure, which "
        f"this environment cannot build (arm-none-eabi-gcc is absent).\n"
        f"Violations:\n" + "\n".join(violations) + f"\n\nFull text:\n{text}"
    )


def test_seam_header_is_not_included_by_rurp_shield_h():
    """Coverage 3 -- D-09: a repo-bounded transitive walk from
    include/rurp_shield.h never reaches the seam header."""
    violations, headers_visited = _seam_reachability_violations(
        _SHIELD_HEADER, (_INCLUDE,), _SEAM_HEADER_NAME
    )
    assert headers_visited > 0, (
        f"the reachability walk from {_SHIELD_HEADER} visited "
        f"{headers_visited} headers -- a walk that visits nothing cannot "
        f"prove absence of the forbidden include."
    )
    assert violations == [], (
        f"expected {_SHIELD_HEADER} to never reach the seam header, directly "
        f"or transitively (D-09). Headers visited: {headers_visited}.\n"
        f"Violations:\n" + "\n".join(violations)
    )


def test_seam_header_includers_are_exactly_the_sanctioned_set():
    """Coverage 4 -- D-09: the includer census enforces all three
    properties (every hit sanctioned, every existing sanctioned path a hit,
    count >= 2), stable both before and after Plan 126-08."""
    violations, hits = _includer_census_violations(
        _REPO_ROOT, _SEARCH_DIRS, _SANCTIONED_INCLUDERS, _SEAM_HEADER_NAME
    )
    assert violations == [], (
        f"includer census failed.\nObserved hits ({len(hits)}): {hits!r}\n"
        f"Sanctioned set: {_SANCTIONED_INCLUDERS!r}\n"
        f"Violations:\n" + "\n".join(violations)
    )


def test_public_config_declarations_stay_in_rurp_shield_h():
    """Coverage 5 -- D-07: the four public config declarations stay in
    rurp_shield.h and are absent from the seam header."""
    shield_text = _SHIELD_HEADER.read_text()
    seam_text = _SEAM_HEADER.read_text()
    violations = _public_declarations_violations(shield_text, seam_text)
    assert violations == [], (
        f"expected all of {_PUBLIC_CONFIG_DECLARATIONS!r} declared in "
        f"{_SHIELD_HEADER} and absent from {_SEAM_HEADER}.\n"
        f"Violations:\n" + "\n".join(violations)
    )


def test_config_version_literal_is_unchanged():
    """Coverage 6 -- D-07: CONFIG_VERSION in rurp_shield.h is still 'VER06'."""
    violations = _config_version_violations(_SHIELD_HEADER.read_text())
    assert violations == [], (
        f"expected CONFIG_VERSION in {_SHIELD_HEADER} to be the literal "
        f"'VER06'.\nViolations:\n" + "\n".join(violations)
    )


def test_config_start_lives_below_the_seam():
    """Coverage 7 -- D-07: CONFIG_START is 48 in the AVR backend and absent
    from the common policy layer."""
    violations = _config_start_violations(_AVR_BACKEND.read_text(), _POLICY_SRC.read_text())
    assert violations == [], (
        f"expected CONFIG_START defined as 48 in {_AVR_BACKEND} and absent "
        f"from {_POLICY_SRC}.\nViolations:\n" + "\n".join(violations)
    )


def test_policy_layer_has_no_platform_conditional():
    """Coverage 8 -- D-08: the common policy layer contains no preprocessor
    conditional, so the per-platform split is structural rather than
    declared."""
    violations = _platform_conditional_violations(_POLICY_SRC.read_text())
    assert violations == [], (
        f"expected {_POLICY_SRC} to contain no preprocessor conditional at "
        f"all.\nViolations:\n" + "\n".join(violations)
    )


def _mutate_third_declaration(header_text):
    """Plant a third declaration inside the extern "C" block of a COPY of
    the seam header text. Never touches the committed file."""
    marker = 'extern "C" {\n'
    idx = header_text.index(marker) + len(marker)
    insertion = "bool rurp_config_storage_extra(void);\n\n"
    return header_text[:idx] + insertion + header_text[idx:]


def _mutate_includes_inside_wrapper(header_text):
    """Move the <stdbool.h>/<stddef.h> includes from before the extern "C"
    wrapper to just after it opens, on a COPY of the seam header text."""
    include_block = "#include <stdbool.h>\n#include <stddef.h>\n"
    assert include_block in header_text, (
        "fixture assumption failed: the seam header's include block text "
        "has drifted from what this mutation expects to find and remove"
    )
    without_includes = header_text.replace(include_block, "", 1)
    marker = 'extern "C" {\n'
    idx = without_includes.index(marker) + len(marker)
    return without_includes[:idx] + include_block + without_includes[idx:]


def _mutate_forbidden_include(shield_text):
    """Add an include of the seam header to a COPY of rurp_shield.h's text,
    right after an existing repo-relative include, so the mutated copy still
    parses as a plausible header for the walk."""
    marker = '#include "rurp_pinout.h"\n'
    assert marker in shield_text, (
        "fixture assumption failed: rurp_shield.h's include list has "
        "drifted from what this mutation expects to find"
    )
    return shield_text.replace(marker, marker + '#include "rurp_config_storage.h"\n', 1)


@pytest.mark.parametrize(
    "case_name",
    [
        "third_declaration",
        "includes_inside_wrapper",
        "forbidden_include",
        "unsanctioned_includer",
    ],
)
def test_helper_reports_violations_on_planted_copies(tmp_path, case_name):
    """Coverage 9 -- the RED demonstration. Each of four independent
    mutations is built as a copy in tmp_path (never a committed file) and
    fed to the same module-level helper the corresponding positive test
    calls above -- proving the gate can actually fail, not merely pass by
    construction. Confirms no committed file was mutated by re-hashing
    include/rurp_config_storage.h, include/rurp_shield.h and
    src/rurp_config_utils.cpp before and after."""
    watched_paths = (_SEAM_HEADER, _SHIELD_HEADER, _POLICY_SRC)
    shas_before = {p: _git_blob_sha(p) for p in watched_paths}

    try:
        if case_name == "third_declaration":
            mutated_text = _mutate_third_declaration(_SEAM_HEADER.read_text())
            violations = _declaration_census_violations(mutated_text)
        elif case_name == "includes_inside_wrapper":
            mutated_text = _mutate_includes_inside_wrapper(_SEAM_HEADER.read_text())
            violations = _linkage_violations(mutated_text)
        elif case_name == "forbidden_include":
            mutated_include_dir = tmp_path / "include"
            mutated_include_dir.mkdir()
            mutated_path = mutated_include_dir / "rurp_shield_copy.h"
            mutated_path.write_text(_mutate_forbidden_include(_SHIELD_HEADER.read_text()))
            violations, _headers_visited = _seam_reachability_violations(
                mutated_path, (mutated_include_dir,), _SEAM_HEADER_NAME
            )
        elif case_name == "unsanctioned_includer":
            scratch_root = tmp_path / "scratch_repo"
            unsanctioned = scratch_root / "src" / "unsanctioned_includer.cpp"
            unsanctioned.parent.mkdir(parents=True)
            unsanctioned.write_text('#include "rurp_config_storage.h"\n')
            violations, _hits = _includer_census_violations(
                scratch_root, _SEARCH_DIRS, _SANCTIONED_INCLUDERS, _SEAM_HEADER_NAME
            )
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


def test_module_has_no_pio_libdeps_dependency():
    """Coverage 10 -- C-12: this module's own text contains no path under
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


def test_compiler_is_required_not_optional():
    """Coverage 11 -- adapted from the analog's self-enforcing no-skip leg
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
