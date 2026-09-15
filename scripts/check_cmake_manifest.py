#!/usr/bin/env python3
"""scripts/check_cmake_manifest.py — BASE-04 CMake source-list drift gate (Phase 123 Plan 04, D-06/D-07).

`platform/py32f071/CMakeLists.txt` (landed by Phase 124's MERGE-01/MERGE-02) names
three `set()` source lists with three incompatible path idioms (123-RESEARCH.md
"BASE-04: The Real CMake Manifest"):

  - FIRESTARTER_COMMON_SOURCES -- `"${REPOSITORY_ROOT}/src/..."` (16 entries),
    the shared firmware sources the ARM port re-compiles. This is the list
    BASE-04 exists to protect: today it names
    `"${REPOSITORY_ROOT}/src/proms/flash_type_3.cpp"` and
    `"${REPOSITORY_ROOT}/src/proms/flash_type_4.cpp"` -- both renamed by
    v1.19 Phase 104 to flash_nor_unlock.cpp / flash_5v_page.cpp. That is the
    confirmed MERGE-02 defect this gate exists to catch, and it must exist
    BEFORE the port lands so it CATCHES the rename damage rather than
    blessing it after the fact.
  - PY32_PLATFORM_SOURCES -- bare paths relative to platform/py32f071/ (7
    entries): the ARM-only sources (startup, main, HAL glue).
  - PY32_SDK_SOURCES -- `"${PY32_SDK_ROOT}/..."` (14 entries), where
    PY32_SDK_ROOT is a FetchContent download directory
    (py32f071_sdk_SOURCE_DIR) that exists only after a networked `cmake`
    configure. This list is STRUCTURALLY EXEMPT from resolution -- a
    property of FetchContent, NOT a PY32_EXCLUDED allow-list entry. A gate
    that resolved these paths would report 14 permanent violations with no
    defect present (123-RESEARCH.md Pitfall 6).

`target_include_directories` (which mixes all three path idioms) is
deliberately NOT parsed here -- a missing include directory is a
warning-class problem, not the rename-damage class BASE-04 targets. Do not
"complete" this gate by adding it.

Coarse-key arming (D-07). `platform/py32f071/` does not exist on this branch
yet -- it arrives with Phase 124's merge. `ARMED` is computed from whether
`<root>/platform/py32f071` is a DIRECTORY (never a manual flip constant): if
absent, this gate prints `UNARMED:` and exits 0 -- the correct state until
Phase 124 lands the port. Once armed, the manifest at
`platform/py32f071/CMakeLists.txt` MUST exist and MUST parse into at least
one enforced source; a missing or unparseable manifest under an armed key is
a hard failure (exit 2), never a silent UNARMED and never a skip -- a rename
INSIDE the port cannot disarm this gate, only deleting the whole
`platform/py32f071/` directory can, and that is not a silent event.

PY32_EXCLUDED allow-list (a new contract: this phase authors its format,
Phase 124 populates it). A commented line of the form

    # PY32_EXCLUDED: <path> -- <reason>

in the manifest allow-lists a firmware source under `src/` that is
deliberately NOT named in FIRESTARTER_COMMON_SOURCES. The reason segment is
MANDATORY -- an entry with a path but no stated reason is itself a
violation, because an allow-list without required reasons degrades into a
silencer. 123-RESEARCH.md's measured deliberate-omission set against `beta`
(Phase 124 wrote the first five lines verbatim; Plan 126-03 added a sixth,
for the AVR config-storage backend TU split out of src/rurp_config_utils.cpp;
Plan 126-08 retired that sixth line again once src/rurp_config_utils.cpp was
promoted into FIRESTARTER_COMMON_SOURCES in the same commit that deleted
platform/py32f071/src/config.cpp -- see the note below):

    src/boards/uno_rurp_shield.cpp        -- AVR board impl, no ARM analogue
    src/boards/leonardo_rurp_shield.cpp   -- AVR board impl, no ARM analogue
    src/boards/rurp_common.cpp            -- AVR-specific common
    src/dev_tools.cpp                     -- DEV_TOOLS deliberately off on ARM (MERGE-08)
    src/boards/rurp_config_storage_eeprom.cpp -- AVR EEPROM backend, no ARM analogue

    NOTE: src/rurp_config_utils.cpp is no longer in this allow-list. Plan
    126-08 promoted it into FIRESTARTER_COMMON_SOURCES -- it is now an
    ARM-compiled common source, in the SAME commit that deleted
    platform/py32f071/src/config.cpp. The two were atomic in the same
    direction Plan 126-03's deferral note used to warn about: promoting the
    policy TU before the deletion would have given the ARM link two
    definitions of each of the four public config functions -- undetectable
    in this devcontainer (arm-none-eabi-gcc/cmake/ninja are absent, and
    py32f071.yml does not fire on a push to this branch), surfacing only in
    a later gated CI run. That window was never opened.

Exit codes:
  0 -- UNARMED (platform/py32f071/ absent), OR armed and every enforced
       source resolves, with every reverse-check tree omission covered by a
       reasoned PY32_EXCLUDED entry (gate passes)
  1 -- armed, and at least one enforced source is unresolvable, OR at least
       one PY32_EXCLUDED entry has no stated reason, OR at least one tree
       source under src/ is unnamed in FIRESTARTER_COMMON_SOURCES and
       uncovered by any reasoned PY32_EXCLUDED entry, OR the gate resolved
       zero enforced sources while armed (never-vacuous guard: an empty or
       unparsed source list must not look like a clean pass)
  2 -- armed but the manifest is missing or does not parse into at least one
       enforced source, OR the manifest contains a `set()` source list whose
       name is in neither ENFORCED_LISTS nor EXEMPT_LISTS (a configuration
       change this gate has not been taught about) -- a tool/config error,
       never silently reported as a pass

Anti-hollow contract: this checker's mandatory paired pytest is
`tests/test_check_cmake_manifest.py`, exercising the four fixture trees
under `tests/fixtures/` (`planted_cmake_manifest_missing_source/`,
`planted_cmake_manifest_excluded_no_reason/`, `clean_cmake_manifest_excluded/`,
`clean_unarmed_tree/`) via the `FIRESTARTER_MANIFEST_ROOT` env seam and a
real subprocess -- never an in-process import -- so a passing suite proves
this script itself fails the build on real rename damage, not merely that
the test asserts it should.

Non-claim: a green run proves every path the manifest NAMES resolves in the
tree, and every tree source not named is either structurally exempt or
carries a reasoned PY32_EXCLUDED entry. It does NOT prove the manifest names
everything the port needs, and it never configures or builds anything --
`cmake`, `ninja` and `arm-none-eabi-gcc` are all absent from this
devcontainer, so this gate is deliberately a TEXTUAL gate over the
manifest's own `set()` blocks.

Usage:
    python3 scripts/check_cmake_manifest.py
    FIRESTARTER_MANIFEST_ROOT=/path/to/tree python3 scripts/check_cmake_manifest.py
"""
import os
import re
import sys
from pathlib import Path

