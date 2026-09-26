/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/*
 * In one sentence: there is one flash-programming primitive and it writes a
 * whole 256-byte page, so the commit is the completion of a single
 * erase-inactive-then-program-whole-page sequence -- never a separate
 * trailing header/CRC word. Fire-proof:
 * tests/test_config_storage_dualslot.py.
 */

#include "config_storage_dualslot.h"

#include <string.h>

namespace
{

/**
 * Holds the outcome of scanning both slots for the best (highest-sequence,
 * validated) record. Shared by rurp_dualslot_load and the active-slot
 * determination in rurp_dualslot_save, so the two agree on which slot is
 * "active" by construction.
 */
struct ScanResult
{
    bool found;
    uint8_t slot;
    StoredConfiguration record;
};

/**
 * The V5 validation order, implemented as an ORDERING, not merely as a set
 * of independent checks. Each step gates the next; a field is never used
 * before its own check has passed.
 */
bool validate_record(const StoredConfiguration& rec, size_t len)
{
    // 1. magic first: the slot was never written, holds garbage from a
    //    partial write, or holds a record from an unrelated firmware image.
    if (rec.magic != CONFIG_MAGIC)
    {
        return false;
    }

    // 2. length second, bounds-checked against the CALLER's buffer, and
    //    checked BEFORE it is ever used as a copy bound. This is the single
    //    most dangerous field in the record: it arrives from flash and is
    //    used to size a copy into RAM on a Cortex-M0+ with 16 KiB of SRAM
    //    and no MPU configured. The CRC below does NOT protect this check --
    //    a hostile or unlucky writer can recompute a matching CRC over
    //    whatever `length` they choose, so the bound must be ordered before
    //    any copy, never merely present.
    if (rec.length > len)
    {
        return false;
    }

    // 3. crc32 third, over the record from its start up to the crc32 field
    //    itself, computed with offsetof -- never a literal offset, because
    //    sizeof(StoredConfiguration) differs across all three compilers.
    const uint32_t computed = rurp_config_crc32(&rec, offsetof(StoredConfiguration, crc32));
    if (rec.crc32 != computed)
    {
        return false;
    }

    return true;
}

/**
 * Read and validate both slots, keeping the strictly higher `sequence`
 * among the candidates that pass validate_record. `len` bounds the
 * `length`-field check for this scan; both rurp_dualslot_load and
 * rurp_dualslot_save's active-slot determination call this with the same
 * bound (sizeof(rurp_configuration_t), the seam's documented buffer size --
 * include/rurp_config_storage.h states every real caller passes exactly
 * that), so the two agree on which slot is active by construction.
 */
ScanResult scan_slots(const rurp_flash_primitives_t* primitives, size_t len)
{
    ScanResult result;
    result.found = false;
    result.slot = 0;

    for (uint8_t slot = 0; slot < 2; ++slot)
    {
        StoredConfiguration candidate;

        if (!primitives->read(primitives->ctx, slot, &candidate, sizeof(candidate)))
        {
            // Primitive-level read failure: treat the slot as unusable,
            // exactly like a failed validation.
            continue;
        }

        if (!validate_record(candidate, len))
        {
            continue;
        }

        if (!result.found || candidate.sequence > result.record.sequence)
        {
            result.found = true;
            result.slot = slot;
            result.record = candidate;
        }
    }

    return result;
}

} // namespace

extern "C" uint32_t rurp_config_crc32(const void* data, size_t len)
{
    // Bitwise reflected CRC-32, polynomial 0xEDB88320, NO lookup table (a
    // 1 KiB table for an operation that runs at boot and on rare config
    // writes is not worth the flash). This is NOT a security primitive: it
    // detects accidental corruption only and provides no tamper resistance,
    // because anyone able to write flash can recompute it.
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (crc >> 1) ^ 0xEDB88320u;
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }

    return ~crc;
}

