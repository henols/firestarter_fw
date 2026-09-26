# PY32F071 Flash-Persistent Configuration — Design Record

**Phase 126 Plan 01. Requirements: CFG-01, CFG-02.**

This document is the in-scope subset of the flash-configuration design, vendored onto this
milestone branch because its cited source is stranded on closed pull requests. It vendors blob
**`4b1a441`**'s §"Configuration storage" — the only part of that document PR #48 did not
supersede — cites it by SHA, and marks everything superseded as superseded rather than silently
following it. Both closed-PR homes of that blob: `feature/py32f071-toolchain` (PR #46, **closed**)
and `feature/py32f071-full-support` (PR #47, **closed**).

**`platform/py32f071/PORTING.md` does not exist on any live branch.** `.planning/PROJECT.md` and
`.planning/STATE.md` both cite that path as the port's specification; a later reader who goes
looking for it under that name on `v1.23-py32f071-integration` (or any other live branch) will not
find it. This document, plus the historical blob SHA above, is what survives.

## Configuration storage (vendored, in scope)

Blob `4b1a441`'s entire in-scope subset, three sentences and a struct, verbatim:

> AVR continues to use EEPROM. PY32F071 uses internal flash with two independently validated
> records:
>
> ```cpp
> struct StoredConfiguration {
>     uint32_t magic;
>     uint16_t version;
>     uint16_t length;
>     rurp_configuration_t configuration;
>     uint32_t sequence;
>     uint32_t crc32;
> };
> ```
>
> The newest valid sequence is loaded. A failed or interrupted write must leave the previous record
> usable.

Also in scope, from the blob's acceptance checklist: *"Flash-backed configuration survives
interrupted writes."*

**Record explicitly, so nobody later "reconciles" them (D-17):** the wrapper's `version` field
(`uint16_t`) is **not** `CONFIG_VERSION` — the `char[6]` literal `"VER06"` that lives inside
`rurp_configuration_t` itself (`include/rurp_shield.h:46`). `rurp_configuration_t` is embedded
**byte-for-byte**, unmodified by this phase, which is what makes CFG-07's "schema unchanged"
structurally true rather than merely asserted in prose. `version` is written as the literal `1`
by this phase, and nothing in this phase reads it back to branch on it — **no version-dispatch
branch is added**. It exists in the wrapper so a future schema change has somewhere to record its
own generation; that is a future phase's problem, not this one's.

## SUPERSEDED by PR #48's actual module layout

Blob `4b1a441`'s entire §"PY32F071 backend modules" tree is superseded. Every module name it
prescribes is marked below, mapped to what PR #48 actually built under `platform/py32f071/src/`
and `platform/py32f071/include/`. **None of the left-hand names exist in this tree; do not go
looking for them, and do not resurrect them.**

| Blob's prescribed module | Superseded — actually built as |
|---|---|
| `storage.cpp` | `config.cpp` (and this phase **deletes** `config.cpp` per CFG-07 — its `rurp_save_config()` persists nothing and its `rurp_validate_config` is a second, drifted copy of common policy). The replacement is this phase's own `config_storage_dualslot.cpp` / `config_storage_flash.cpp`, landed in later plans, not by PR #48 |
| `gpio.cpp` | folded into `py32f071_rurp_shield.cpp` (GPIO configuration and the data-bus routines live there, not in a standalone file) |
| `board.cpp` | folded into `py32f071_rurp_shield.cpp` and `main.cpp` (startup order and board bring-up) |
| `adc.cpp` | folded into `py32f071_rurp_shield.cpp` (`configure_adc`, `read_adc_channel`, `read_adc_average`) |
| `dac.cpp` | **no successor exists in this tree.** The DAC-VPP concern this module would have owned is out of scope this phase (see below) |
| `py32f071_board.h` | folded into `include/boards/py32f071_rurp_shield.h` |
| `py32f071_pins.h` | folded into `include/boards/py32f071_rurp_shield.h` (the same header carries the pin map) |
| `usb.cpp` | superseded by `usb_cdc.c` plus `platform_compat.cpp` (the CherryUSB CDC glue and portability shims) |

Two further modules PR #48 built that the blob never named at all: `main.cpp` (entry point and
startup sequencing) and `platform_compat.cpp` (portability shims the blob's outline did not
anticipate). They are not superseding a specific blob name; they are simply new.

## Out of scope

The blob's §"ADC measurement" steps 5–6 (apply the common two-point board calibration; average/
filter enough samples to reject boost-converter ripple), its entire §"DAC VPP control", and its
acceptance items covering calibration, the closed DAC loop and real hardware, are **out of scope**
for this phase. They route to **FUT-VPP** and **FUT-CAL** (the queued calibration milestone).

