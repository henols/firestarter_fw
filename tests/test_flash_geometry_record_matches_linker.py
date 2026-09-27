"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson

Permission is hereby granted under MIT license.

The PY32F071 flash geometry and flash map recorded in
`platform/py32f071/DESIGN.md` must agree with the linker script
`platform/py32f071/linker/PY32F071xB_FLASH.ld`.

This module replaces a git-history gate. That gate asked `git` whether the
commit that added the geometry record came before every commit that edited the
linker script. The rule behind it: the linker map must follow from recorded,
cited geometry, never from a guess. When the record moved into `DESIGN.md`,
the adding commit became newer than the linker edits, so the history question
no longer measured anything. This module checks the content fact instead:

1. The record states the page size, the sector size and the main-flash range.
2. The sector size is a whole number of pages.
3. `CONFIG` starts on a sector boundary, is a whole number of sectors, and ends
   exactly at the end of main flash.
4. `__config_page_size` is the recorded page size, and the two slots are one
   page apart inside `CONFIG`.
5. `FLASH` ends at or before `ORIGIN(CONFIG)`.
6. The flash-map table in the record has the same origin and length as the
   linker script for every region and config symbol.

Every test calls the one helper `_violations(design_text, linker_text)`. The
synthetic tests feed it violating input, so each check is shown to fail.
Stdlib and pytest only.
"""

import re
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_DESIGN = _REPO_ROOT / "platform" / "py32f071" / "DESIGN.md"
_LINKER = _REPO_ROOT / "platform" / "py32f071" / "linker" / "PY32F071xB_FLASH.ld"

_PAGE_RE = re.compile(r"^\|\s*Page size\s*\|\s*(\d+)\s*bytes\s*\|", re.MULTILINE)
_SECTOR_RE = re.compile(r"^\|\s*Sector size\s*\|\s*(\d+)\s*bytes\s*\|", re.MULTILINE)
_MAIN_RE = re.compile(
    r"^\|\s*Main flash\s*\|\s*`(0x[0-9A-Fa-f]+)`\s*to\s*`(0x[0-9A-Fa-f]+)`", re.MULTILINE
)
# One flash-map row: | `NAME ...` | `origin` or empty | `length` or "256 B" or empty |
_MAP_ROW_RE = re.compile(
    r"^\|\s*`(\w+)(?:\s*\([a-z]+\))?`\s*\|\s*(?:`(0x[0-9A-Fa-f]+)`)?\s*\|\s*"
    r"(?:`(\d+)([KkMm]?)`|(\d+)\s*B)?\s*\|",
    re.MULTILINE,
)

_REGION_RE = re.compile(
    r"^\s*(\w+)\s*\([A-Za-z]+\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*,"
    r"\s*LENGTH\s*=\s*(\d+)\s*([KkMm]?)\s*$",
    re.MULTILINE,
)
_PROVIDE_RE = re.compile(r"PROVIDE\(\s*(__\w+)\s*=\s*(.*?)\)\s*;")

_MAP_REGIONS = ("BOOTLOADER", "FLASH", "CONFIG", "RAM")
_MAP_SYMBOLS = (
    "__config_page_size",
    "__config_slot_a_start",
    "__config_slot_b_start",
    "__config_region_end",
)


def _section(text, heading):
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.rstrip() == heading:
            end = next(
                (j for j in range(i + 1, len(lines)) if lines[j].startswith("## ")),
                len(lines),
            )
            return "\n".join(lines[i + 1 : end])
    return None


def _size(num, suffix):
    n = int(num)
    if suffix and suffix.lower() == "k":
        n *= 1024
    elif suffix and suffix.lower() == "m":
        n *= 1024 * 1024
    return n


def _parse_geometry(design_text):
    body = _section(design_text, "## Flash geometry") or ""
    page = _PAGE_RE.search(body)
    sector = _SECTOR_RE.search(body)
    main = _MAIN_RE.search(body)
    return {
        "page": int(page.group(1)) if page else None,
        "sector": int(sector.group(1)) if sector else None,
        "flash_lo": int(main.group(1), 16) if main else None,
        "flash_hi": int(main.group(2), 16) if main else None,
    }


def _parse_design_map(design_text):
    """Return name -> (origin or None, length or None) from the flash-map table."""
    body = _section(design_text, "## Flash map") or ""
    rows = {}
    for name, origin, length, suffix, byte_len in _MAP_ROW_RE.findall(body):
        o = int(origin, 16) if origin else None
        if length:
            ln = _size(length, suffix)
        elif byte_len:
            ln = int(byte_len)
        else:
            ln = None
        rows[name] = (o, ln)
    return rows


def _parse_linker(linker_text):
    m = re.search(r"MEMORY\s*\{(.*?)\n\}", linker_text, re.DOTALL)
    regions = {}
    if m:
        for name, origin, length, suffix in _REGION_RE.findall(m.group(1)):
            regions[name] = (int(origin, 0), _size(length, suffix))
    symbols = {}
    for name, expr in _PROVIDE_RE.findall(linker_text):
        total = 0
        ok = True
        for term in expr.split("+"):
            term = term.strip()
            mo = re.match(r"^(ORIGIN|LENGTH)\((\w+)\)$", term)
            if mo:
                if mo.group(2) not in regions:
                    ok = False
                    break
                total += regions[mo.group(2)][0 if mo.group(1) == "ORIGIN" else 1]
            else:
                total += int(term, 0)
        if ok:
            symbols[name] = total
    return regions, symbols


def _violations(design_text, linker_text):
    """Every check in this module. Returns a list of messages; empty means
    the record and the linker script agree."""
    v = []
    geo = _parse_geometry(design_text)
    missing = [k for k, val in geo.items() if val is None]
    if missing:
        return [f"geometry: DESIGN.md does not state {missing!r} in '## Flash geometry'"]
    page, sector, lo, hi = geo["page"], geo["sector"], geo["flash_lo"], geo["flash_hi"]
    if page <= 0 or sector % page != 0:
        v.append(f"geometry: sector {sector} is not a whole number of {page}-byte pages")

    regions, symbols = _parse_linker(linker_text)
    for name in ("FLASH", "CONFIG"):
        if name not in regions:
            v.append(f"linker: no {name} region in the MEMORY block")
    if v:
        return v

    cfg_origin, cfg_len = regions["CONFIG"]
    if (cfg_origin - lo) % sector != 0:
        v.append(f"CONFIG origin {cfg_origin:#x} is not on a {sector}-byte sector boundary")
    if cfg_len % sector != 0:
        v.append(f"CONFIG length {cfg_len} is not a whole number of {sector}-byte sectors")
    if cfg_origin < lo or cfg_origin + cfg_len != hi + 1:
        v.append(
            f"CONFIG [{cfg_origin:#x}, {cfg_origin + cfg_len:#x}) does not end at the "
            f"recorded main-flash end {hi + 1:#x}"
        )
    flash_origin, flash_len = regions["FLASH"]
    if flash_origin != lo or flash_origin + flash_len > cfg_origin:
        v.append(
            f"FLASH [{flash_origin:#x}, {flash_origin + flash_len:#x}) does not start at "
            f"{lo:#x} or reaches into CONFIG at {cfg_origin:#x}"
        )
    if symbols.get("__config_page_size") != page:
        v.append(
            f"__config_page_size is {symbols.get('__config_page_size')!r}, the record "
            f"says the page is {page}"
        )
    a = symbols.get("__config_slot_a_start")
    b = symbols.get("__config_slot_b_start")
    if a is None or b is None or b - a != page:
        v.append(f"config slots {a!r} and {b!r} are not exactly one {page}-byte page apart")
    elif not (cfg_origin <= a and b + page <= cfg_origin + cfg_len):
        v.append("config slots are not inside the CONFIG region")

    design_map = _parse_design_map(design_text)
    for name in _MAP_REGIONS:
        if name not in regions:
            v.append(f"linker: no {name} region")
            continue
        if design_map.get(name) != regions[name]:
            v.append(
                f"map: DESIGN.md records {name} as {design_map.get(name)!r}, the linker "
                f"script has {regions[name]!r}"
            )
    expected_symbols = {
        "__config_page_size": (None, symbols.get("__config_page_size")),
        "__config_slot_a_start": (a, page),
        "__config_slot_b_start": (b, page),
        "__config_region_end": (symbols.get("__config_region_end"), None),
    }
    for name in _MAP_SYMBOLS:
        if design_map.get(name) != expected_symbols[name]:
            v.append(
                f"map: DESIGN.md records {name} as {design_map.get(name)!r}, the linker "
                f"script gives {expected_symbols[name]!r}"
            )
    return v


def _real():
    assert _DESIGN.exists(), f"{_DESIGN} does not exist. This must FAIL, never skip."
    assert _LINKER.exists(), f"{_LINKER} does not exist. This must FAIL, never skip."
    return _DESIGN.read_text(), _LINKER.read_text()


def test_geometry_record_is_parsed():
    design, _ = _real()
    geo = _parse_geometry(design)
    assert geo == {
        "page": 256,
        "sector": 8192,
        "flash_lo": 0x08000000,
        "flash_hi": 0x0801FFFF,
    }, geo


def test_flash_map_table_is_parsed():
    design, _ = _real()
    rows = _parse_design_map(design)
    assert set(_MAP_REGIONS + _MAP_SYMBOLS) <= set(rows), rows


def test_record_and_linker_agree():
    design, linker = _real()
    assert _violations(design, linker) == [], _violations(design, linker)


# --- violating inputs: each check can fail --------------------------------


def _mutate(text, old, new):
    assert old in text, f"planted-mutation target {old!r} not found"
    return text.replace(old, new, 1)


@pytest.mark.parametrize(
    "target, old, new, expect",
    [
        ("linker", "ORIGIN = 0x0801E000, LENGTH = 8K", "ORIGIN = 0x0801FE00, LENGTH = 8K",
         "sector boundary"),
        ("linker", "ORIGIN = 0x0801E000, LENGTH = 8K", "ORIGIN = 0x0801C000, LENGTH = 8K",
         "does not end at"),
        ("linker", "LENGTH = 120K", "LENGTH = 128K", "reaches into CONFIG"),
        ("linker", "__config_page_size    = 256", "__config_page_size    = 128",
         "__config_page_size"),
        ("linker", "ORIGIN(CONFIG) + 256);", "ORIGIN(CONFIG) + 512);", "one 256-byte page"),
        ("design", "| Page size | 256 bytes |", "| Page size | 128 bytes |", "__config_page_size"),
        ("design", "| Sector size | 8192 bytes |", "| Sector size | 16384 bytes |",
         "sector boundary"),
        ("design", "| `FLASH (rx)` | `0x08000000` | `120K` |",
         "| `FLASH (rx)` | `0x08000000` | `128K` |", "map: DESIGN.md records FLASH"),
        ("design", "| `__config_slot_b_start` | `0x0801E100` |",
         "| `__config_slot_b_start` | `0x0801E200` |", "__config_slot_b_start"),
        ("design", "| Main flash |", "| Main memory |", "does not state"),
    ],
)
def test_planted_violation_is_reported(target, old, new, expect):
    design, linker = _real()
    if target == "linker":
        linker = _mutate(linker, old, new)
    else:
        design = _mutate(design, old, new)
    violations = _violations(design, linker)
    assert any(expect in x for x in violations), violations


def test_sector_not_a_whole_number_of_pages_is_reported():
    design, linker = _real()
    design = _mutate(design, "| Sector size | 8192 bytes |", "| Sector size | 8000 bytes |")
    assert any("whole number of" in x for x in _violations(design, linker))


def test_missing_config_region_is_reported():
    design, linker = _real()
    linker = re.sub(r"^\s*CONFIG \(r\).*$", "", linker, count=1, flags=re.MULTILINE)
    assert any("no CONFIG region" in x for x in _violations(design, linker))


def test_no_skip_in_this_module():
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    assert skip_call not in own_text
    assert skipif_marker not in own_text
