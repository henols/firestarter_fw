"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 126 Plan 06 -- CFG-06's map properties as an exit code: the reserved
py32f071 config flash region (platform/py32f071/linker/PY32F071xB_FLASH.ld)
is sector-aligned, lies inside the physical part, holds two slots exactly
one page apart, cannot be reached by the app region, exposes all four
required symbols, and carries D-13's zero-length bootloader seam with its
migration-cost comment intact.

Requirements: CFG-05, CFG-06, CFG-07
Decisions covered: D-10, D-11, D-12, D-13, D-18, C-3, C-4, D-02

This module invokes no compiler -- every assertion below is a textual gate
over committed source, following tests/test_vpp_seam_manual_on_every_board.py
's plain read_text() idiom (`:333-386`), deliberately not parsed into a
structure beyond the minimal region/symbol extraction this gate needs.

Every piece of arithmetic RESEARCH A6 keeps OUT of the linker script lives
here instead: the sector-alignment modulo, the inside-the-physical-part
bounds check, and the app-cannot-reach-config comparison. `ASSERT` with a
modulo on a region origin is the construct RESEARCH A6 rates most likely to
need adjustment on real arm-none-eabi-ld, and no ARM linker exists in this
environment to try it against -- Python, which can actually run the
arithmetic, is the venue instead.

This module executes in NO CI leg on this branch: `pytest tests/ -v` appears
only in build.yml (push/PR to main) and beta-build.yml (push to beta) --
neither fires on this firmware milestone branch, and py32f071.yml has no
pytest step at all. The local run recorded in this phase's SUMMARY is the
only evidence this module's assertions were ever exercised.

