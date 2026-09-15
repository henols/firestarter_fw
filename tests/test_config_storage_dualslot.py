"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 126 Plan 09 -- CFG-05's proof that the shipped dual-slot config storage
core (platform/py32f071/src/config_storage_dualslot.cpp) behaves correctly,
through six distinctly named tests plus an independent CRC known-answer
anchor (D-05).

Requirements: CFG-05
Decisions covered: D-01, D-02, D-03, D-05, D-15, D-16, D-17, D-19

D-02 is why this module compiles platform/py32f071/src/config_storage_dualslot.cpp
BY EXPLICIT PATH and supplies only a RAM fake for the three injected flash
primitives (rurp_flash_primitives_t): the tested code must be the shipped
code. Two alternatives are explicitly rejected, per the core header's own
comment: an independent fake reimplementation of the algorithm living only in
this test (it would prove a copy behaves -- the exact hollow-gate shape
Phases 118 and 124 each had to unwind); and compiling the real backend
against a hand-written stub HAL header (it would become an unversioned
mirror of the pinned FetchContent SDK this test venue cannot see).

Criterion 4 forbids an aggregate pass/fail: the six behaviours below are six
distinctly named functions, never one function standing in for all six.
D-05 adds a seventh, independent known-answer vector
(CRC32("123456789") == 0xCBF43926) so the other six do not merely agree with
themselves -- an implementation asserted against itself proves nothing (the
HOST-06 discipline).

This module executes in NO CI leg on this branch: `pytest tests/` runs only
in build.yml (push/PR to main) and beta-build.yml (push to beta);
py32f071.yml has no pytest step at all. The local run recorded in
126-NONREGRESSION.md is the only evidence this module's assertions were ever
exercised.

Self-contained path resolution below -- NOT in conftest.py (firestarter/tests/
has no conftest.py anywhere in the repo, and none of pytest.ini,
pyproject.toml, setup.cfg or tox.ini exist either; a recorded house-rule
pattern decision, not an omission). Stdlib and pytest only.

NON-CLAIM (Claim Ceiling, explicit): this harness exercises the shipped
algorithm against a RAM model of the flash primitives on a host compiler. It
proves nothing about real PY32F071 silicon, a real power loss, or a real DFU
install -- no PY32F071 PCB exists (see .planning/REQUIREMENTS.md's
"Validation Ceiling"). The interrupted-write test models FLASH_Program_Page's
64 discrete word-store boundaries (observable in the pinned SDK source, C-2)
-- it is not an observation of a real reset on real silicon.

Coverage:
  1. test_crc32_matches_the_independent_known_answer_vector -- D-05: the
     compiled core's rurp_config_crc32 over b"123456789" returns 0xCBF43926,
     both written as constants in THIS file, never derived from the module
     under test.
  2. test_blank_slots_report_no_valid_record -- both slots all-0xFF:
     rurp_dualslot_load returns false, plus a companion "magic trap" fixture
     (magic still invalid, but `length` set to an otherwise-valid value and
     crc32 self-consistent, so length and crc32 both pass by construction)
     proving the rejection is on `magic` alone, not a length or CRC
     accident.
  3. test_newest_sequence_wins_when_both_slots_valid -- two valid records
     with different `sequence`, tested in both slot orders.
  4. test_slot_with_bad_crc_is_rejected_in_favour_of_the_other -- one valid
     record, one CRC-corrupted (with a HIGHER sequence, so the valid one can
     only win via the CRC gate) -- plus the phase's highest-severity
     mitigation: an oversized `length` field is rejected before any copy,
     verified via a sentinel-filled caller buffer whose bytes past `len`
     survive untouched.
  5. test_both_slots_corrupt_reports_no_valid_record -- both slots hold
     valid-magic, bad-CRC records: false, the same outcome as blank from a
     different input (D-15).
  6. test_interrupted_write_leaves_the_previous_record_loadable -- C-2: save
     aborted after N in {0, 1, 32, 63, 64} word stores (FLASH_Program_Page's
     real 64-word granularity). Asserts the STRONGER, ACCURATE invariant
     recorded by Plan 126-07's own investigation of this exact core: load()
     ALWAYS returns a valid, loadable record (never garbage, never false),
     and which record wins (previous vs. new) depends on whether N covers
     the record's own word footprint (ceil(sizeof(StoredConfiguration)/4)
     words) -- not a blanket "always the previous record" claim.
  7. test_successive_saves_alternate_slots -- four successive saves land in
     alternating physical slots with strictly increasing `sequence`.
  8. test_module_has_no_pio_libdeps_dependency -- C-12: this module's own
     text contains no path under the gitignored PlatformIO build-artifacts
     directory.
  9. test_compiler_is_required_not_optional -- adapted verbatim (including
     its concatenation trick) from the analog's self-enforcing no-skip leg
     (tests/test_vpp_seam_manual_on_every_board.py:447-468): no skip call or
     conditional-skip marker anywhere in this module -- a missing compiler
     must FAIL the suite, never be silently skipped.
