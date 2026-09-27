"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Content gate over the PY32F071 design record, `platform/py32f071/DESIGN.md`,
its README and the linker-script comment that points at it.

The record used to exist as two copies (an authoritative record and a subset)
and this module compared them. The two copies and their seed note were merged
into the one `DESIGN.md`, so no parity comparison is left to make. What this
module still protects is the content those copies carried:

- the three-tier flash path, and that the DFU path does not retire the
  self-flash bootloader;
- the seven PCB checklist rows, each with one Why line and one
  Breaks-if-omitted line, plus the "Deliberately undecided" list;
- the bootloader budget, and that its reservation figure always carries its
  migration cost;
- the USB identity decision and the exact ship-gate sentence;
- the socket-empty instruction, in the record and in the README;
- the linker comment's pointer to the record, and the absence of the false
  "no VTOR" claim.

The flash map and geometry values are checked against the linker script by
`tests/test_flash_geometry_record_matches_linker.py`, not here.

**Single-helper rule.** Every positive test and every planted-violation test
goes through `_design_violations`, `_readme_violations` and
`_linker_violations`. A planted violation therefore exercises the same code
that the positive tests depend on.

The module name is kept because agent-os
`standards/testing/standalone-checkout.md` cites
`test_no_test_reads_outside_this_repo` by this path.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
_FW_REPO_ROOT = _HERE.parent
_PY32_DIR = _FW_REPO_ROOT / "platform" / "py32f071"
_DESIGN = _PY32_DIR / "DESIGN.md"
_README = _PY32_DIR / "README.md"
_LINKER = _PY32_DIR / "linker" / "PY32F071xB_FLASH.ld"

# Exact `## ` heading lines of the sections this module checks. A renamed
# heading makes the extractor return None, which is a violation, never a
# silent pass.
_HEADINGS = {
    "flash_path": "## Flash path",
    "bootloader": "## Bootloader budget",
    "usb": "## USB identity",
    "pcb": "## PCB checklist",
    "socket": "## Socket empty before a firmware install",
}

# Exact literals. Each assertion is a plain substring test, so the record may
# wrap a sentence in `**` bold markers. U+2014 EM DASH where an em dash
# appears.
_L1_NON_RETIREMENT = (
    "The factory USB DFU path does not retire the self-flash bootloader."
)

_L2_SHIP_GATE = (
    "Ship gate: no PY32F071 board ships, and no release advertises a USB "
    "identity, until a PID allocated under VID 0x1209 exists."
)

_L3_SOCKET_EMPTY = (
    "Before any PY32F071 firmware install — DFU, SWD or otherwise — "
    "the PROM socket must be empty."
)

_README_NO_PCB = "No PCB exists."

_NEEDLES = {
    "flash_path": (
        "self-flash bootloader",
        "CDC",
        "COBS",
        "factory USB DFU",
        "SWD",
        "intended primary",
        "maintainer/manufacturing recovery",
        "last resort",
    ),
    "bootloader": (
        "12,032",
        "14.6 KiB",
        "24 KiB",
        "3 sectors",
        "LENGTH = 0",
    ),
    "usb": (
        "0x1209",
        "1209:0001",
        "pid.codes",
        "0x36B7",
        "0xFFFF",
        "Puya Semiconductor",
        "usbd_cdc_if.c",
        "pycdc.inf",
        "0ed2f4b4d3391eccfd4491006a30295fd78e32c2",
        "0x0448",
        "py32_dfu.py",
        "0xFE/0x01",
    ),
    "pcb": (
        "PF8",
        "nBOOT1",
        "PA13",
        "PA14",
        "nRST",
        "PB0",
        "PB7",
        "LQFP64",
        "CSP64",
        "QFN64",
        "LQFP48",
        "QFN48",
        "QFN56",
        "QFN32",
        "HSE",
        "PA4",
        "ADC",
        "PA11",
        "PA12",
        "1.5 kΩ",
    ),
    "socket": (
        "provisional",
        "RURP_PY32F071_PINMAP_PROVISIONAL",
        "direction",
        "BOOT0",
        "three board revisions",
    ),
}

