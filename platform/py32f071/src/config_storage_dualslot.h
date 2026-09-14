/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __CONFIG_STORAGE_DUALSLOT_H__
#define __CONFIG_STORAGE_DUALSLOT_H__

/*
 * The HAL-free dual-slot config storage core.
 * This is the algorithm that scans, validates,
 * selects and alternates over two flash slots; it never touches a register
 * and never includes a HAL header.
 *
 * WHY INJECTED PRIMITIVES:
 *   This algorithm is compiled TWICE from this ONE source file: once linked
 *   against the real PY32 HAL (platform/py32f071/src/config_storage_flash.cpp
 *   supplies the primitives), and once linked against a RAM fake
 *   in tests/test_config_storage_dualslot.py. The tested code
 *   is therefore the shipped code. Two alternatives were considered and
 *   rejected: an independent fake reimplementation living only in the test
 *   (rejected -- it proves a copy behaves, not the shipped code); and
 *   compiling the real backend against a
 *   hand-written stub HAL header for the test to link against (rejected --
 *   the stub becomes an unversioned mirror of the pinned FetchContent SDK
 *   that the test venue cannot see, so it can silently drift from the real
 *   HAL's actual contract).
 *
 * WHY THIS FILE IS HAL-FREE AND UNGUARDED:
 *   No HAL include and no platform `#error` guard appears anywhere below,
 *   because the host test harness compiles this file directly with a bare
 *   host g++. The `#error` platform guard belongs in the HAL glue TU
 *   (config_storage_flash.cpp), never here.
 *
 * RECORD FORMAT IS VENDORED:
 *   The six-field layout below comes verbatim from blob `4b1a441`
 *   (platform/py32f071/CONFIG-STORAGE.md), and `rurp_configuration_t` is
 *   embedded byte-for-byte and unmodified -- which is what makes
 *   "schema unchanged" structurally true rather than merely
 *   asserted in prose. The wrapper's `version` field is NOT `CONFIG_VERSION`
 *   (the `char[6]` literal "VER06" living inside `rurp_configuration_t`
 *   itself, include/rurp_shield.h:46) and must never be "reconciled" with it.
 *
 * COMMIT SEMANTICS:
 *   The obvious shape -- "erase the inactive slot, program the record
 *   body, program the header/CRC word LAST" -- names a step that cannot be
 *   executed on this part. `py32f071_hal_flash.h`'s `IS_FLASH_TYPEPROGRAM`
 *   accepts exactly one value (`FLASH_TYPEPROGRAM_PAGE`); `FLASH_Program_Page`
 *   writes 64 32-bit words unconditionally; RM V0.2 §4.2.3.2 hard-faults on
 *   any non-32-bit write. There is no primitive that writes a single trailing
 *   word. The amended shape, recorded in CONFIG-STORAGE.md, is what this core
 *   implements instead: erase the inactive slot, build the WHOLE 256-byte
 *   record (header and CRC included) in a 4-byte-aligned staging buffer, then
 *   issue ONE page program whose completion IS the commit. The active slot
 *   is never touched, so any interruption leaves the previous record
 *   loadable, and a page interrupted mid-burst fails CRC on the next load.
 *   There is no separate header-write, commit-word or final-word step
 *   anywhere in this core.
 *
 * VALIDATION ORDER (V5, load-bearing):
 *   Every byte read from either slot is UNTRUSTED INPUT: it may be blank
 *   (0xFF), garbage from a partial write, or a record left over from an
 *   unrelated firmware image. The mandatory order, each check gating the
 *   next, is `magic`, then `length` -- bounds-checked against the caller's
 *   buffer -- then `crc32`. A record whose `length` exceeds the caller's
 *   buffer is rejected BEFORE any copy is made; the CRC does not substitute
 *   for this bound check, because anyone able to write flash can recompute a
 *   matching CRC over whatever content they choose.
 *
 * SIZES ARE SYMBOLIC:
 *   Host `long` is 8 bytes; the target's is 4. `sizeof(StoredConfiguration)`
 *   therefore measures 48 / 31 / 36 bytes on host / AVR / (computed) ARM, and
 *   `g++ -m32` is unavailable in this environment to reproduce a 32-bit host
 *   figure. The only absolute claim anywhere in this core is the relational
 *   `sizeof(StoredConfiguration) <= 256`; no literal size or field offset may
 *   be written anywhere in this file or its implementation.
 *
 * FIRE-PROOF:
 *   tests/test_config_storage_dualslot.py exercises this core
 *   directly (the six named behaviours plus the CRC known-answer vector).
 *   tests/test_py32_flash_map.py gates the flash map this core addresses
 *   (slot addresses, page size, erase-unit separation).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rurp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CONFIG_MAGIC -- ASCII 'R' 'U' 'R' 'P' ("RURP"), tying the record to the
 * shield name already used throughout the firmware. This is a
 * LOCAL CHOICE, explicitly NOT vendored: blob `4b1a441`
 * specifies the `magic` field but supplies no value for it, so this constant
 * must never be described as vendored.
 *
 * It satisfies two hard constraints: it is neither `0xFFFFFFFF` -- what
 * erased NOR flash reads back as, which would make a blank slot look like a
 * valid record and collapse the blank test into a CRC-collision
 * inference -- nor `0x00000000`.
 */
#define CONFIG_MAGIC ((uint32_t)0x52555250)