# Resolve the repo root from this file's location so the gate behaves
# identically regardless of the caller's working directory (mirrors
# check_size_baseline.py:63 / check_build_warnings.py:77).
# Layout: <repo>/scripts/check_cmake_manifest.py -> repo root is one parent up.
REPO_ROOT = Path(__file__).resolve().parent.parent

FIRESTARTER_MANIFEST_ROOT = os.environ.get("FIRESTARTER_MANIFEST_ROOT", str(REPO_ROOT))

_ROOT = Path(FIRESTARTER_MANIFEST_ROOT)
_PLATFORM_DIR = _ROOT / "platform" / "py32f071"
_MANIFEST_PATH = _PLATFORM_DIR / "CMakeLists.txt"

ARMED = _PLATFORM_DIR.is_dir()

# Enforced: the two lists whose named paths must all resolve in this tree.
ENFORCED_LISTS = {"FIRESTARTER_COMMON_SOURCES", "PY32_PLATFORM_SOURCES"}

EXEMPT_LISTS = {"PY32_SDK_SOURCES"}

# Extracts `set(<NAME> <body>)` blocks. The real manifest's source-list
# set() bodies have no nested parentheses (${VAR} uses braces, not parens),
# so a non-nesting [^)]* is sufficient; this deliberately does not attempt
# full CMake parsing.
SET_BLOCK_RE = re.compile(r"set\(\s*(?P<name>\w+)\s+(?P<body>[^)]*)\)", re.DOTALL)