# Compared case-insensitively inside the "Deliberately undecided" span.
_UNDECIDED_MARKER = "### Deliberately undecided"
_UNDECIDED_NEEDLES = ("socket", "zif", "connector", "power budget")

# The bootloader reservation figure, in either written form.
_FIGURE_RE = re.compile(r"24\s*KiB|3\s+(?:whole\s+)?sectors", re.IGNORECASE)
_COST_TOKENS = ("ORIGIN", "migration", "re-flash")

_LINKER_NEEDLES = (
    "DESIGN.md",
    "__VTOR_PRESENT",
    "SCB->VTOR",
    "BOOTLOADER (rx) : ORIGIN = 0x08000000, LENGTH = 0",
)
_LINKER_FORBIDDEN_RE = re.compile(r"no\s+VTOR", re.IGNORECASE)

# A checklist row header: "- [ ] **R<digit> — <title>". The Why and
# Breaks-if-omitted lines must begin after exactly two leading spaces.
_ROW_HEADER_RE = re.compile(r"^- \[ \] \*\*R(\d+) — (.+)$")
_ROW_WHY_RE = re.compile(r"^  - \*Why:\*(.{20,})")
_ROW_BREAKS_RE = re.compile(r"^  - \*Breaks if omitted:\*(.{20,})")


def _extract_section(text, heading):
    """Return the body between the unique line equal to `heading` and the
    next `## ` line, with trailing blank lines stripped.

    Returns None when no line equals `heading`. Raises AssertionError with
    'refusing to guess' when two or more lines equal it."""
    lines = text.splitlines()
    starts = [i for i, line in enumerate(lines) if line.rstrip() == heading]
    if len(starts) > 1:
        raise AssertionError(
            f"{len(starts)} lines equal {heading!r} -- refusing to guess which "
            "section is the real one."
        )
    if not starts:
        return None
    start = starts[0]
    end = len(lines)
    for j in range(start + 1, len(lines)):
        if lines[j].startswith("## "):
            end = j
            break
    body = lines[start + 1 : end]
    while body and body[-1].strip() == "":
        body.pop()
    return "\n".join(body)


def _assert_non_vacuous(value, source):
    """A parse that found nothing must fail, never pass: an empty value
    would make every later substring check vacuously true."""
    assert value is not None and value.strip(), (
        f"parsed value {value!r} from {source} -- a parse that found nothing "
        "would make every downstream comparison vacuously true."
    )


def _checklist_rows(body):
    """Return (row_id, title, why_line, breaks_line) for each checklist row
    in `body`. Raises AssertionError naming the row when a row is not
    followed by its Why line and its Breaks-if-omitted line."""
    lines = body.splitlines()
    rows = []
    for i, line in enumerate(lines):
        m = _ROW_HEADER_RE.match(line)
        if not m:
            continue
        row_id = f"R{m.group(1)}"
        title = m.group(2)
        following = [ln for ln in lines[i + 1 :] if ln.strip() != ""]
        if len(following) < 2:
            raise AssertionError(
                f"row {row_id} ({title!r}) is not followed by both its Why "
                "and Breaks-if-omitted lines."
            )
        why_line, breaks_line = following[0], following[1]
        if not _ROW_WHY_RE.match(why_line):
            raise AssertionError(
                f"row {row_id} ({title!r})'s Why line does not match the "
                f"required '  - *Why:*<20+ chars>' shape. Got: {why_line!r}"
            )
        if not _ROW_BREAKS_RE.match(breaks_line):
            raise AssertionError(
                f"row {row_id} ({title!r})'s Breaks-if-omitted line does not "
                "match the required '  - *Breaks if omitted:*<20+ chars>' "
                f"shape. Got: {breaks_line!r}"
            )
        rows.append((row_id, title, why_line, breaks_line))
    return rows