/**
 * StoredConfiguration -- the vendored on-flash record, six fields in
 * blob `4b1a441`'s order:
 *   magic         -- separates "never written" and "garbage" from a real
 *                     record; validated first.
 *   version       -- written as the literal 1. No reader branches on this
 *                     field yet, and a version-dispatch branch must NOT be
 *                     added: it could never execute and could never be
 *                     tested. It exists so a future schema change has
 *                     somewhere to record its own generation -- a future
 *                     phase's problem, not this one's.
 *   length        -- the length, in bytes, of `configuration` as written.
 *                     Gives forward compatibility and makes a cross-width
 *                     read detectable rather than silently misparsed. This
 *                     is the single most dangerous field in the record: it
 *                     arrives from flash and is used as a copy bound on a
 *                     Cortex-M0+ with 16 KiB of SRAM and no MPU configured.
 *   configuration -- rurp_configuration_t, embedded byte-for-byte and
 *                     unmodified here.
 *   sequence      -- a monotonically incremented uint32_t. Drives
 *                     newest-wins selection on load and slot alternation on
 *                     save. Deliberately has NO wraparound branch: flash
 *                     endurance bounds the write count many orders of
 *                     magnitude below 2^32, so a rollover branch could never
 *                     execute and could never be tested.
 *   crc32         -- validates the record. Checked LAST, after `magic` and
 *                     the `length` bound, never before either.
 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    rurp_configuration_t configuration;
    uint32_t sequence;
    uint32_t crc32;
} StoredConfiguration;

/**
 * rurp_flash_primitives_t -- the three flash operations this core needs,
 * injected so the same algorithm can be linked against the real HAL or a RAM
 * fake. `ctx` is opaque state handed back to every call unchanged.
 */
typedef struct
{
    /**
     * Read `len` bytes from slot `slot` (0 = A, 1 = B) into `dst`. Returns
     * false on any primitive-level failure; the caller treats that slot as
     * unusable, the same as a failed validation.
     */
    bool (*read)(void* ctx, uint8_t slot, void* dst, size_t len);

    /**
     * Erase the whole 256-byte page backing slot `slot`. This is the ONLY
     * erase granularity this core ever asks for.
     */
    bool (*erase_page)(void* ctx, uint8_t slot);

    /**
     * Program the whole 256-byte page backing slot `slot` from EXACTLY 64
     * words. This is the ONLY program granularity the part offers:
     * `IS_FLASH_TYPEPROGRAM` accepts one value and `FLASH_Program_Page`
     * writes 64 words unconditionally, so the caller must always hand over
     * a full 64-word buffer -- the HAL reads 64 words regardless of the
     * caller's object size.
     *
     * A call on a page that was not just erased is a DEFECT, not a smaller
     * or silently-accepted overwrite: RM §4.2.3.2 step 2 requires reading
     * out a non-blank page's 64 words before programming over it, and
     * `FLASH_Program_Page` does not do that -- the HAL is only
     * correct on a page that is already blank. The RAM fake used by
     * tests/test_config_storage_dualslot.py asserts this as a fake-detected
     * failure.
     */
    bool (*program_page)(void* ctx, uint8_t slot, const uint32_t words[64]);

    void* ctx;
} rurp_flash_primitives_t;

/**
 * rurp_config_crc32 -- a bitwise reflected CRC-32, polynomial 0xEDB88320, NO
 * lookup table (a 1 KiB table for an operation that runs at boot and on rare
 * config writes is not worth the flash). Computed over `len` bytes starting
 * at `data`.
 *
 * CRC32 IS NOT A SECURITY PRIMITIVE: it detects accidental corruption only,
 * and provides no tamper resistance -- anyone able to write flash can
 * recompute a matching CRC32 over whatever content they choose. "CRC-
 * protected" must never drift into "authenticated" in any artifact this
 * milestone writes.
 *
 * The tree's only existing checksum, the CRC8-CCITT PROGMEM accessor at
 * src/boards/rurp_serial_utils.cpp:381, is deliberately NOT reused here: it
 * is the wrong algorithm (CRC8, not CRC32) and AVR-shaped (PROGMEM), living
 * inside a translation unit the native envs compile.
 */
uint32_t rurp_config_crc32(const void* data, size_t len);

/**
 * rurp_dualslot_load -- scan both slots, validate each (magic, then
 * length-bounded, then crc32, in that order), and keep the valid candidate
 * with the strictly higher `sequence`. Copies into the caller's `blob`
 * (bounded by `len`) and returns true on success.
 *
 * Returns false when no valid candidate survives -- and that outcome is
 * INDISTINGUISHABLE between "both slots blank" and "both slots
 * corrupt": policy above this core has exactly one recovery path to apply
 * either way.
 */
bool rurp_dualslot_load(const rurp_flash_primitives_t* primitives, void* blob, size_t len);

/**
 * rurp_dualslot_save -- persist `len` bytes from `blob` following the
 * commit shape: erase the inactive slot, build the
 * whole record in a 4-byte-aligned 256-byte staging buffer, then program
 * that one page. The active slot is never touched. Returns true only if the
 * page program completed successfully; a non-`HAL_OK` result from either
 * primitive returns false immediately and is never reported as success.
 */
bool rurp_dualslot_save(const rurp_flash_primitives_t* primitives, const void* blob, size_t len);

/*
 * The only absolute size claim anywhere in this core: true under all
 * three compilers (host, AVR, ARM), unlike any literal size or offset would
 * be. No literal size or field offset may be written anywhere in this file
 * or its implementation -- use sizeof/offsetof instead.
 */
static_assert(sizeof(StoredConfiguration) <= 256, "StoredConfiguration must fit in one 256-byte flash page");

#ifdef __cplusplus
}
#endif

#endif // __CONFIG_STORAGE_DUALSLOT_H__