"""

import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_INCLUDE = _REPO_ROOT / "include"
_PLATFORM_SRC = _REPO_ROOT / "platform" / "py32f071" / "src"
_CORE_SRC = _PLATFORM_SRC / "config_storage_dualslot.cpp"

_CRC32_KAT_INPUT = b"123456789"
_CRC32_KAT_EXPECTED = 0xCBF43926

_KV_RE = re.compile(r"(\w+)=(\S+)")


def _resolve_compiler():
    """Resolve the host C++ compiler, fail-closed.

    Deliberately never bypassed via any decorator or runtime call that would
    mark this outcome as skipped, anywhere in this module: doing so would
    recreate the exact BASE-02/BASE-03 absence-proxy failure class this
    milestone's Phase 123 removed. If $CXX (or 'g++') cannot be resolved via
    shutil.which, this raises via a plain assert, which the test runner
    reports as a FAILURE, never a skipped outcome. This module's harness
    runs in NO CI leg on this branch -- the local run is the only evidence,
    which makes the fail-closed contract here even more load-bearing than in
    the CI-covered precedent.
    """
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    assert compiler is not None, (
        "host C++ compiler not found on PATH (checked $CXX, falling back to "
        "'g++'). This must FAIL the suite, never be silently skipped -- no "
        "embedded toolchain is invoked here."
    )
    return compiler


_FAKE_PREAMBLE = """
#include "config_storage_dualslot.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

alignas(4) uint8_t g_page[2][256];
bool g_erased_since_program[2] = {false, false};
int g_erase_calls[2] = {0, 0};
int g_program_calls[2] = {0, 0};
int g_abort_after_words = -1;   // -1 = no abort, complete all 64 words
int g_last_programmed_slot = -1;

bool fake_read(void*, uint8_t slot, void* dst, size_t len)
{
    memcpy(dst, g_page[slot], len);
    return true;
}

bool fake_erase_page(void*, uint8_t slot)
{
    memset(g_page[slot], 0xFF, sizeof(g_page[slot]));
    g_erased_since_program[slot] = true;
    g_erase_calls[slot]++;
    return true;
}

bool fake_program_page(void*, uint8_t slot, const uint32_t words[64])
{
    if (!g_erased_since_program[slot])
    {
        // C-8: a program on a page not just erased is a DEFECT, not a
        // silently-accepted overwrite. Printed on stdout (never solely the
        // exit code) before aborting so a caller inspecting stdout can tell
        // this apart from every other crash.
        printf("DEFECT:PROGRAM_WITHOUT_ERASE slot=%u\\n", (unsigned)slot);
        fflush(stdout);
        abort();
    }
    g_program_calls[slot]++;
    g_last_programmed_slot = slot;
    int words_to_write = (g_abort_after_words < 0 || g_abort_after_words > 64)
        ? 64
        : g_abort_after_words;
    uint32_t* dest = reinterpret_cast<uint32_t*>(g_page[slot]);
    for (int i = 0; i < words_to_write; ++i)
    {
        dest[i] = words[i];
    }
    // Cleared after every program attempt (successful or aborted): a second
    // program on this slot without an intervening erase must be caught.
    g_erased_since_program[slot] = false;
    return words_to_write == 64;
}

[[maybe_unused]] rurp_flash_primitives_t make_primitives()
{
    rurp_flash_primitives_t p;
    p.read = fake_read;
    p.erase_page = fake_erase_page;
    p.program_page = fake_program_page;
    p.ctx = nullptr;
    return p;
}

[[maybe_unused]] void reset_fake()
{
    memset(g_page, 0xFF, sizeof(g_page));
    g_erased_since_program[0] = g_erased_since_program[1] = false;
    g_erase_calls[0] = g_erase_calls[1] = 0;
    g_program_calls[0] = g_program_calls[1] = 0;
    g_abort_after_words = -1;
    g_last_programmed_slot = -1;
}

