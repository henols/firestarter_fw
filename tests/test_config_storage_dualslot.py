"""
Project Name: Firestarter
Copyright (c) 2026 Henrik Olsson
Permission is hereby granted under MIT license.

Compile and execute the production dual-slot storage core using injected in-memory flash primitives. Assert CRC, corrupt/blank record rejection, sequence ordering, interrupted writes, and slot rotation.
"""


import re

from ._host_build import INCLUDE, REPO_ROOT, compile_ok, host_cxx, run_binary

_PLATFORM_SRC = REPO_ROOT / "platform" / "py32f071" / "src"
_CORE_SRC = _PLATFORM_SRC / "config_storage_dualslot.cpp"

_CRC32_KAT_INPUT = b"123456789"
_CRC32_KAT_EXPECTED = 0xCBF43926

_KV_RE = re.compile(r"(\w+)=(\S+)")


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


def _compile_and_run(tmp_path, scenario, main_text):
    """Write the scenario's fresh TU, compile it together with the shipped
    platform/py32f071/src/config_storage_dualslot.cpp (by explicit path --
    D-02), run it, and return the parsed stdout rows. Every committed
    scenario's main() returns 0 unconditionally; its result crosses on
    stdout."""
    tu_path = _write_ram_fake_tu(tmp_path, scenario, main_text)
    binary_path = tmp_path / f"dualslot_{scenario}"

    compile_ok([
        host_cxx(), "-std=gnu++17", "-Wall", "-Wextra",
        "-I", str(INCLUDE), "-I", str(_PLATFORM_SRC),
        str(_CORE_SRC), str(tu_path),
        "-o", str(binary_path),
    ])

    run_result = run_binary(binary_path)
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


def test_crc32_matches_the_independent_known_answer_vector(tmp_path):
    """Coverage 1 -- D-05: the compiled core's rurp_config_crc32 over the
    nine bytes b"123456789" returns 0xCBF43926. Both the input and the
    expected value are constants written in THIS file, never derived from
    the module under test -- the non-vacuity anchor for the other six
    (HOST-06 discipline)."""
    rows, run_result = _compile_and_run(tmp_path, "crc_kat", _MAIN_CRC_KAT)
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
    rows, run_result = _compile_and_run(tmp_path, "blank", _MAIN_BLANK)

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
    rows, run_result = _compile_and_run(tmp_path, "newest_wins", _MAIN_NEWEST_WINS)

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
    rows, run_result = _compile_and_run(
        tmp_path, "bad_crc_and_length_bound", _MAIN_BAD_CRC_AND_LENGTH_BOUND
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
    rows, run_result = _compile_and_run(tmp_path, "both_corrupt", _MAIN_BOTH_CORRUPT)

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
    rows, run_result = _compile_and_run(
        tmp_path, "interrupted_write", _MAIN_INTERRUPTED_WRITE
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
    rows, run_result = _compile_and_run(
        tmp_path, "successive_saves", _MAIN_SUCCESSIVE_SAVES
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