Why the full 195-line blob was not restored under its own name: 4 of its 15 acceptance items cover
calibration, the closed DAC loop, or real hardware — all explicitly deferred — and restoring the
whole document in-tree under the `PORTING.md` name would re-strand those out-of-scope prescriptions
as if they were this phase's contract. Vendoring only the in-scope subset, with the rest marked
superseded or out of scope, is the deliberate choice (CONTEXT.md §Discretion).

## Flash geometry

Source of record: **Puya, *PY32F07X Series Reference Manual*, V0.2** — §4.1 "Key features" (p.34),
§4.2.1 "Flash structure" (p.34), **Table 4-1 "Flash structure and boundary addresses"** (p.34).

| Property | Value | Citation |
|---|---|---|
| Page size (smallest erase + program unit) | **256 bytes** | RM V0.2 §4.1, §4.2.1 |
| Sector size | **8192 bytes (8 KBytes)** | RM V0.2 §4.1, §4.2.1; §4.2.3.5 |
| Main flash | 128 KBytes, `0x08000000`–`0x0801FFFF` = 16 sectors / 512 pages | RM V0.2 Table 4-1 |
| Sector 15 | pages 480–511, `0x0801E000`–`0x0801FFFF` | RM V0.2 Table 4-1 |
| Program granularity | one full page, as 64 × 32-bit words; a non-32-bit write raises a hard fault | RM V0.2 §4.2.3.2 |
| Erase granularities | page (`PER`, 256 B), sector (`SER`, 8 KB), mass (`MER`) | RM V0.2 §4.2.3.3/.4/.5 |

Corroborated byte-for-byte against the pinned SDK `OpenPuya/PY32F071_Firmware` @
`0ed2f4b4d3391eccfd4491006a30295fd78e32c2` — the exact `GIT_TAG` in
`platform/py32f071/CMakeLists.txt:16` — `Drivers/CMSIS/Device/PY32F071/Include/py32f071xB.h:575-581`:

```c
#define FLASH_PAGE_SIZE       0x00000100U      /*!< FLASH Page Size, 256 Bytes */
#define FLASH_SECTOR_SIZE     0x00002000U      /*!< FLASH Sector Size, 8192 Bytes */
#define FLASH_BASE            (0x08000000UL)   /*!< FLASH base address */
#define FLASH_END             (0x0801FFFFUL)   /*!< FLASH end address */
```

**Do not use** either of these two traps, both of which look plausible and are both wrong for this
part:

1. `py32f071_hal_flash.h:268`'s own comment, `/*!<Program 128bytes at a specified address.*/` — a
   stale comment carried over from the smaller PY32F030 part, sitting directly above
   `FLASH_Program_Page`'s own `while(index<64U)` loop (64 × 4 bytes = 256, not 128), and
   contradicted outright by RM §4.2.3.2.
2. The widely circulated "128-byte page / 4 KiB sector" PY32 figures — correct for PY32F030 /
   PY32F003, wrong for the PY32F071 this port targets.

## Reserved flash map

| Region / symbol | Address | Length | Note |
|---|---|---|---|
| `BOOTLOADER` (D-13 named seam) | `0x08000000` | **0** | zero-length placeholder; see the migration-cost paragraph below |
| `FLASH` (application region) | `0x08000000` | **120K** (`0x1E000`) | shrunk from the physical 128K so `.text`/`.rodata` cannot physically reach the config region |
| `CONFIG` | `0x0801E000` | **8K** (Sector 15, pages 480–511) | one whole sector, sector-aligned |
| `__config_slot_a_start` | `0x0801E000` | 256 B | page 480 |
| `__config_slot_b_start` | `0x0801E100` | 256 B | page 481 — a **different page erase unit** from slot A |
| `__config_page_size` | — | `256` | matches `FLASH_PAGE_SIZE` above |

**D-18 amends D-10's shrink quantum — as an amendment, not a replacement.** D-10's own elements are
untouched: top-of-flash placement, a shrunk `FLASH` `LENGTH`, a second `MEMORY` region, and
`PROVIDE`d linker symbols for the C code (D-11). D-18 changes only the *quantum* of the shrink,
from "two erase units" (512 B) to **one whole 8 KiB sector**. Reason: at `0x0801FE00` (the 512 B
reading), the app region would end at `0x0801FDFF` — **inside Sector 15** — so a sector-granular
DFU erase of the app's final block would be exactly the block holding config; D-10 was chosen
specifically to avoid needing that hazard, and the sector-aligned reservation removes it rather
than merely documenting it. **Accepted cost:** 7680 B of the reserved 8192 B is deliberate slack
(6.25% of total flash), reclaimable later by FUT-N05 or by additional config slots **without
moving any address**.