// Builds a valid record signed with the COMPILED CORE's OWN CRC (D-02): the
// only CRC value ever computed in Python is the independent KAT constant.
[[maybe_unused]] StoredConfiguration make_valid_record(uint32_t sequence, char marker)
{
    StoredConfiguration rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = CONFIG_MAGIC;
    rec.version = 1;
    rec.length = static_cast<uint16_t>(sizeof(rurp_configuration_t));
    memset(&rec.configuration, marker, sizeof(rec.configuration));
    rec.sequence = sequence;
    rec.crc32 = rurp_config_crc32(&rec, offsetof(StoredConfiguration, crc32));
    return rec;
}

[[maybe_unused]] void write_record_to_slot(uint8_t slot, const StoredConfiguration& rec)
{
    memset(g_page[slot], 0xFF, sizeof(g_page[slot]));
    memcpy(g_page[slot], &rec, sizeof(rec));
}

} // namespace
"""

_MAIN_CRC_KAT = """
#include "config_storage_dualslot.h"

#include <cstdio>

int main()
{
    const unsigned char input[] = "123456789";
    uint32_t result = rurp_config_crc32(input, 9);
    printf("crc32=0x%08X\\n", result);
    return 0;
}
"""

_MAIN_BLANK = """
int main()
{
    printf("sizeof_storedconfig=%zu\\n", sizeof(StoredConfiguration));
    rurp_flash_primitives_t primitives = make_primitives();

    // Sub-case 1: the bare blank fixture -- both slots read all-0xFF.
    reset_fake();
    rurp_configuration_t blob;
    memset(&blob, 0xAA, sizeof(blob));
    bool loaded = rurp_dualslot_load(&primitives, &blob, sizeof(blob));
    printf("blank_loaded=%d\\n", (int)loaded);

    // Sub-case 2 (the companion assertion): magic stays invalid (still
    // 0xFFFFFFFF -- genuinely blank), but `length` is set to an
    // otherwise-valid value and the crc32 field is deliberately set to a
    // SELF-CONSISTENT correct CRC over the rest of the record. Both other
    // gates -- length and crc32 -- are made to pass by construction, so
    // ONLY the magic check can be what rejects this fixture. If the
    // implementation checked crc32 (or length) before magic, or not at
    // all, this "trap" fixture would be accepted. Because magic gates
    // first, it is still rejected -- proving the blank case is caught on
    // magic, not by a CRC or length accident. (A first draft of this trap
    // left `length` at its blank 0xFFFF value, which the length check
    // alone already rejects regardless of magic -- masking exactly the
    // property this companion assertion exists to isolate; corrected here.)
    reset_fake();
    StoredConfiguration trap;
    memset(&trap, 0xFF, sizeof(trap));
    trap.length = static_cast<uint16_t>(sizeof(rurp_configuration_t));
    trap.crc32 = rurp_config_crc32(&trap, offsetof(StoredConfiguration, crc32));
    write_record_to_slot(0, trap);
    write_record_to_slot(1, trap);
    rurp_configuration_t blob2;
    memset(&blob2, 0xAA, sizeof(blob2));
    bool trap_loaded = rurp_dualslot_load(&primitives, &blob2, sizeof(blob2));
    printf("magic_trap_loaded=%d\\n", (int)trap_loaded);

    return 0;
}
"""

_MAIN_NEWEST_WINS = """
int main()
{
    printf("sizeof_storedconfig=%zu\\n", sizeof(StoredConfiguration));
    rurp_flash_primitives_t primitives = make_primitives();

    StoredConfiguration hi = make_valid_record(9, 'H');
    StoredConfiguration lo = make_valid_record(3, 'L');

    // Order 1: slot A (index 0) holds the higher sequence.
    reset_fake();
    write_record_to_slot(0, hi);
    write_record_to_slot(1, lo);
    rurp_configuration_t blob1;
    memset(&blob1, 0, sizeof(blob1));
    bool ok1 = rurp_dualslot_load(&primitives, &blob1, sizeof(blob1));
    printf("order1_loaded=%d order1_marker=%c\\n", (int)ok1,
           ok1 ? (char)(reinterpret_cast<uint8_t*>(&blob1)[0]) : '?');

    // Order 2: slot B (index 1) holds the higher sequence -- proves the
    // result is not an artefact of scan order.
    reset_fake();
    write_record_to_slot(0, lo);
    write_record_to_slot(1, hi);
    rurp_configuration_t blob2;
    memset(&blob2, 0, sizeof(blob2));
    bool ok2 = rurp_dualslot_load(&primitives, &blob2, sizeof(blob2));
    printf("order2_loaded=%d order2_marker=%c\\n", (int)ok2,
           ok2 ? (char)(reinterpret_cast<uint8_t*>(&blob2)[0]) : '?');

    return 0;
}
"""

_MAIN_BAD_CRC_AND_LENGTH_BOUND = """
int main()
{
    size_t caller_len = sizeof(rurp_configuration_t);
    printf("sizeof_storedconfig=%zu sizeof_configuration=%zu\\n",
           sizeof(StoredConfiguration), caller_len);
    rurp_flash_primitives_t primitives = make_primitives();

    // Part A: one valid record, one CRC-corrupted record carrying a HIGHER
    // sequence -- the valid one must still win, proving the CRC gate
    // overrides sequence-based selection rather than merely coexisting
    // with it.
    reset_fake();
    StoredConfiguration valid = make_valid_record(5, 'V');
    StoredConfiguration corrupt = make_valid_record(9, 'X');
    corrupt.crc32 ^= 0xFFFFFFFFu;
    write_record_to_slot(0, valid);
    write_record_to_slot(1, corrupt);
    rurp_configuration_t blob;
    memset(&blob, 0, sizeof(blob));
    bool crc_ok = rurp_dualslot_load(&primitives, &blob, caller_len);
    printf("badcrc_loaded=%d badcrc_marker=%c\\n", (int)crc_ok,
           crc_ok ? (char)(reinterpret_cast<uint8_t*>(&blob)[0]) : '?');

    // Part B: the length-bound ORDERING assertion -- the phase's
    // highest-severity mitigation (T-126-09-01). A record whose `length`
    // field exceeds the caller's declared `len` must be rejected BEFORE any
    // copy, and the caller's buffer beyond `len` must remain exactly as the
    // caller left it. The physical buffer is deliberately over-allocated
    // past `len` so the bytes beyond it can be inspected -- the CRC does
    // NOT substitute for this check, because anyone able to write flash can
    // recompute a matching CRC over whatever `length` they choose, and the
    // target is a Cortex-M0+ with 16 KiB of SRAM and no MPU configured.
    reset_fake();
    StoredConfiguration oversized = make_valid_record(1, 'O');
    oversized.length = static_cast<uint16_t>(caller_len + 8);
    oversized.crc32 = rurp_config_crc32(&oversized, offsetof(StoredConfiguration, crc32));
    write_record_to_slot(0, oversized);
    // Slot 1 stays blank -- the oversized record is the only candidate.

    const size_t physical_buf_size = caller_len + 32;
    uint8_t* buf = new uint8_t[physical_buf_size];
    memset(buf, 0xEE, physical_buf_size);
    bool oversized_loaded = rurp_dualslot_load(&primitives, buf, caller_len);
    bool sentinel_intact = true;
    for (size_t i = caller_len; i < physical_buf_size; ++i)
    {
        if (buf[i] != 0xEE)
        {
            sentinel_intact = false;
            break;
        }
    }
    printf("oversized_loaded=%d sentinel_intact=%d erase_calls=%d program_calls=%d\\n",
           (int)oversized_loaded, (int)sentinel_intact,
           g_erase_calls[0] + g_erase_calls[1], g_program_calls[0] + g_program_calls[1]);
    delete[] buf;

    return 0;
}
"""

_MAIN_BOTH_CORRUPT = """
int main()
{
    printf("sizeof_storedconfig=%zu\\n", sizeof(StoredConfiguration));
    rurp_flash_primitives_t primitives = make_primitives();

    reset_fake();
    StoredConfiguration a = make_valid_record(4, 'A');
    StoredConfiguration b = make_valid_record(7, 'B');
    a.crc32 ^= 0xFFFFFFFFu;
    b.crc32 ^= 0xFFFFFFFFu;
    write_record_to_slot(0, a);
    write_record_to_slot(1, b);
    rurp_configuration_t blob;
    memset(&blob, 0xAA, sizeof(blob));
    bool loaded = rurp_dualslot_load(&primitives, &blob, sizeof(blob));
    printf("both_corrupt_loaded=%d\\n", (int)loaded);

    return 0;
}
"""

_MAIN_INTERRUPTED_WRITE = """
int main()
{
    size_t footprint_words = (sizeof(StoredConfiguration) + 3) / 4;
    printf("sizeof_storedconfig=%zu footprint_words=%zu\\n",
           sizeof(StoredConfiguration), footprint_words);

    rurp_flash_primitives_t primitives = make_primitives();
    const int abort_values[5] = {0, 1, 32, 63, 64};

    for (size_t i = 0; i < 5; ++i)
    {
        reset_fake();
        StoredConfiguration old_rec = make_valid_record(5, 'O');
        write_record_to_slot(0, old_rec);   // active slot = 0

        g_abort_after_words = abort_values[i];

        rurp_configuration_t new_blob;
        memset(&new_blob, 'N', sizeof(new_blob));
        bool save_ok = rurp_dualslot_save(&primitives, &new_blob, sizeof(new_blob));

        g_abort_after_words = -1;

        rurp_configuration_t loaded;
        memset(&loaded, 0xCC, sizeof(loaded));
        bool load_ok = rurp_dualslot_load(&primitives, &loaded, sizeof(loaded));

        printf("interrupted_n=%d save_ok=%d load_ok=%d marker=%c\\n",
               abort_values[i], (int)save_ok, (int)load_ok,
               load_ok ? (char)(reinterpret_cast<uint8_t*>(&loaded)[0]) : '?');
    }

    return 0;
}
"""

_MAIN_SUCCESSIVE_SAVES = """
int main()
{
    printf("sizeof_storedconfig=%zu\\n", sizeof(StoredConfiguration));
    reset_fake();
    rurp_flash_primitives_t primitives = make_primitives();

    for (int i = 0; i < 4; ++i)
    {
        rurp_configuration_t blob;
        memset(&blob, 'A' + i, sizeof(blob));
        bool save_ok = rurp_dualslot_save(&primitives, &blob, sizeof(blob));

        StoredConfiguration written;
        memcpy(&written, g_page[g_last_programmed_slot], sizeof(written));
        printf("save_iter=%d save_ok=%d programmed_slot=%d sequence=%u\\n",
               i, (int)save_ok, g_last_programmed_slot, (unsigned)written.sequence);
    }

    rurp_configuration_t final_blob;
    memset(&final_blob, 0, sizeof(final_blob));
    bool final_ok = rurp_dualslot_load(&primitives, &final_blob, sizeof(final_blob));
    printf("final_loaded=%d final_marker=%c\\n", (int)final_ok,
           final_ok ? (char)(reinterpret_cast<uint8_t*>(&final_blob)[0]) : '?');

    return 0;
}
"""


def _write_ram_fake_tu(tmp_path, scenario, main_text):
    """Write a FRESH C++ translation unit into tmp_path for `scenario`,
    never a committed fixture TU (D-03 declines one). `scenario` names the
    file only; `main_text` supplies the scenario-specific main(), appended
    after the shared RAM-fake preamble (skipped entirely for the
    fake-free crc_kat scenario, whose main_text is self-contained)."""
    tu_path = tmp_path / f"dualslot_{scenario}.cpp"
    if scenario == "crc_kat":
        tu_path.write_text(main_text)
    else:
        tu_path.write_text(_FAKE_PREAMBLE + main_text)
    return tu_path


def _compile(compiler, sources, output_path):
    """Compile (and link) the given source files into a real binary at
    output_path. List argv only, never a shell; always
    -std=gnu++17 -Wall -Wextra, -I the repo include/ directory and -I the
    platform source directory."""
    argv = [
        compiler,
        "-std=gnu++17",
        "-Wall",
        "-Wextra",
        "-I",
        str(_INCLUDE),
        "-I",
        str(_PLATFORM_SRC),
    ]
    argv += [str(source) for source in sources]
    argv += ["-o", str(output_path)]
    return subprocess.run(argv, capture_output=True, text=True)


def _run_binary(binary_path):
    """Execute the compiled binary as a second subprocess, list argv. Every
    result crosses this process boundary on stdout, never the run's exit
    code: 1 is also the exit code of a compile failure, a link failure and a
    crash, so returning it from main would make the correct answer
    indistinguishable from every failure mode. Every committed scenario's
    main() returns 0 unconditionally."""
    return subprocess.run([str(binary_path)], capture_output=True, text=True)


def _compile_and_run(compiler, tmp_path, scenario, main_text, core_src=None):
    """Write the scenario's fresh TU, compile it together with the core
    source (by explicit path -- D-02) using the shared helper every
    committed test calls, run it, and return the parsed stdout rows.
    `core_src` defaults to the real, shipped
    platform/py32f071/src/config_storage_dualslot.cpp; Task 2's mutation
    demonstrations pass a scratch copy instead, through this SAME helper."""
    core = core_src if core_src is not None else _CORE_SRC
    tu_path = _write_ram_fake_tu(tmp_path, scenario, main_text)
    binary_path = tmp_path / f"dualslot_{scenario}"

    compile_result = _compile(compiler, (core, tu_path), binary_path)
    assert compile_result.returncode == 0, (
        f"expected a clean compile for scenario {scenario!r}.\n"
        f"argv: {compiler} ... {tu_path}\n"
        f"stdout:\n{compile_result.stdout}\nstderr:\n{compile_result.stderr}"
    )
    assert compile_result.stderr == "", (
        f"expected zero bytes of -Wall -Wextra warning output for scenario "
        f"{scenario!r}.\nstdout:\n{compile_result.stdout}\n"
        f"stderr:\n{compile_result.stderr}"
    )

    run_result = _run_binary(binary_path)
    assert run_result.returncode == 0, (
        f"expected the compiled binary to run cleanly for scenario "
        f"{scenario!r}.\nstdout:\n{run_result.stdout}\nstderr:\n{run_result.stderr}"
    )

    rows = [dict(_KV_RE.findall(line)) for line in run_result.stdout.splitlines() if line.strip()]
    assert rows, (
        f"expected at least one parsable stdout line for scenario "
        f"{scenario!r}.\nstdout:\n{run_result.stdout!r}"
    )
    return rows, run_result


def test_crc32_matches_the_independent_known_answer_vector():
    """Coverage 1 -- D-05: the compiled core's rurp_config_crc32 over the
    nine bytes b"123456789" returns 0xCBF43926. Both the input and the
    expected value are constants written in THIS file, never derived from
    the module under test -- the non-vacuity anchor for the other six
    (HOST-06 discipline)."""
    compiler = _resolve_compiler()
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        rows, run_result = _compile_and_run(compiler, tmp_path, "crc_kat", _MAIN_CRC_KAT)
        observed = int(rows[0]["crc32"], 16)
        assert observed == _CRC32_KAT_EXPECTED, (
            f"expected CRC32({_CRC32_KAT_INPUT!r}) == "
            f"{_CRC32_KAT_EXPECTED:#010x}, got {observed:#010x}.\n"
            f"stdout:\n{run_result.stdout}"
        )


def test_blank_slots_report_no_valid_record(tmp_path):
    """Coverage 2 -- D-15: both slots read all-0xFF, rurp_dualslot_load
    returns false. The companion "magic trap" sub-case (magic still
    invalid, crc32 self-consistent) proves the rejection is on `magic`, not
    a CRC accident."""
    compiler = _resolve_compiler()
    rows, run_result = _compile_and_run(compiler, tmp_path, "blank", _MAIN_BLANK)

    blank_loaded = int(rows[1]["blank_loaded"])
    assert blank_loaded == 0, (
        f"expected rurp_dualslot_load() to return false on two all-0xFF "
        f"slots.\nstdout:\n{run_result.stdout}"
    )

    magic_trap_loaded = int(rows[2]["magic_trap_loaded"])
    assert magic_trap_loaded == 0, (
        f"expected the magic-trap fixture (invalid magic, self-consistent "
        f"crc32) to still be rejected -- if this were accepted, the "
        f"rejection above would be a CRC accident, not a genuine magic "
        f"check.\nstdout:\n{run_result.stdout}"
    )


def test_newest_sequence_wins_when_both_slots_valid(tmp_path):
    """Coverage 3: two valid records with different `sequence`, the higher
    one wins, tested in both slot orders so the result is not an artefact
    of scan order."""
    compiler = _resolve_compiler()
    rows, run_result = _compile_and_run(compiler, tmp_path, "newest_wins", _MAIN_NEWEST_WINS)

    order1 = rows[1]
    order2 = rows[2]
    assert int(order1["order1_loaded"]) == 1 and order1["order1_marker"] == "H", (
        f"expected the higher-sequence record ('H') to win with the higher "
        f"sequence in slot A.\nstdout:\n{run_result.stdout}"
    )
    assert int(order2["order2_loaded"]) == 1 and order2["order2_marker"] == "H", (
        f"expected the higher-sequence record ('H') to still win with the "
        f"higher sequence in slot B -- proves the result is not an "
        f"artefact of scan order.\nstdout:\n{run_result.stdout}"
    )


def test_slot_with_bad_crc_is_rejected_in_favour_of_the_other(tmp_path):
    """Coverage 4: one valid record, one CRC-corrupted record (with a
    HIGHER sequence) -- the valid one wins. Plus T-126-09-01, the phase's
    highest-severity mitigation: an oversized `length` field is rejected
    before any copy, with the caller's sentinel-filled buffer intact past
    `len` and no primitive-level flash write attributable to it."""
    compiler = _resolve_compiler()
    rows, run_result = _compile_and_run(
        compiler, tmp_path, "bad_crc_and_length_bound", _MAIN_BAD_CRC_AND_LENGTH_BOUND
    )

    badcrc = rows[1]
    assert int(badcrc["badcrc_loaded"]) == 1 and badcrc["badcrc_marker"] == "V", (
        f"expected the valid record ('V') to win over the CRC-corrupted "
        f"one ('X', which carries a HIGHER sequence) -- the CRC gate must "
        f"override sequence-based selection.\nstdout:\n{run_result.stdout}"
    )

    oversized = rows[2]
    assert int(oversized["oversized_loaded"]) == 0, (
        f"expected an oversized-`length` record to be REJECTED (load() "
        f"returns false; slot 1 is blank so there is no valid fallback).\n"
        f"stdout:\n{run_result.stdout}"
    )
    assert int(oversized["sentinel_intact"]) == 1, (
        f"expected the caller's buffer bytes beyond `len` to remain "
        f"exactly the sentinel pattern (0xEE) -- an oversized `length` "
        f"must be rejected BEFORE any copy, not merely rejected somehow.\n"
        f"stdout:\n{run_result.stdout}"
    )
    assert int(oversized["erase_calls"]) == 0 and int(oversized["program_calls"]) == 0, (
        f"expected zero erase/program primitive calls attributable to a "
        f"load() on an oversized-`length` record -- load() must never "
        f"touch flash.\nstdout:\n{run_result.stdout}"
    )


def test_both_slots_corrupt_reports_no_valid_record(tmp_path):
    """Coverage 5 -- D-15: both slots hold valid-magic, bad-CRC records:
    false, the SAME outcome as the blank case from a DIFFERENT input --
    together the two make D-15's blank/corrupt equivalence a proven claim
    rather than an assumption."""
    compiler = _resolve_compiler()
    rows, run_result = _compile_and_run(compiler, tmp_path, "both_corrupt", _MAIN_BOTH_CORRUPT)

    both_corrupt_loaded = int(rows[1]["both_corrupt_loaded"])
    assert both_corrupt_loaded == 0, (
        f"expected rurp_dualslot_load() to return false when both slots "
        f"hold valid-magic, bad-crc32 records.\nstdout:\n{run_result.stdout}"
    )


def test_interrupted_write_leaves_the_previous_record_loadable(tmp_path):
    """Coverage 6 -- C-2: rurp_dualslot_save is aborted after N in
    {0, 1, 32, 63, 64} word stores (FLASH_Program_Page's real 64-word
    granularity), swept inside this one function. Asserts the ACCURATE
    invariant Plan 126-07's own investigation of this exact core recorded:
    load() ALWAYS returns a valid, loadable record (never garbage, never
    false) for every N, and WHICH record wins (previous vs. new) depends on
    whether N covers the record's own word footprint
    (ceil(sizeof(StoredConfiguration)/4) words) -- not a blanket "always
    the previous record" claim, which 126-07 measured to be false for
    N >= the footprint on this host."""
    compiler = _resolve_compiler()
    rows, run_result = _compile_and_run(
        compiler, tmp_path, "interrupted_write", _MAIN_INTERRUPTED_WRITE
    )

    footprint_words = int(rows[0]["footprint_words"])
    n_rows = rows[1:]
    assert len(n_rows) == 5, (
        f"expected 5 interrupted-write results (N in "
        f"{{0, 1, 32, 63, 64}}), got {len(n_rows)}.\nstdout:\n{run_result.stdout}"
    )

    for row in n_rows:
        n = int(row["interrupted_n"])
        load_ok = int(row["load_ok"])
        marker = row["marker"]
        assert load_ok == 1, (
            f"expected load() to ALWAYS return a valid record after an "
            f"abort at N={n} words -- a torn write must never brick the "
            f"unit.\nstdout:\n{run_result.stdout}"
        )
        expected_marker = "O" if n < footprint_words else "N"
        assert marker == expected_marker, (
            f"at N={n} words (record footprint is {footprint_words} "
            f"words), expected the {'PREVIOUS' if expected_marker == 'O' else 'NEW'} "
            f"record ('{expected_marker}') to load, got marker={marker!r}. "
            f"This is the invariant Plan 126-07 measured directly: which "
            f"record wins depends on whether the abort falls before or "
            f"after the record's own footprint, not a blanket "
            f"'previous record always wins' claim.\nstdout:\n{run_result.stdout}"
        )


def test_successive_saves_alternate_slots(tmp_path):
    """Coverage 7: four successive saves land in alternating physical
    slots with strictly increasing `sequence`."""
    compiler = _resolve_compiler()
    rows, run_result = _compile_and_run(
        compiler, tmp_path, "successive_saves", _MAIN_SUCCESSIVE_SAVES
    )

    save_rows = [r for r in rows if "save_iter" in r]
    assert len(save_rows) == 4, (
        f"expected 4 successive save iterations, got {len(save_rows)}.\n"
        f"stdout:\n{run_result.stdout}"
    )

    slots = [int(r["programmed_slot"]) for r in save_rows]
    sequences = [int(r["sequence"]) for r in save_rows]

    for i, r in enumerate(save_rows):
        assert int(r["save_ok"]) == 1, (
            f"expected save iteration {i} to report success.\n"
            f"stdout:\n{run_result.stdout}"
        )

    for i in range(1, len(slots)):
        assert slots[i] != slots[i - 1], (
            f"expected successive saves to alternate physical slots; "
            f"observed slot sequence {slots!r}.\nstdout:\n{run_result.stdout}"
        )

    for i in range(1, len(sequences)):
        assert sequences[i] > sequences[i - 1], (
            f"expected strictly increasing `sequence` across successive "
            f"saves; observed {sequences!r}.\nstdout:\n{run_result.stdout}"
        )

    final_row = next(r for r in rows if "final_loaded" in r)
    assert int(final_row["final_loaded"]) == 1, (
        f"expected the final load() after 4 saves to succeed.\n"
        f"stdout:\n{run_result.stdout}"
    )
    assert final_row["final_marker"] == chr(ord("A") + 3), (
        f"expected the final load() to return the marker of the LAST save "
        f"('{chr(ord('A') + 3)}'), got {final_row['final_marker']!r}.\n"
        f"stdout:\n{run_result.stdout}"
    )


def test_module_has_no_pio_libdeps_dependency():
    """Coverage 8 -- C-12: this module's own text contains no path under
    the gitignored PlatformIO build-artifacts directory. The needle is
    built via concatenation so this test's own description of the property
    does not trip its own check."""
    own_text = Path(__file__).read_text()
    forbidden_path_fragment = "." + "pio" + "/"
    assert forbidden_path_fragment not in own_text, (
        "expected no reference to a " + forbidden_path_fragment + " build "
        "artifact path anywhere in this module (C-12): a gitignored "
        "per-env libdeps dependency passes warm and fails on a clean "
        "checkout."
    )


def test_compiler_is_required_not_optional():
    """Coverage 9 -- adapted verbatim (including its concatenation trick)
    from the analog's self-enforcing no-skip leg
    (tests/test_vpp_seam_manual_on_every_board.py:447-468): this module's
    own source contains no skip decorator and no skip call anywhere, so the
    fail-closed contract in _resolve_compiler is self-enforcing and cannot
    be silently bypassed by a future edit.

    The two needle strings below are built via concatenation (not written
    verbatim) so this test's own assertion text does not trip its own
    check -- the literal substrings must appear NOWHERE in this file,
    including inside this test's failure messages."""
    own_text = Path(__file__).read_text()
    skip_call = "pytest" + ".skip"
    skipif_marker = "mark" + ".skipif"
    assert skip_call not in own_text, (
        "expected no " + skip_call + " call anywhere in this module -- the "
        "compiler-absence case must FAIL, never SKIP."
    )
    assert skipif_marker not in own_text, (
        "expected no @pytest." + skipif_marker + " decorator anywhere in "
        "this module -- the compiler-absence case must FAIL, never SKIP."
    )