Plan 126-08 EXTENDED this module with `test_manifest_names_the_flash_driver`
(C-3's edit-4 assertion) and five sibling functions, once
platform/py32f071/src/config_storage_flash.cpp existed and the tree genuinely
contained `HAL_FLASH_` calls -- an implication-shaped assertion with no
antecedent would have been vacuous before that plan landed. No second module
was created for this extension; it lives here.

**Why `test_manifest_names_the_flash_driver` must prove its own antecedent
(C-3, Pitfall 3):** `platform/py32f071/include/py32f071_hal_conf.h:10` already
sets `HAL_FLASH_MODULE_ENABLED` and `:53` already includes
`py32f071_hal_flash.h`, so every `HAL_FLASH_` call **compiles** -- the failure
this assertion guards against is at **link**, not compile. `scripts/check_cmake_manifest.py`'s
own docstring declares `PY32_SDK_SOURCES` structurally exempt from resolution,
because `PY32_SDK_ROOT` resolves only after a networked `cmake` configure, and
`cmake`, `ninja` and `arm-none-eabi-gcc` are all absent from this environment
-- nothing else here would ever catch a missing flash driver. Without this
assertion proving a real, non-zero count of `HAL_FLASH_` call sites before
checking the manifest, the first sign of the defect would be a gated CI run,
hours after the code was written.

Self-contained path resolution below -- NOT in conftest.py (firestarter/
tests/ has no conftest.py anywhere in the repo, and none of pytest.ini,
pyproject.toml, setup.cfg or tox.ini exist either; a recorded house-rule
pattern decision, not an omission). Stdlib and pytest only.

Every check below is factored into a module-level `_violations_*` helper
taking parsed structures (never raw un-parsed text, except where the check
is itself textual) and returning a list of violations. The positive tests
and the planted-copy RED demonstration (Coverage 13) both call the same
helpers -- a parallel second implementation inside the RED test would prove
nothing about the gate (Phase 124 D-14: "a guard that supplies the answer
it tests is structurally dead"). No blob SHA literal appears anywhere in
this module.

Coverage:
  1. test_config_region_origin_and_length_are_the_recorded_values -- CONFIG's
     ORIGIN and LENGTH parsed from the MEMORY block equal 0x0801E000 and
     8192 (8 KiB).
  2. test_config_page_size_symbol_is_the_reference_manual_figure --
     __config_page_size is PROVIDEd as 256, matching the CFG-02 record.
  3. test_the_two_slots_are_in_different_page_erase_units --
     __config_slot_a_start resolves to ORIGIN(CONFIG) and
     __config_slot_b_start to ORIGIN(CONFIG) + 256, exactly one page apart.
  4. test_config_region_is_sector_aligned -- 0x0801E000 is a multiple of
     8192 -- the arithmetic A6 keeps out of the linker script.
  5. test_config_region_lies_inside_the_physical_part -- CONFIG's start and
     end both lie within 0x08000000 .. 0x08000000 + 128 KiB.
  6. test_app_region_cannot_reach_the_config_region -- FLASH's LENGTH is
     120K and ORIGIN(FLASH) + LENGTH(FLASH) <= ORIGIN(CONFIG).
  7. test_all_four_config_symbols_are_provided -- all four symbol names
     appear in PROVIDE statements.
  8. test_bootloader_seam_is_present_and_zero_length -- accepts either a
     BOOTLOADER region with LENGTH = 0, or the A7 fallback pair of
     PROVIDEd start/size symbols with size 0. At least one shape must
     exist; a non-zero length or size in either shape is a violation.
  9. test_bootloader_seam_carries_its_migration_cost_comment -- the comment
     states that giving the seam a size moves the application origin and is
     a migration rather than a resize.
  10. test_the_linker_script_cites_the_geometry_record -- the leading
      comment names CONFIG-STORAGE.md and the RM identifiers.
  11. test_no_pre_pv32f071_page_or_sector_figure_appears_near_a_config_address
      -- Pitfall 1's warning signs (128, 0x80, 2048, 4096) are absent from
      the MEMORY block and the config-symbol region, each named with the
      wrong source it corresponds to.
  12. test_host_contract_asymmetry_is_recorded -- CONFIG-STORAGE.md records
      FLASH_BASE 0x08000000, the 131072 envelope, and the correct-not-drift
      statement (C-10 / D-12).
  13. test_helper_reports_violations_on_planted_copies -- the RED
      demonstration: six planted mutated copies in tmp_path, each fed to the
      same module-level helpers, each producing a non-empty violation list.
      The real linker script's blob SHA is unchanged before and after.
  14. test_compiler_is_required_not_optional -- the self-enforcing no-skip
      leg.
  15. test_manifest_names_the_flash_driver -- C-3's edit-4 assertion: an
      implication with a PROVEN antecedent. Scans platform/py32f071/src/ for
      any `HAL_FLASH_` call, fails if the scan finds none (the antecedent
      must be real, not assumed), then asserts the manifest names
      py32f071_hal_flash.c. Records the scanned-file count and the
      call-site count in the assertion message.
  16. test_the_flash_glue_uses_only_hal_entry_points -- C-4: the HAL glue TU
      (config_storage_flash.cpp) calls only HAL_FLASH_Unlock, HAL_FLASH_Erase,
      HAL_FLASH_Program and HAL_FLASH_Lock, and contains no direct
      flash-register access (no `FLASH->...`).
  17. test_the_dualslot_core_stays_hal_free -- config_storage_dualslot.cpp's
      include set is exactly its local header plus C standard headers, with
      no HAL call and no platform #error guard, so the host harness can
      compile it directly.
  18. test_pr48_config_cpp_is_absent_from_the_tree -- CFG-07's deletion,
      verified by absence.
  19. test_slot_addresses_come_from_linker_symbols -- the glue TU declares
      the three linker symbols extern and does not duplicate any config
      address as a literal constant.
  20. test_manifest_helper_reports_violations_on_planted_copies -- the RED
      demonstration: a manifest with the flash-driver entry removed (while a
      HAL_FLASH_ call exists), a glue TU with a direct register access, and a
      core TU with a HAL include, each fed to the same module-level helpers
      the positive tests above call, each producing a non-empty violation
      list. No committed file is mutated; the three watched blob SHAs
      (CMakeLists.txt, config_storage_flash.cpp, config_storage_dualslot.cpp)
      are confirmed unchanged before and after.
"""

import re
import shutil
import subprocess
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_LINKER_PATH = _REPO_ROOT / "platform" / "py32f071" / "linker" / "PY32F071xB_FLASH.ld"
_CONFIG_STORAGE_MD = _REPO_ROOT / "platform" / "py32f071" / "CONFIG-STORAGE.md"

_PY32_SRC_DIR = _REPO_ROOT / "platform" / "py32f071" / "src"
_MANIFEST_PATH = _REPO_ROOT / "platform" / "py32f071" / "CMakeLists.txt"
_GLUE_PATH = _PY32_SRC_DIR / "config_storage_flash.cpp"
_CORE_PATH = _PY32_SRC_DIR / "config_storage_dualslot.cpp"
_CONFIG_CPP_PATH = _PY32_SRC_DIR / "config.cpp"
_FLASH_DRIVER_NAME = "py32f071_hal_flash.c"

_ALLOWED_HAL_FLASH_CALLS = {"HAL_FLASH_Unlock", "HAL_FLASH_Erase", "HAL_FLASH_Program", "HAL_FLASH_Lock"}
_HAL_FLASH_CALL_RE = re.compile(r"\b(HAL_FLASH_\w+)\s*\(")
_DIRECT_REGISTER_RE = re.compile(r"\bFLASH\s*->\s*\w+")
_COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
_CORE_LOCAL_INCLUDE_RE = re.compile(r'#\s*include\s*([<"][^">]+[">])')
_SANCTIONED_CORE_LOCAL_INCLUDES = {"config_storage_dualslot.h"}
_REQUIRED_EXTERN_SLOT_SYMBOLS = ("__config_slot_a_start", "__config_slot_b_start", "__config_page_size")
_LITERAL_SLOT_ADDRESS_RE = re.compile(r"0x0801[Ee]000|0x0801[Ee]100")

_REQUIRED_SYMBOLS = (
    "__config_page_size",
    "__config_slot_a_start",
    "__config_slot_b_start",
    "__config_region_end",
)

_REGION_RE = re.compile(
    r"^\s*(\w+)\s*\([A-Za-z]+\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*,"
    r"\s*LENGTH\s*=\s*(\d+)\s*([KkMm]?)\s*$",
    re.MULTILINE,
)
_PROVIDE_RE = re.compile(r"PROVIDE\(\s*(__\w+)\s*=\s*(.*?)\)\s*;")
_FALLBACK_START_RE = re.compile(
    r"PROVIDE\(\s*__bootloader_region_start\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*\)\s*;"
)
_FALLBACK_SIZE_RE = re.compile(r"PROVIDE\(\s*__bootloader_region_size\s*=\s*(\d+)\s*\)\s*;")

_WRONG_FIGURE_PATTERNS = {
    r"\b128\b": (
        "128 -- the PY32F030/F003 page-size figure (wrong for this part; "
        "also the stale py32f071_hal_flash.h:268 comment's claim)"
    ),
    r"\b0x80\b": "0x80 -- the hex form of the wrong 128-byte page-size figure",
    r"\b2048\b": (
        "2048 -- the host's DFU DEFAULT_ERASE_PAGE_SIZE fallback grid, which "
        "matches neither this part's page nor its sector size (C-9)"
    ),
    r"\b4096\b": (
        "4096 -- the commonly-circulated (wrong) '4 KiB sector' PY32 figure "
        "(correct for PY32F030/F003, wrong for PY32F071)"
    ),
}


def _resolve_git():
    """Resolve the `git` binary, fail-closed."""
    git_bin = shutil.which("git")
    assert git_bin is not None, (
        "`git` binary not found on PATH. This must FAIL the suite, never be "
        "silently skipped."
    )
    return git_bin


def _git(*args):
    git_bin = _resolve_git()
    argv = [git_bin, "-C", str(_REPO_ROOT), *args]
    return subprocess.run(argv, capture_output=True, text=True)


def _resolve_expr(expr, regions):
    """Resolve a PROVIDE expression (an int literal, ORIGIN(x), LENGTH(x),
    or a '+' sum of those) to an integer, given a parsed regions mapping."""
    total = 0
    for term in expr.split("+"):
        term = term.strip()
        m = re.match(r"^ORIGIN\((\w+)\)$", term)
        if m:
            total += regions[m.group(1)][0]
            continue
        m = re.match(r"^LENGTH\((\w+)\)$", term)
        if m:
            total += regions[m.group(1)][1]
            continue
        total += int(term, 0)
    return total


def _parse_regions(text):
    """Returns a dict of region name -> (origin: int, length: int), parsed
    from the MEMORY { ... } block. K/M suffixes on LENGTH are normalised to
    bytes."""
    m = re.search(r"MEMORY\s*\{(.*?)\n\}", text, re.DOTALL)
    if not m:
        return {}
    block = m.group(1)
    regions = {}
    for name, origin_s, length_s, suffix in _REGION_RE.findall(block):
        origin = int(origin_s, 0)
        length = int(length_s)
        if suffix.lower() == "k":
            length *= 1024
        elif suffix.lower() == "m":
            length *= 1024 * 1024
        regions[name] = (origin, length)
    return regions


def _parse_symbols(text, regions):
    """Returns a dict of PROVIDEd symbol name -> resolved integer value,
    excluding the two A7 fallback bootloader symbols (handled separately by
    the bootloader-seam check)."""
    symbols = {}
    for name, expr in _PROVIDE_RE.findall(text):
        if name in ("__bootloader_region_start", "__bootloader_region_size"):
            continue
        symbols[name] = _resolve_expr(expr, regions)
    return symbols


def _scoped_map_text(text):
    """Scope a textual scan to the MEMORY block through the PROVIDE/ASSERT
    block, EXCLUDING the leading file-level comment (which legitimately
    cites the physical part's total '128 KiB' size -- that is correct and
    expected, not a warning sign). Falls back to the whole text if the
    expected anchors are not found."""
    if "MEMORY" in text and "_estack" in text:
        return text[text.index("MEMORY") : text.index("_estack")]
    return text


# --- module-level violation helpers, shared by positive and RED tests ---


def _violations_config_origin_length(regions):
    cfg = regions.get("CONFIG")
    if cfg is None:
        return ["no CONFIG region found in the MEMORY block"]
    origin, length = cfg
    v = []
    if origin != 0x0801E000:
        v.append(f"CONFIG ORIGIN is {hex(origin)}, expected 0x0801E000")
    if length != 8192:
        v.append(f"CONFIG LENGTH is {length}, expected 8192 (8K, one whole sector)")
    return v


def _violations_page_size(symbols):
    val = symbols.get("__config_page_size")
    if val != 256:
        return [
            f"__config_page_size resolved to {val!r}, expected 256 (RM V0.2 "
            f"§4.1/§4.2.1/Table 4-1) -- 128 is the PY32F030/F003 figure, not "
            f"this part's"
        ]
    return []


def _violations_slots_different_page(symbols):
    a = symbols.get("__config_slot_a_start")
    b = symbols.get("__config_slot_b_start")
    page = symbols.get("__config_page_size")
    if a is None or b is None or page is None:
        return ["cannot check slot spacing: one or more required symbols missing"]
    if b - a != page:
        return [
            f"slot_b - slot_a = {b - a}, expected exactly one page ({page}) "
            f"apart -- the two slots must be in different erase units"
        ]
    return []


def _violations_sector_alignment(regions):
    cfg = regions.get("CONFIG")
    if cfg is None:
        return ["no CONFIG region to check for sector alignment"]
    origin, _length = cfg
    if origin % 8192 != 0:
        return [
            f"CONFIG ORIGIN {hex(origin)} is not a multiple of 8192 (one "
            f"sector) -- this arithmetic lives in Python (RESEARCH A6) "
            f"because there is no ARM linker here to test a modulo ASSERT"
        ]
    return []


def _violations_inside_physical_part(regions):
    cfg = regions.get("CONFIG")
    if cfg is None:
        return ["no CONFIG region to check physical bounds"]
    origin, length = cfg
    lo, hi = 0x08000000, 0x08000000 + 128 * 1024
    if not (lo <= origin and origin + length <= hi):
        return [
            f"CONFIG [{hex(origin)}, {hex(origin + length)}) is not fully "
            f"inside the physical part [{hex(lo)}, {hex(hi)})"
        ]
    return []


def _violations_app_cannot_reach_config(regions):
    flash = regions.get("FLASH")
    cfg = regions.get("CONFIG")
    if flash is None or cfg is None:
        return ["cannot check app-vs-config overlap: FLASH or CONFIG region missing"]
    if flash[0] + flash[1] > cfg[0]:
        return [
            f"FLASH ends at {hex(flash[0] + flash[1])}, which reaches into "
            f"CONFIG starting at {hex(cfg[0])} -- the app region must shrink "
            f"so overlap is a link failure, not a runtime surprise"
        ]
    return []


def _violations_all_symbols_provided(symbols):
    missing = [name for name in _REQUIRED_SYMBOLS if name not in symbols]
    if missing:
        return [f"missing PROVIDE for required symbol(s): {missing}"]
    return []


def _violations_bootloader_seam(regions, text):
    region = regions.get("BOOTLOADER")
    start_m = _FALLBACK_START_RE.search(text)
    size_m = _FALLBACK_SIZE_RE.search(text)
    has_region_shape = region is not None
    has_fallback_shape = start_m is not None and size_m is not None

    if not has_region_shape and not has_fallback_shape:
        return [
            "no BOOTLOADER region and no __bootloader_region_start/"
            "__bootloader_region_size fallback pair found -- D-13's seam "
            "must not be dropped"
        ]
    v = []
    if has_region_shape and region[1] != 0:
        v.append(
            f"BOOTLOADER region LENGTH is {region[1]}, expected 0 -- D-13's "
            f"seam must stay zero-length, never quietly upgraded to a real "
            f"reservation"
        )
    if has_fallback_shape:
        size_val = int(size_m.group(1))
        if size_val != 0:
            v.append(
                f"__bootloader_region_size is {size_val}, expected 0 -- "
                f"D-13's seam must stay zero-length"
            )
    return v


def _violations_bootloader_migration_comment(text):
    required_phrases = (
        "MOVES the application's ORIGIN",
        "MIGRATION",
        "not a resize",
    )
    missing = [p for p in required_phrases if p not in text]
    if missing:
        return [f"bootloader seam migration-cost comment missing phrase(s): {missing}"]
    return []


def _violations_cites_geometry_record(text):
    v = []
    if "CONFIG-STORAGE.md" not in text:
        v.append("linker script does not cite platform/py32f071/CONFIG-STORAGE.md")
    if "V0.2" not in text:
        v.append("linker script does not cite the reference-manual version 'V0.2'")
    return v


def _violations_wrong_page_sector_figures(text):
    scoped = _scoped_map_text(text)
    v = []
    for pattern, source in _WRONG_FIGURE_PATTERNS.items():
        if re.search(pattern, scoped):
            v.append(f"found offending figure near a config address: {source}")
    return v


def _all_violations(text):
    """Composes every atomic helper above over one parsed copy of the
    linker-script text. Used by the RED demonstration only -- it is
    composition of the existing helpers, never a second implementation of
    their logic."""
    regions = _parse_regions(text)
    symbols = _parse_symbols(text, regions)
    v = []
    v += _violations_config_origin_length(regions)
    v += _violations_page_size(symbols)
    v += _violations_slots_different_page(symbols)
    v += _violations_sector_alignment(regions)
    v += _violations_inside_physical_part(regions)
    v += _violations_app_cannot_reach_config(regions)
    v += _violations_all_symbols_provided(symbols)
    v += _violations_bootloader_seam(regions, text)
    v += _violations_wrong_page_sector_figures(text)
    return v


# --- fixtures shared by the real-repo tests ---


def _real_text():
    return _LINKER_PATH.read_text()


def _real_parsed():
    text = _real_text()
    regions = _parse_regions(text)
    symbols = _parse_symbols(text, regions)
    return text, regions, symbols


# --- positive (real-repo) tests ---


def test_config_region_origin_and_length_are_the_recorded_values():
    """Coverage 1."""
    _, regions, _ = _real_parsed()
    assert not _violations_config_origin_length(regions), (
        f"expected CONFIG region at 0x0801E000 length 8192, got {regions.get('CONFIG')}"
    )
    assert regions["CONFIG"] == (0x0801E000, 8192)


def test_config_page_size_symbol_is_the_reference_manual_figure():
    """Coverage 2."""
    _, _, symbols = _real_parsed()
    violations = _violations_page_size(symbols)
    assert not violations, violations
    assert symbols["__config_page_size"] == 256


def test_the_two_slots_are_in_different_page_erase_units():
    """Coverage 3."""
    _, _, symbols = _real_parsed()
    violations = _violations_slots_different_page(symbols)
    assert not violations, violations
    assert symbols["__config_slot_a_start"] == 0x0801E000
    assert symbols["__config_slot_b_start"] == 0x0801E100


def test_config_region_is_sector_aligned():
    """Coverage 4."""
    _, regions, _ = _real_parsed()
    violations = _violations_sector_alignment(regions)
    assert not violations, violations
    assert regions["CONFIG"][0] % 8192 == 0


def test_config_region_lies_inside_the_physical_part():
    """Coverage 5."""
    _, regions, _ = _real_parsed()
    violations = _violations_inside_physical_part(regions)
    assert not violations, violations


def test_app_region_cannot_reach_the_config_region():
    """Coverage 6."""
    _, regions, _ = _real_parsed()
    violations = _violations_app_cannot_reach_config(regions)
    assert not violations, violations
    assert regions["FLASH"] == (0x08000000, 122880)


def test_all_four_config_symbols_are_provided():
    """Coverage 7."""
    _, _, symbols = _real_parsed()
    violations = _violations_all_symbols_provided(symbols)
    assert not violations, violations
    for name in _REQUIRED_SYMBOLS:
        assert name in symbols


def test_bootloader_seam_is_present_and_zero_length():
    """Coverage 8."""
    text, regions, _ = _real_parsed()
    violations = _violations_bootloader_seam(regions, text)
    assert not violations, violations


def test_bootloader_seam_carries_its_migration_cost_comment():
    """Coverage 9."""
    text = _real_text()
    violations = _violations_bootloader_migration_comment(text)
    assert not violations, violations


def test_the_linker_script_cites_the_geometry_record():
    """Coverage 10."""
    text = _real_text()
    violations = _violations_cites_geometry_record(text)
    assert not violations, violations


def test_no_pre_pv32f071_page_or_sector_figure_appears_near_a_config_address():
    """Coverage 11."""
    text = _real_text()
    violations = _violations_wrong_page_sector_figures(text)
    assert not violations, violations


def test_host_contract_asymmetry_is_recorded():
    """Coverage 12 -- D-12(b) / C-10: CONFIG-STORAGE.md records FLASH_BASE
    0x08000000, the physical 131072 envelope, and the statement that the
    120K-vs-128K asymmetry is correct rather than drift."""
    text = _CONFIG_STORAGE_MD.read_text()
    assert "FLASH_BASE" in text, f"expected FLASH_BASE cited in {_CONFIG_STORAGE_MD}"
    assert "0x08000000" in text, f"expected 0x08000000 cited in {_CONFIG_STORAGE_MD}"
    assert "131072" in text, f"expected the physical 131072 envelope cited in {_CONFIG_STORAGE_MD}"
    assert re.search(r"correct,\s+not\s+drift", text), (
        f"expected the correct-not-drift statement in {_CONFIG_STORAGE_MD} -- "
        f"the 120K-vs-128K asymmetry must be recorded as intentional, never "
        f"as an unexplained mismatch"
    )


def test_helper_reports_violations_on_planted_copies(tmp_path):
    """Coverage 13 -- the RED demonstration. Six planted mutated copies of
    the real linker script, each with exactly one property broken, each fed
    to the same module-level helpers via _all_violations. Each must produce
    a non-empty violation list. The real file's blob SHA is confirmed
    unchanged before and after."""
    before_blob = _git("hash-object", str(_LINKER_PATH)).stdout.strip()
    real_text = _real_text()

    planted = {
        "non-sector-aligned CONFIG origin": real_text.replace(
            "CONFIG (r)  : ORIGIN = 0x0801E000, LENGTH = 8K",
            "CONFIG (r)  : ORIGIN = 0x0801FE00, LENGTH = 8K",
        ),
        "slots two pages apart": real_text.replace(
            "PROVIDE(__config_slot_b_start = ORIGIN(CONFIG) + 256);",
            "PROVIDE(__config_slot_b_start = ORIGIN(CONFIG) + 512);",
        ),
        "BOOTLOADER given a non-zero length": real_text.replace(
            "BOOTLOADER (rx) : ORIGIN = 0x08000000, LENGTH = 0",
            "BOOTLOADER (rx) : ORIGIN = 0x08000000, LENGTH = 8K",
        ),
        "FLASH left at the physical 128K": real_text.replace(
            "FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 120K",
            "FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K",
        ),
        "a missing PROVIDE": real_text.replace(
            "PROVIDE(__config_region_end   = ORIGIN(CONFIG) + LENGTH(CONFIG));  /* 0x08020000 */\n",
            "",
        ),
        "a 128-byte page size": real_text.replace(
            "PROVIDE(__config_page_size    = 256);",
            "PROVIDE(__config_page_size    = 128);",
        ),
    }

    for description, mutated_text in planted.items():
        assert mutated_text != real_text, (
            f"planted copy {description!r} did not actually differ from the "
            f"real text -- the replacement target string was not found"
        )
        planted_path = tmp_path / f"planted-{abs(hash(description))}.ld"
        planted_path.write_text(mutated_text)
        violations = _all_violations(mutated_text)
        assert violations, (
            f"expected a non-empty violation list for the planted copy "
            f"{description!r}, got none"
        )

    after_blob = _git("hash-object", str(_LINKER_PATH)).stdout.strip()
    assert before_blob == after_blob, (
        f"expected the real linker script's blob SHA to be unchanged by "
        f"this test, got {before_blob} before and {after_blob} after"
    )


def test_compiler_is_required_not_optional():
    """Coverage 14 -- this module invokes no compiler; the self-enforcing
    no-skip leg asserts only the absence of a skip call and a conditional-
    skip marker.

    The two needle strings below are built via concatenation (not written
    verbatim) so this test's own assertion text does not trip its own
    check -- the literal substrings must appear NOWHERE in this file,
    including inside this test's failure messages."""
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- a "
        "tool-absence case must FAIL, never SKIP."
    )
    assert skipif_marker not in own_text, (
        "expected no @pytest." + skipif_marker + " decorator anywhere in "
        "this module -- a tool-absence case must FAIL, never SKIP."
    )



def _scan_hal_flash_calls(src_dir, repo_root):
    """Scan every .c/.cpp/.h file under src_dir for a HAL_FLASH_* call.

    Returns (files_scanned, call_sites) where call_sites is a list of
    (repo-relative-path, matched-symbol-name) tuples. This is the proven
    antecedent for test_manifest_names_the_flash_driver -- the caller MUST
    assert call_sites is non-empty before trusting the implication below,
    or the assertion would be vacuously true (exactly the shape this
    project's history has had to unwind twice)."""
    files_scanned = 0
    call_sites = []
    for path in sorted(src_dir.rglob("*")):
        if path.is_file() and path.suffix in (".c", ".cpp", ".h"):
            files_scanned += 1
            stripped = _COMMENT_RE.sub("", path.read_text())
            for m in _HAL_FLASH_CALL_RE.finditer(stripped):
                call_sites.append((str(path.relative_to(repo_root)), m.group(1)))
    return files_scanned, call_sites


def _violations_manifest_names_flash_driver(manifest_text, call_site_count, driver_name=_FLASH_DRIVER_NAME):
    """C-3's edit-4 assertion, as a violations helper: the manifest must
    name driver_name whenever call_site_count is nonzero. call_site_count is
    passed in (not recomputed) so the RED demonstration can plant a
    manifest missing the entry while still asserting against a real,
    nonzero count."""
    if call_site_count == 0:
        return [
            "antecedent unproven: 0 HAL_FLASH_ call sites found -- refusing "
            "to evaluate a vacuous implication (an implication with an "
            "unproven antecedent is exactly the vacuous shape this project "
            "has had to unwind twice)"
        ]
    if driver_name not in manifest_text:
        return [
            f"{driver_name!r} not named anywhere in the manifest, despite "
            f"{call_site_count} HAL_FLASH_ call site(s) found in the tree"
        ]
    return []


def _hal_flash_calls_in_text(text):
    return sorted(set(_HAL_FLASH_CALL_RE.findall(_COMMENT_RE.sub("", text))))


def _violations_hal_entry_points_only(text):
    """C-4: only the four sanctioned HAL_FLASH_* entry points may be called,
    and no direct flash-register access (FLASH->...) may appear. Comments
    are stripped first so a prose mention (e.g. explaining why FLASH->CR is
    NOT poked directly) is never mistaken for actual register access."""
    violations = []
    stripped = _COMMENT_RE.sub("", text)
    calls = _hal_flash_calls_in_text(text)
    unexpected = [c for c in calls if c not in _ALLOWED_HAL_FLASH_CALLS]
    if unexpected:
        violations.append(
            f"unexpected HAL_FLASH_* call(s): {unexpected!r} -- only "
            f"{sorted(_ALLOWED_HAL_FLASH_CALLS)!r} are permitted (C-4)"
        )
    if _DIRECT_REGISTER_RE.search(stripped):
        violations.append(
            "found direct flash-register access ('FLASH->...') -- C-4 "
            "requires routing exclusively through the four HAL_FLASH_* "
            "entry points; RM V0.2 §4.2.3.6 configures the timing registers "
            "INSIDE those entry points, so a hand-rolled register sequence "
            "compiles, reviews cleanly, and fails only on silicon"
        )
    return violations


def _violations_core_hal_free(text):
    """The dual-slot core's include set must be exactly its local header
    plus C standard headers (angle-bracket includes), with no HAL call and
    no platform #error guard -- so the host test harness can compile this
    TU directly against a RAM fake (D-02). Comments are stripped first."""
    violations = []
    stripped = _COMMENT_RE.sub("", text)
    for raw in _CORE_LOCAL_INCLUDE_RE.findall(stripped):
        if raw.startswith('"'):
            name = raw.strip('"')
            if name not in _SANCTIONED_CORE_LOCAL_INCLUDES:
                violations.append(
                    f"unexpected local include {raw!r} in the dual-slot "
                    f"core -- expected only "
                    f"{sorted(_SANCTIONED_CORE_LOCAL_INCLUDES)!r}"
                )
    if re.search(r"\bHAL_\w+\s*\(", stripped):
        violations.append(
            "found a HAL_* call in the dual-slot core -- this TU must stay "
            "HAL-free so the host harness can compile it directly (D-02)"
        )
    if "#error" in stripped:
        violations.append(
            "found a platform #error guard in the dual-slot core -- the "
            "guard belongs only in the HAL glue TU (config_storage_flash.cpp), "
            "never here, because the host harness compiles this file directly"
        )
    return violations


def _violations_slot_addresses_from_linker(text):
    """D-11: the glue TU must declare the three linker symbols extern and
    must never duplicate a config-slot address as a literal constant."""
    violations = []
    for sym in _REQUIRED_EXTERN_SLOT_SYMBOLS:
        if not re.search(rf"extern\s+uint32_t\s+{re.escape(sym)}\s*;", text):
            violations.append(
                f"expected 'extern uint32_t {sym};' declared in the glue "
                f"TU, not found"
            )
    if _LITERAL_SLOT_ADDRESS_RE.search(text):
        violations.append(
            "found a literal config-slot address (e.g. 0x0801E000 / "
            "0x0801E100) in the glue TU -- slot addresses must come only "
            "from the linker symbols (D-11), never a duplicated constant"
        )
    return violations


def test_manifest_names_the_flash_driver():
    """Coverage 15 -- C-3's edit-4 assertion: an implication with a PROVEN
    antecedent. Fails if the scan finds zero HAL_FLASH_ call sites; records
    the scanned-file count and the call-site count either way."""
    files_scanned, call_sites = _scan_hal_flash_calls(_PY32_SRC_DIR, _REPO_ROOT)
    assert call_sites, (
        f"antecedent unproven: scanned {files_scanned} file(s) under "
        f"{_PY32_SRC_DIR}, found 0 HAL_FLASH_ call sites -- the implication "
        f"this test checks would be vacuously true"
    )
    manifest_text = _MANIFEST_PATH.read_text()
    violations = _violations_manifest_names_flash_driver(manifest_text, len(call_sites))
    assert violations == [], (
        f"scanned {files_scanned} file(s) under {_PY32_SRC_DIR}, found "
        f"{len(call_sites)} HAL_FLASH_ call site(s): {call_sites!r}.\n"
        f"Violations:\n" + "\n".join(violations)
    )


def test_the_flash_glue_uses_only_hal_entry_points():
    """Coverage 16 -- C-4: the glue TU calls only the four sanctioned
    HAL_FLASH_* entry points and contains no direct flash-register access."""
    text = _GLUE_PATH.read_text()
    violations = _violations_hal_entry_points_only(text)
    assert violations == [], violations
    calls = _hal_flash_calls_in_text(text)
    for expected in sorted(_ALLOWED_HAL_FLASH_CALLS):
        assert expected in calls, f"expected {expected} to be called in {_GLUE_PATH}, not found"


def test_the_dualslot_core_stays_hal_free():
    """Coverage 17 -- the dual-slot core's include set is exactly its local
    header plus C standard headers, with no HAL call and no platform #error
    guard."""
    text = _CORE_PATH.read_text()
    violations = _violations_core_hal_free(text)
    assert violations == [], violations


def test_pr48_config_cpp_is_absent_from_the_tree():
    """Coverage 18 -- CFG-07's deletion, verified by absence, not by diff."""
    assert not _CONFIG_CPP_PATH.exists(), (
        f"expected {_CONFIG_CPP_PATH} to be absent from the tree (CFG-07) -- "
        f"PR #48's rurp_save_config() persisted nothing and must be deleted, "
        f"not reconciled"
    )


def test_slot_addresses_come_from_linker_symbols():
    """Coverage 19 -- D-11: the glue TU declares the three linker symbols
    extern and does not duplicate any config address as a literal constant."""
    text = _GLUE_PATH.read_text()
    violations = _violations_slot_addresses_from_linker(text)
    assert violations == [], violations


def test_manifest_helper_reports_violations_on_planted_copies():
    """Coverage 20 -- the RED demonstration. Three independently planted
    copies -- a manifest with the flash-driver entry removed (antecedent
    forced nonzero), a glue TU with a direct register access, and a core TU
    with a HAL include -- each fed to the same module-level helpers the
    positive tests above call, each producing a non-empty violation list.
    No committed file is mutated; the three watched blob SHAs are confirmed
    unchanged before and after."""
    watched_paths = (_MANIFEST_PATH, _GLUE_PATH, _CORE_PATH)
    shas_before = {p: _git("hash-object", str(p)).stdout.strip() for p in watched_paths}

    manifest_text = _MANIFEST_PATH.read_text()
    assert _FLASH_DRIVER_NAME in manifest_text, (
        "fixture assumption failed: the real manifest no longer names "
        f"{_FLASH_DRIVER_NAME!r} -- this mutation expects to find and "
        "remove it"
    )
    mutated_manifest = manifest_text.replace(
        f'"${{PY32_SDK_ROOT}}/Drivers/PY32F071_HAL_Driver/Src/{_FLASH_DRIVER_NAME}"\n', ""
    )
    assert mutated_manifest != manifest_text, (
        "planted copy 'manifest missing the flash driver entry' did not "
        "actually differ from the real text"
    )
    violations_1 = _violations_manifest_names_flash_driver(mutated_manifest, call_site_count=1)
    assert violations_1, (
        "expected a non-empty violation list for a manifest missing the "
        "flash-driver entry while a HAL_FLASH_ call exists, got none"
    )

    glue_text = _GLUE_PATH.read_text()
    mutated_glue = glue_text + "\nstatic void _planted_direct_register_access() { FLASH->CR = 0; }\n"
    assert mutated_glue != glue_text, (
        "planted copy 'glue TU with direct register access' did not "
        "actually differ from the real text"
    )
    violations_2 = _violations_hal_entry_points_only(mutated_glue)
    assert violations_2, (
        "expected a non-empty violation list for a glue TU carrying a "
        "direct register access, got none"
    )

    core_text = _CORE_PATH.read_text()
    mutated_core = '#include "py32f071_hal.h"\n' + core_text
    assert mutated_core != core_text, (
        "planted copy 'core TU with a HAL include' did not actually differ "
        "from the real text"
    )
    violations_3 = _violations_core_hal_free(mutated_core)
    assert violations_3, (
        "expected a non-empty violation list for a core TU carrying a HAL "
        "include, got none"
    )

    shas_after = {p: _git("hash-object", str(p)).stdout.strip() for p in watched_paths}
    assert shas_after == shas_before, (
        f"expected no committed file to be mutated by this planted-copy "
        f"demonstration; blob SHAs before: {shas_before!r}, after: "
        f"{shas_after!r}"
    )