**D-13's bootloader seam carries its cost honestly.** A top-of-flash `CONFIG` region grows
*downward* as it is reserved, without moving anything below it. A bottom-of-flash `BOOTLOADER`
placeholder does **not** have that property: giving it a non-zero length later **moves the
application's `ORIGIN`**, which is a flash-map **migration**, not a resize — every previously
flashed unit's vector-table address changes. Phase 129 must record its bootloader budget as an
*intent with that cost attached*, never as a number that reads as already paid for.

## Amendment to D-16 — the commit step, corrected

D-16's locked wording is: *erase the inactive slot → program the record body → program the
header/CRC word LAST.* Research (C-2) found a step in that sequence that **cannot be executed on
this part**, and this is recorded as an explicit amendment, not a silent reinterpretation.

Why: `py32f071_hal_flash.h:631`'s `IS_FLASH_TYPEPROGRAM` accepts exactly one value
(`FLASH_TYPEPROGRAM_PAGE`) — there is no word, halfword or byte program primitive.
`FLASH_Program_Page` writes 64 `uint32_t` words unconditionally, and RM V0.2 §4.2.3.2 states any
non-32-bit write raises a hard fault. There is no primitive that writes a single trailing word, so
"program the header/CRC word LAST" as a distinct final step is not implementable here.

**Corrected shape:** erase the inactive slot → build the *entire* 256-byte record, header and CRC
included, in a 4-byte-aligned staging buffer → issue one page program whose completion **is** the
commit. The property D-16 exists to protect survives intact: the active slot is never touched
until the new one is complete, so any interruption leaves the previous record loadable, and a page
interrupted mid-burst fails its CRC check on the next load and is rejected outright.

An alternative that preserves D-16's literal wording — two pages per slot, a body page followed by
a separate commit page — was considered and rejected: it adds a failure mode (body page valid,
commit page torn) that CRC rejection already covers for free, at the cost of a second page and a
second erase per write. The phrase "program the header/CRC word LAST" must not appear anywhere in
this phase's implementation as an instruction to follow.

## CONFIG_MAGIC

The value is **`0x52555250`** — the ASCII four-character code `'R'`, `'U'`, `'R'`, `'P'`, tying the
record to the shield name already used throughout the firmware (`rurp_configuration_t`,
`rurp_config_utils.cpp`) and readable as `RURP` in a hex dump. It is defined **once**, in the
HAL-free core's local header.

**Recorded explicitly as a this-milestone choice, not vendored:** blob `4b1a441` specifies the
`magic` *field* but supplies no value for it. Describing this constant as vendored would be
exactly the shape of overclaim Phase 122's C-5 had to correct — attributing a this-milestone
decision to a source document that never made it. `0x52555250` satisfies the two hard constraints
the value must meet: it is neither `0xFFFFFFFF` (what erased NOR flash reads back as, which would
make a blank slot look like a valid record) nor `0x00000000`.

## Validation order for a record read from flash

Every byte read from slot A or slot B is **untrusted input** — it may be blank, garbage left over
from a partial write, or a leftover record from an unrelated firmware image. The mandatory
validation order, each check gating the next: **`magic`**, then **`length`** — bounds-checked
against the caller's buffer — then **`crc32`**.

A record whose `length` exceeds the caller's buffer is **rejected before any copy is made**; the
eventual copy, once a record passes validation, is bounded by `min(length, len)`. **The CRC does
not save you here**: the bounds check on `length` must be ordered strictly before the copy, because
anyone able to write flash can recompute a matching CRC over any content they choose. Target for
this reasoning: Cortex-M0+, 16 KiB SRAM, no MPU configured — there is no hardware backstop behind
this check.

## CRC32 is not a security primitive

CRC32 detects **accidental corruption only**. It provides no tamper resistance and no
authentication: anyone who can write to flash can recompute a CRC32 over whatever content they
choose and produce a record that validates. "CRC-protected" must never drift into "authenticated"
in any artifact this milestone writes.

## Erase before program is mandatory