# Extracts a source-file path from a set() body: optional surrounding
# quotes, the ${VAR}/word/path characters, then one of the four source
# extensions this project uses, with a trailing (?!\w) boundary so a
# non-source extension that merely starts with the same letter (e.g. the
# ".cmake" in CMAKE_TOOLCHAIN_FILE, or the ".c" prefix inside a longer
# non-source token) can never falsely match. This is how a set() block is
# recognised as a SOURCE list at all: if PATH_RE finds no match in a
# block's body, the block is not a source list (e.g. TARGET_NAME,
# REPOSITORY_ROOT, PY32_SDK_ROOT, LINKER_SCRIPT, CMAKE_TOOLCHAIN_FILE) and
# is silently skipped -- never flagged as an unknown source list.
PATH_RE = re.compile(r'"?(?P<path>[$\w{}/.\-]+\.(?:cpp|c|s|S))(?!\w)"?')

# Matches ANY comment line that starts the PY32_EXCLUDED: keyword, valid or
# not -- used to detect a malformed (present but unreasoned) entry as a
# violation distinct from "genuinely absent from the manifest".
EXCLUDED_LINE_RE = re.compile(r"^\s*#\s*PY32_EXCLUDED:\s*(?P<rest>.*)$", re.MULTILINE)

# A well-formed PY32_EXCLUDED allow-list line body: a path, a "--"
# separator and a MANDATORY non-empty reason. `(?P<reason>.+)` requires at
# least one character -- an entry with a path but no `--` reason (or an
# empty reason after `--`) does not match this pattern as valid, and is
# routed to excluded_invalid by parse_manifest() below instead.
EXCLUDED_RE = re.compile(r"^(?P<path>\S+)\s*--\s*(?P<reason>.+?)\s*$")

_SOURCE_EXTS = (".cpp", ".c")


class ManifestError(Exception):
    """The manifest is missing, or armed but did not parse as expected.

    Caught only at the entry point and converted to exit 2 -- never exit 0
    and never exit 1. A rename or deletion of the manifest itself, while
    platform/py32f071/ still exists, must never look like UNARMED (D-07)
    and must never look like a clean pass.
    """


def parse_manifest(text):
    """Parse every `set()` block in `text` that looks like a source list
    (PATH_RE matches at least one entry in its body), plus every
    PY32_EXCLUDED: comment line.

    Returns (source_lists, excluded_valid, excluded_invalid):
      source_lists    -- {list_name: [raw_path, ...]} for every set() block
                         PATH_RE recognised as a source list, whatever its
                         name (including names in neither ENFORCED_LISTS nor
                         EXEMPT_LISTS -- the caller rejects those).
      excluded_valid   -- {path: reason} for every well-formed
                         `# PY32_EXCLUDED: <path> -- <reason>` comment line.
      excluded_invalid -- [raw_rest, ...] for every PY32_EXCLUDED: comment
                         line that does NOT carry a non-empty reason.
    """
    source_lists = {}
    for m in SET_BLOCK_RE.finditer(text):
        paths = [pm.group("path") for pm in PATH_RE.finditer(m.group("body"))]
        if paths:
            source_lists.setdefault(m.group("name"), []).extend(paths)

    excluded_valid = {}
    excluded_invalid = []
    for m in EXCLUDED_LINE_RE.finditer(text):
        rest = m.group("rest").strip()
        vm = EXCLUDED_RE.match(rest)
        if vm and vm.group("reason").strip():
            excluded_valid[vm.group("path")] = vm.group("reason").strip()
        else:
            excluded_invalid.append(rest)

    return source_lists, excluded_valid, excluded_invalid


def resolve_paths(list_name, raw_paths):
    """Resolve `raw_paths` from `list_name` to (raw, absolute_path) pairs.

    FIRESTARTER_COMMON_SOURCES entries substitute ${REPOSITORY_ROOT} with
    the manifest root (platform/py32f071/../.. per the manifest's own
    REPOSITORY_ROOT definition -- which equals the FIRESTARTER_MANIFEST_ROOT
    seam value). PY32_PLATFORM_SOURCES entries are bare paths relative to
    platform/py32f071/. Never called for EXEMPT_LISTS -- those paths are
    never resolved at all.
    """
    resolved = []
    for raw in raw_paths:
        if "${REPOSITORY_ROOT}" in raw:
            rel = raw.replace("${REPOSITORY_ROOT}/", "").replace("${REPOSITORY_ROOT}", "")
            resolved.append((raw, _ROOT / rel))
        else:
            resolved.append((raw, _PLATFORM_DIR / raw))
    return resolved