def _design_violations(text):
    """Every content check on the design record. Returns a list of
    'key: message' strings. An empty list means the record passes."""
    v = []
    sections = {key: _extract_section(text, h) for key, h in _HEADINGS.items()}
    for key, body in sections.items():
        if body is None or not body.strip():
            v.append(f"{key}: section {_HEADINGS[key]!r} is missing or empty")
    for key, needles in _NEEDLES.items():
        body = sections[key] or ""
        missing = [n for n in needles if n not in body]
        if missing:
            v.append(f"{key}: missing needles {missing!r}")

    if _L1_NON_RETIREMENT not in (sections["flash_path"] or ""):
        v.append(f"flash_path: missing the sentence {_L1_NON_RETIREMENT!r}")
    if _L2_SHIP_GATE not in (sections["usb"] or ""):
        v.append(f"usb: missing the ship-gate sentence {_L2_SHIP_GATE!r}")
    if _L3_SOCKET_EMPTY not in (sections["socket"] or ""):
        v.append(f"socket: missing the sentence {_L3_SOCKET_EMPTY!r}")

    pcb = sections["pcb"] or ""
    try:
        row_ids = [r[0] for r in _checklist_rows(pcb)]
    except AssertionError as exc:
        v.append(f"pcb: {exc}")
    else:
        expected = [f"R{n}" for n in range(1, 8)]
        if row_ids != expected:
            v.append(f"pcb: expected rows {expected!r} in order, got {row_ids!r}")
    if _UNDECIDED_MARKER not in pcb:
        v.append(f"pcb: no {_UNDECIDED_MARKER!r} subsection")
    else:
        span = pcb[pcb.index(_UNDECIDED_MARKER) :].lower()
        missing = [n for n in _UNDECIDED_NEEDLES if n not in span]
        if missing:
            v.append(f"pcb: undecided subsection is missing {missing!r}")

    lines = (sections["bootloader"] or "").splitlines()
    figure_idxs = [i for i, line in enumerate(lines) if _FIGURE_RE.search(line)]
    if not figure_idxs:
        v.append("bootloader: no line carries the reservation figure")
    for idx in figure_idxs:
        window = lines[max(0, idx - 2) : idx + 3]
        if not any(tok in w for w in window for tok in _COST_TOKENS):
            v.append(
                f"bootloader: line {idx} ({lines[idx]!r}) carries the "
                f"reservation figure with no cost token {_COST_TOKENS!r} "
                "within two lines"
            )
    return v


def _readme_violations(text):
    v = []
    if _L3_SOCKET_EMPTY not in text:
        v.append(f"readme: missing the sentence {_L3_SOCKET_EMPTY!r}")
    if "DESIGN.md" not in text:
        v.append("readme: does not point to DESIGN.md")
    if _README_NO_PCB not in text:
        v.append(f"readme: does not state {_README_NO_PCB!r}")
    return v


def _linker_violations(text):
    v = []
    missing = [n for n in _LINKER_NEEDLES if n not in text]
    if missing:
        v.append(f"linker: missing needles {missing!r}")
    m = _LINKER_FORBIDDEN_RE.search(text)
    if m:
        v.append(
            f"linker: contains the false {m.group(0)!r} clause -- the part "
            "declares __VTOR_PRESENT and SystemInit writes SCB->VTOR"
        )
    return v


def _read(path: Path) -> str:
    assert path.exists(), f"{path} does not exist. This must FAIL, never skip."
    return path.read_text()


def _git_bin() -> str:
    git_bin = shutil.which("git")
    assert git_bin is not None, (
        "`git` binary not found on PATH. This must FAIL the suite, never be "
        "silently skipped."
    )
    return git_bin