extern "C" bool rurp_dualslot_load(const rurp_flash_primitives_t* primitives, void* blob, size_t len)
{
    const ScanResult best = scan_slots(primitives, len);

    if (!best.found)
    {
        // Blank (both slots read 0xFF, failing the magic check) and
        // both-slots-corrupt (failing length or crc32) return the SAME
        // false, indistinguishably. Policy above this core has exactly one
        // recovery path to apply either way, and the caller's buffer is not
        // written past `len` -- nothing was written to it at all.
        return false;
    }

    // Bounded by the smaller of the record's `length` and the caller's
    // `len`. validate_record's step 2 already guarantees
    // best.record.length <= len, which makes this min() redundant on the
    // happy path -- kept anyway as defence in depth, deliberately, not
    // because it is needed today.
    const size_t copy_len = (best.record.length < len) ? best.record.length : len;
    memcpy(blob, &best.record.configuration, copy_len);
    return true;
}

extern "C" bool rurp_dualslot_save(const rurp_flash_primitives_t* primitives, const void* blob, size_t len)
{
    // Step 1: determine the active slot by the same scan rurp_dualslot_load
    // performs, so the two agree by construction and successive saves
    // alternate. If no slot is valid, the inactive slot is slot 0.
    const ScanResult active = scan_slots(primitives, sizeof(rurp_configuration_t));
    const uint8_t inactive_slot = active.found ? static_cast<uint8_t>(1u - active.slot) : static_cast<uint8_t>(0u);

    // Step 2: build the record in RAM.
    StoredConfiguration record;
    memset(&record, 0, sizeof(record));
    record.magic = CONFIG_MAGIC;
    record.version = 1; // written as 1; no reader branches on it yet
    record.length = static_cast<uint16_t>(len);

    const size_t config_copy_len = (len < sizeof(record.configuration)) ? len : sizeof(record.configuration);
    memcpy(&record.configuration, blob, config_copy_len);

    // Monotonically incremented uint32_t, deliberately with NO wraparound
    // branch: flash endurance bounds the write count many orders of
    // magnitude below 2^32, so a rollover branch could never execute and
    // could never be tested.
    record.sequence = active.found ? (active.record.sequence + 1u) : 1u;

    record.crc32 = rurp_config_crc32(&record, offsetof(StoredConfiguration, crc32));

    // Step 3: erase the inactive slot. This is a hard correctness
    // requirement, not a preference -- RM §4.2.3.2 step 2 requires reading
    // out a non-blank page's 64 words before programming over it, and
    // FLASH_Program_Page does not do that, so the HAL is only correct on a
    // page that is already blank. RM §4.5.3 / §4.2.3.3: if the flash
    // write-protection option byte covers the config pages, the erase is
    // silently skipped by the hardware and WRPERR is set instead -- so a
    // non-success return here must never be reported as a successful save.
    if (!primitives->erase_page(primitives->ctx, inactive_slot))
    {
        return false;
    }

    // Step 4: build the WHOLE page in a 4-byte-aligned 256-byte staging
    // buffer, filled with 0xFF first. This buffer is MANDATORY, not
    // stylistic: the HAL reads 64 words from the pointer it is given
    // regardless of the caller's object size, so passing &record directly
    // would program roughly 220 bytes of adjacent RAM into flash --
    // writable, and then readable back over DFU.
    uint32_t page[64];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &record, sizeof(record));

    // Step 5: program the whole page. ITS COMPLETION IS THE COMMIT -- there
    // is no separate header-write, commit-word or
    // final-word step anywhere in this core; no primitive exists for one.
    // The active slot was never touched, so an abort anywhere above leaves
    // it loadable, and an aborted program leaves the inactive slot
    // CRC-invalid, so a subsequent load() rejects it and returns the
    // previous record -- exactly blob 4b1a441's "a failed or interrupted
    // write must leave the previous record usable," reached without a
    // trailing-word commit this part cannot express.
    return primitives->program_page(primitives->ctx, inactive_slot, page);
}