def enumerate_tree_sources():
    """Return every .cpp/.c file under <root>/src, as repo-relative POSIX
    strings, for the reverse-omission check. Empty list if src/ is absent.
    """
    src_dir = _ROOT / "src"
    if not src_dir.is_dir():
        return []
    return sorted(
        str(p.relative_to(_ROOT)).replace(os.sep, "/")
        for p in src_dir.rglob("*")
        if p.is_file() and p.suffix in _SOURCE_EXTS
    )


def main():
    if not ARMED:
        print(
            f"UNARMED: {_PLATFORM_DIR} absent -- this gate arms itself the "
            "moment Phase 124 lands the py32f071 port (no manual flip "
            "needed; a rename inside the port cannot disarm it either)."
        )
        return 0

    if not _MANIFEST_PATH.is_file():
        raise ManifestError(
            f"{_MANIFEST_PATH} not found under an ARMED platform/py32f071/ "
            "directory -- if the manifest was renamed or moved, update "
            "_MANIFEST_PATH in check_cmake_manifest.py rather than letting "
            "this gate silently disarm or silently pass"
        )

    text = _MANIFEST_PATH.read_text()
    source_lists, excluded_valid, excluded_invalid = parse_manifest(text)

    unknown = sorted(set(source_lists) - ENFORCED_LISTS - EXEMPT_LISTS)
    if unknown:
        print(
            f"ERROR: unrecognised source list(s) in {_MANIFEST_PATH}: "
            f"{unknown} -- neither ENFORCED_LISTS nor EXEMPT_LISTS names "
            "them; this is a configuration change the gate has not been "
            "taught about, and must never be silently ignored.",
            file=sys.stderr,
        )
        return 2

    violations = []
    enforced_resolved_count = 0
    enforced_common_relpaths = set()

    for list_name in sorted(ENFORCED_LISTS):
        raw_paths = source_lists.get(list_name, [])
        for raw, resolved in resolve_paths(list_name, raw_paths):
            enforced_resolved_count += 1
            if list_name == "FIRESTARTER_COMMON_SOURCES":
                try:
                    enforced_common_relpaths.add(
                        str(resolved.relative_to(_ROOT)).replace(os.sep, "/")
                    )
                except ValueError:
                    pass
            if not resolved.is_file():
                violations.append(f"{list_name}: {raw!r} -> {resolved} (not found)")

    if excluded_invalid:
        for raw_rest in excluded_invalid:
            violations.append(
                "PY32_EXCLUDED entry missing its mandatory reason segment: "
                f"{raw_rest!r} (required format is "
                "'PY32_EXCLUDED: <path> -- <reason>')"
            )

    allow_listed = []
    for relpath in enumerate_tree_sources():
        if relpath in enforced_common_relpaths:
            continue
        if relpath in excluded_valid:
            allow_listed.append(relpath)
            continue
        violations.append(
            f"{relpath}: present in tree, not named in "
            "FIRESTARTER_COMMON_SOURCES, and not covered by a reasoned "
            "PY32_EXCLUDED entry"
        )

    # Never-vacuous guard, folded into violations rather than a separate
    # early return: an empty or unparsed source list must not look like a
    # clean pass, regardless of what else was or wasn't found.
    if enforced_resolved_count == 0:
        violations.insert(
            0,
            "armed but resolved ZERO enforced sources across "
            f"{sorted(ENFORCED_LISTS)} -- an empty or unparsed source list "
            "must not look like a clean pass (never-vacuous guard)",
        )

    if violations:
        print(f"FAIL: {len(violations)} violation(s) in {_MANIFEST_PATH}:")
        for v in violations[:20]:
            print(f"  {v}")
        if len(violations) > 20:
            print(f"  ... and {len(violations) - 20} more")
        return 1

    exempt_count = len(source_lists.get("PY32_SDK_SOURCES", []))
    print(
        f"PASS: {_MANIFEST_PATH} -- {enforced_resolved_count} enforced "
        f"source(s) resolved across {sorted(ENFORCED_LISTS)}; {exempt_count} "
        "PY32_SDK_SOURCES entries structurally exempt (FetchContent -- "
        "PY32_SDK_ROOT resolves only after a networked cmake configure); "
        "allow-listed omission(s): "
        f"{', '.join(allow_listed) if allow_listed else '(none)'}"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ManifestError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(2)