def _git_hash_object(path: Path) -> str:
    result = subprocess.run(
        [_git_bin(), "hash-object", str(path)],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def _git_porcelain(path: Path) -> str:
    result = subprocess.run(
        [_git_bin(), "-C", str(path), "status", "--porcelain"],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout


def _synthetic_record(bodies):
    lines = ["# Synthetic design record (test fixture)", ""]
    for key, heading in _HEADINGS.items():
        lines.append(heading)
        lines.append(bodies[key])
        lines.append("")
    return "\n".join(lines)


# --- the real record ---------------------------------------------------


class TestDesignRecord:
    @pytest.mark.parametrize("key", list(_HEADINGS))
    def test_section_is_present_and_non_vacuous(self, key):
        body = _extract_section(_read(_DESIGN), _HEADINGS[key])
        _assert_non_vacuous(body, f"DESIGN.md, {_HEADINGS[key]!r}")

    @pytest.mark.parametrize("key", list(_HEADINGS))
    def test_section_content(self, key):
        violations = [
            x for x in _design_violations(_read(_DESIGN)) if x.startswith(key + ":")
        ]
        assert not violations, violations

    def test_record_has_no_violations(self):
        violations = _design_violations(_read(_DESIGN))
        assert violations == [], violations

    def test_readme_carries_the_socket_instruction_and_the_pointer(self):
        violations = _readme_violations(_read(_README))
        assert violations == [], violations

    def test_linker_comment_points_at_the_record(self):
        text = _read(_LINKER)
        assert _linker_violations(text) == [], _linker_violations(text)
        # The BOOTLOADER comment block itself must be non-empty: from the
        # MEMORY opening brace to the BOOTLOADER (rx) line.
        lines = text.splitlines()
        memory_idx = brace_idx = bootloader_idx = None
        for i, line in enumerate(lines):
            stripped = line.strip()
            if memory_idx is None and stripped == "MEMORY":
                memory_idx = i
                continue
            if memory_idx is not None and brace_idx is None and stripped == "{":
                brace_idx = i
            if stripped.startswith("BOOTLOADER (rx)"):
                bootloader_idx = i
                break
        assert brace_idx is not None and bootloader_idx is not None, (
            "could not locate the MEMORY opening brace or the BOOTLOADER (rx) line"
        )
        _assert_non_vacuous(
            "\n".join(lines[brace_idx + 1 : bootloader_idx]),
            "linker script MEMORY-to-BOOTLOADER comment span",
        )

    def test_planted_mutation_of_the_real_record_is_detected(
        self, tmp_path, monkeypatch
    ):
        """Mutate a tmp_path copy of the real record and prove the helper
        reports it. The real file and the working tree must stay unchanged."""
        real_path = _DESIGN
        before_blob = _git_hash_object(real_path)
        real_text = real_path.read_text()
        mutated = real_text.replace(_L2_SHIP_GATE, "Ship gate: none.", 1)
        assert mutated != real_text, "the ship-gate sentence was not found"

        planted = tmp_path / "planted-DESIGN.md"
        planted.write_text(mutated)
        monkeypatch.setattr(sys.modules[__name__], "_DESIGN", planted)
        violations = _design_violations(_read(_DESIGN))
        assert any("ship-gate" in x for x in violations), violations

        assert _git_hash_object(real_path) == before_blob, (
            "the planted mutation touched the REAL DESIGN.md"
        )
        assert _git_porcelain(_FW_REPO_ROOT) == "", (
            "the firmware working tree is not clean after the planted test"
        )


# --- synthetic inputs: each check can fail ------------------------------


class TestChecksFailClosed:
    def _good_bodies(self):
        rows = []
        for n in range(1, 8):
            rows.append(f"- [ ] **R{n} — Row {n} title.**")
            rows.append("  - *Why:* a rationale line that is long enough.")
            rows.append("  - *Breaks if omitted:* a consequence line, long enough.")
            rows.append("")
        pcb = (
            "\n".join(rows)
            + "\n" + " ".join(_NEEDLES["pcb"]) + "\n\n"
            + _UNDECIDED_MARKER + "\n\n- ZIF socket, connector and power budget.\n"
        )
        return {
            "flash_path": " ".join(_NEEDLES["flash_path"]) + "\n" + _L1_NON_RETIREMENT,
            "bootloader": " ".join(_NEEDLES["bootloader"]) + " moves the ORIGIN.",
            "usb": " ".join(_NEEDLES["usb"]) + "\n" + _L2_SHIP_GATE,
            "pcb": pcb,
            "socket": " ".join(_NEEDLES["socket"]) + "\n" + _L3_SOCKET_EMPTY,
        }

    def test_good_synthetic_record_passes(self):
        """The boundary: a record with every item passes, so the failures
        below come from the planted change and not from the fixture."""
        assert _design_violations(_synthetic_record(self._good_bodies())) == []

    def test_empty_extraction_is_not_a_vacuous_pass(self):
        text = "# Title\n\nNo sections here.\n"
        for heading in _HEADINGS.values():
            assert _extract_section(text, heading) is None
        with pytest.raises(AssertionError, match="vacuously true"):
            _assert_non_vacuous(None, "marker-free document")
        with pytest.raises(AssertionError, match="vacuously true"):
            _assert_non_vacuous("   \n", "whitespace-only body")
        assert len(_design_violations(text)) >= len(_HEADINGS)

    def test_renamed_heading_is_a_violation(self):
        text = _synthetic_record(self._good_bodies()).replace(
            "## USB identity", "## USB identity (renamed)"
        )
        violations = _design_violations(text)
        assert any(x.startswith("usb: section") for x in violations), violations

    def test_duplicate_heading_refuses_to_guess(self):
        text = "# T\n\n## PCB checklist\nA.\n\n## PCB checklist\nB.\n"
        with pytest.raises(AssertionError, match="refusing to guess"):
            _extract_section(text, "## PCB checklist")

    def test_missing_ship_gate_is_detected(self):
        bodies = self._good_bodies()
        bodies["usb"] = bodies["usb"].replace(_L2_SHIP_GATE, "")
        violations = _design_violations(_synthetic_record(bodies))
        assert any("ship-gate" in x for x in violations), violations

    def test_figure_without_cost_is_detected(self):
        bodies = self._good_bodies()
        bodies["bootloader"] = " ".join(_NEEDLES["bootloader"])
        violations = _design_violations(_synthetic_record(bodies))
        assert any("no cost token" in x for x in violations), violations

    def test_missing_figure_is_detected(self):
        bodies = self._good_bodies()
        bodies["bootloader"] = "No reservation stated. ORIGIN."
        violations = _design_violations(_synthetic_record(bodies))
        assert any("no line carries the reservation figure" in x for x in violations)

    def test_malformed_checklist_row_is_detected(self):
        bodies = self._good_bodies()
        bodies["pcb"] = bodies["pcb"].replace(
            "- [ ] **R4 — Row 4 title.**\n  - *Why:* a rationale line that is long enough.",
            "- [ ] **R4 — Row 4 title.**\n  - *Reason:* a rationale line that is long enough.",
        )
        violations = _design_violations(_synthetic_record(bodies))
        assert any("R4" in x and "Why line" in x for x in violations), violations

    def test_missing_checklist_row_is_detected(self):
        bodies = self._good_bodies()
        bodies["pcb"] = bodies["pcb"].replace("**R7 — Row 7 title.**", "**Row 7 title.**")
        violations = _design_violations(_synthetic_record(bodies))
        assert any("expected rows" in x for x in violations), violations

    def test_missing_undecided_item_is_detected(self):
        bodies = self._good_bodies()
        bodies["pcb"] = bodies["pcb"].replace(" and power budget", "")
        violations = _design_violations(_synthetic_record(bodies))
        assert any("undecided subsection is missing" in x for x in violations)

    def test_readme_without_socket_instruction_is_detected(self):
        text = "No PCB exists. See DESIGN.md."
        assert any("readme: missing" in x for x in _readme_violations(text))
        text_ok = _L3_SOCKET_EMPTY + " " + text
        assert _readme_violations(text_ok) == []
        assert any(
            "No PCB" in x for x in _readme_violations(_L3_SOCKET_EMPTY + " DESIGN.md")
        )

    def test_linker_with_false_vtor_clause_is_detected(self):
        good = " ".join(_LINKER_NEEDLES)
        assert _linker_violations(good) == []
        assert any("false" in x for x in _linker_violations(good + " on a part with no VTOR"))
        assert any(
            "missing" in x for x in _linker_violations(good.replace("DESIGN.md", ""))
        )


# --- repository hygiene --------------------------------------------------


class TestRepositoryRules:
    def test_dirty_tree_is_detected(self, tmp_path):
        git_bin = _git_bin()
        repo = tmp_path / "throwaway-repo"
        repo.mkdir()
        subprocess.run([git_bin, "init"], cwd=str(repo), capture_output=True, check=True)
        subprocess.run(
            [git_bin, "-c", "user.email=test@example.com", "-c", "user.name=Test",
             "commit", "--allow-empty", "-m", "empty"],
            cwd=str(repo), capture_output=True, check=True,
        )
        (repo / "untracked.txt").write_text("untracked content\n")
        assert _git_porcelain(repo), "an untracked file must show in porcelain"
        assert isinstance(_git_porcelain(_FW_REPO_ROOT), str)

    def test_git_binary_is_required_not_optional(self):
        assert shutil.which("git") is not None
        own_source = Path(__file__).read_text()
        which_git = 'shutil.which("git")'
        idx = own_source.find(which_git)
        assert idx != -1
        assert "assert" in own_source[idx : idx + 200], (
            f"an `assert` must follow {which_git} so a missing binary fails"
        )

    def test_no_test_reads_outside_this_repo(self):
        """Enforces agent-os `standards/testing/standalone-checkout.md`: a
        test reads only files inside its own repo.

        Parses every `tests/*.py` module with `ast` and refuses an import of
        the deleted `meta_presence` helper, a read of any `FIRESTARTER_META*`
        environment key, and a retired-planning-tree path in any string
        literal that is not a docstring."""
        import ast

        # Built from character codes so this checker's own source does not
        # trip it.
        env_prefix = "".join(chr(c) for c in (
            70, 73, 82, 69, 83, 84, 65, 82, 84, 69, 82, 95, 77, 69, 84, 65))
        retired_tree = "".join(chr(c) for c in (
            46, 112, 108, 97, 110, 110, 105, 110, 103))

        violations = []
        for module in sorted(_HERE.glob("*.py")):
            tree = ast.parse(module.read_text(), filename=str(module))
            docstrings = set()
            for node in ast.walk(tree):
                if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef,
                                     ast.AsyncFunctionDef)):
                    doc = ast.get_docstring(node, clean=False)
                    if doc is not None:
                        docstrings.add(doc)
            for node in ast.walk(tree):
                if isinstance(node, ast.ImportFrom) and node.module:
                    if "meta_presence" in node.module:
                        violations.append(f"{module.name}:{node.lineno} imports {node.module}")
                elif isinstance(node, ast.Import):
                    for alias in node.names:
                        if "meta_presence" in alias.name:
                            violations.append(f"{module.name}:{node.lineno} imports {alias.name}")
                elif isinstance(node, ast.Constant) and isinstance(node.value, str):
                    if node.value in docstrings:
                        continue
                    if node.value.startswith(env_prefix):
                        violations.append(
                            f"{module.name}:{node.lineno} names the environment key "
                            f"{node.value!r}"
                        )
                    if retired_tree in node.value:
                        violations.append(
                            f"{module.name}:{node.lineno} contains a retired-planning-"
                            f"tree path in a string literal: {node.value!r}"
                        )
        assert not violations, (
            "test modules must read only files inside this repository. "
            "Violations:\n  " + "\n  ".join(violations)
        )