RM V0.2 §4.2.3.2's programming sequence, step 2, requires reading out the 64 words of a page that
already holds data before programming over it. `FLASH_Program_Page` does **not** perform that
read-out — it is only correct on a page that is already blank. This makes "erase the inactive slot
first" a hard correctness requirement, not a design preference: a program call on a page that has
not just been erased is not a smaller write, it is a defect. The dual-slot test suite asserts a
program attempt on a non-erased page as a **test failure**, never as a silent overwrite.

## Write protection

RM V0.2 §4.5.3 / §4.2.3.3: if the flash write-protection option byte covers the config pages, a
page erase is **silently skipped** by the hardware and the `WRPERR` flag is set instead. The
requirement this places on the backend: any HAL return other than `HAL_OK` must yield
`save() == false`. The backend must never report a save as successful when it was not.

## Reset and interrupt behaviour

RM V0.2 §4.2.3 (p.35), quoted verbatim, twice:

> "If a reset occurs during Flash program and erase operations, the contents of the Flash memory
> are not protected."

> "During a program and erase operations to the Flash memory, any attempt to read the Flash memory
> will stall the bus. The read operation will proceed correctly once the program and erase
> operations has completed. This means that code or data fetches cannot be made while programming
> and erasing operations are in progress."

Combined with the HAL's own interrupt masking (`__disable_irq()`/`__set_PRIMASK()`) across the
64-word program burst, **no interrupt is serviced for the duration of an erase or a program** —
USB CDC included. This sits in direct tension with blob `4b1a441`'s own architectural requirement
that USB must keep running. The prerequisite RM §4.2.3 states for program and erase — HSI must be
on — is already satisfied: `platform/py32f071/src/main.cpp:25-27` turns HSI on and selects it as
the PLL source before anything else runs. A future clock refactor that turns HSI off would
silently break config persistence; this note exists so that refactor does not happen quietly.

**D-14's first-boot cost, recorded as a non-claim:** the validate-and-write-back policy is
unchanged on both platforms (CFG-03 keeps it common), so `rurp_validate_config`'s existing
write-back path fires on a virgin PY32F071 exactly as it does on AVR — meaning a flash erase and
program cycle during startup, stalling the Cortex-M0+ for the duration. With no PCB in existence,
that cost is **not measured**. It is written here as *not measured* — never as *acceptable*, and
never accompanied by a number.

## Host contract

This phase is firmware-only; Phase 127 owns the host half of Criterion 5 and runs in parallel.

`FLASH_BASE` stays `0x08000000` and `FLASH_SIZE` stays the **physical 131072** — because
`FLASH_SIZE` is a *refusal envelope* (`py32_dfu.py:648`), not an erase bound, and shrinking it
would make the host unable to flash the part it describes. The linker's `FLASH` `LENGTH` becomes
120K in this phase while the host's `FLASH_SIZE` stays 128K: this asymmetry is **correct, not
drift** — one number is an application-region bound, the other a physical refusal envelope, and
the two answer different questions.

The host's DFU erase is payload-length-scoped (`py32_dfu.py:748-750`), which is what makes
top-of-flash placement preserve config for free when an installed image does not reach the top of
flash — recorded here as the *intended* behaviour, an explicit non-claim until a board exists to
confirm it on. The host's `DEFAULT_ERASE_PAGE_SIZE = 2048` fallback matches neither the 256 B page
nor the 8192 B sector; it fires only when the device publishes no DfuSe layout, and this phase does
not edit the host to change that.

The reserved config base `0x0801E000` is the named constant Phase 127 must agree with. Two
obligations recorded for whoever writes Phase 127's half: `firestarter_app/tests/scan_paths.py`'s
`CROSS_REPO_TEST_PATHS` contains no entry today for the linker script or any config file, so a host
test reading firmware source text must add one; and per BASE-02 that test must key firmware
presence on `../firestarter/.git` via `tests/fw_presence.py`, never on a single scanned file.

## Claim ceiling

No PY32F071 hardware exists as a physical board. Nothing recorded in this document is a claim
about behaviour observed on that silicon; every figure above is either quoted from the reference
manual, corroborated from the pinned SDK's own header, or derived from source already in this
tree. Any ARM build evidence this milestone produces is a CI workflow run URL plus a head SHA,
never a local build — `arm-none-eabi-gcc`, `cmake` and `ninja` are absent from this environment.
The full list of claims this milestone may and may not make is recorded once, in
`.planning/REQUIREMENTS.md` §"Validation Ceiling"; this document defers to that list by reference
rather than restating its wording.
