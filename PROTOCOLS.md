# Programming Protocols — firmware reference

Implementation reference for how the firmware drives each protocol: write
algorithms, pulse widths, voltage routing, register constants, datasheet
citations and dispatch traceability.

This is the developer-facing document. The user-facing description of what each
protocol is and which chips it is for lives on the wiki:
https://github.com/henols/firestarter/wiki/Programming-Protocols

No tool machine-reads this document — the `tools/wiki/` checkers in the meta repository that
used to cross-check the dispatch table here, the host tool and the firmware were retired on
2026-09-02 (`5426d7ef`), and **no automated dispatch guard exists now** — so the table below is
maintained for human readers only.

`datasheets/<hex>-<NAME>/` folder slug (col 1, phase-85 committed, not renamed) and a
**3-field canonical name entry** — (1) a C-identifier-safe `PROTO_` token, (2) a short human
display name, (3) the datasheet-cited behavioral facet prose (write algorithm, erase model, VPP
behavior, pin roles) — plus a handler-family layer naming the existing many-to-one
`configure_*` dispatch groupings (Phase 100, NAME-01/02/03). It also documents the Phase-86
FM1608 and X88C64 identity corrections (NAME-04), names the phantom and infeasible buckets as
honest non-protocols, and carries the INV-01..INV-09 invariant traceability matrix that anchors
the SAFE-02 handoff to Phases 88/89.

**Reader router:**

- To look up a protocol bucket by hex ID, see [§1 (real protocol buckets)](#1-real-protocol-buckets).
- To understand the FM1608 SRAM→FRAM identity correction or the X88C64 EEPROM identity correction, read the relevant bucket section inside §1 — the NAME-04 call-outs are there.
- To see why a phantom (0x35/0x39) or infeasible (0x11/0x2A/0x2B/0x2C) protocol ID routes to `configure_not_implemented()`, read [§2 (Honest non-protocols)](#2-honest-non-protocols).
- To find the INV-01..INV-09 traceability matrix (invariants, owning handlers, planned native test function names, suite paths), read [§3 (invariant traceability matrix)](#3-invariant-traceability-matrix).

**Canonical bucket set** (re-verified from `chip_database.json` before authoring — DB is authoritative):

> **Operator-approved 2026-07-01 at the Phase-100 NAME-02 gate.** Every `PROTO_` token and
> display name below is final and authoritative; the frozen `datasheets/<hex>-<NAME>/` slug
> column (col 1) is retained verbatim as the DOC-02 divergence anchor and is NOT renamed
> (NAME-F1 deferred). The 0x0E vs 0x29 32-pin SRAM name collision (D-05) is resolved with two
> distinct tokens: `PROTO_SRAM_32PIN` (0x0E) and `PROTO_SRAM_32PIN_NVRAM` (0x29).

**Name ↔ Slug Divergence**

> (a) The frozen-slug column (col 1, `datasheets/<hex>-<NAME>/`) below IS the canonical
> old-slug ↔ new-name map — read a row left-to-right to translate any old minipro-bucket slug
> (e.g. `0x0B-EPROM-LEGACY`) to its current `PROTO_` token + display name (e.g.
> `PROTO_EPROM_24PIN` / "EPROM — 24-pin legacy, 12–25V direct-VPE"). (b) The `datasheets/`
> folder slugs are intentionally frozen and are NOT renamed (NAME-F1 deferred, avoids
> folder/provenance churn) — this doc *records* the divergence, it does not resolve it.
> (c) The host CLI ASCII-normalizes dashes (em-dash `—` / en-dash `–` → ASCII hyphen `-`) in
> its display strings (`firestarter_app/firestarter/ic_layout.py` `_PROTOCOL_DISPLAY_NAME`), a
> documented punctuation deviation from the em-dash col-2 display names below (Phase 102 D-02)
> — the names are otherwise identical.

<!-- firestarter-claims-begin -->
| hex | DB chip count | frozen slug (col 1) | PROTO_ token | display name | handler-family | phantom? |
|-----|--------------|---------------------|--------------------------|--------------------------|-----------------|----------|
| 0x05 | 27 | `0x05-FLASH-AMD-STD` | `PROTO_FLASH_5V_PAGE` | Flash — 5V page-write (EEPROM-like) | 5v_page (0x05 + phantoms 0x35/0x39) | no |
| 0x06 | 190 | `0x06-FLASH-AMD-ALT` | `PROTO_FLASH_NOR_UNLOCK` | Flash — AMD/SST unlock-sequence NOR | nor_unlock (0x06, single-protocol) | no |
| 0x07 | 170 | `0x07-EPROM-STD` | `PROTO_EPROM_28PIN` | EPROM — 28-pin UV/EE, 13V VPP | eprom (0x07/0x08/0x0B) | no |
| 0x08 | 127 | `0x08-EPROM-QUICK` | `PROTO_EPROM_32PIN` | EPROM — 32-pin UV/EE, 13V VPP | eprom (0x07/0x08/0x0B) | no |
| 0x0B | 32 | `0x0B-EPROM-LEGACY` | `PROTO_EPROM_24PIN` | EPROM — 24-pin legacy, 12–25V direct-VPE | eprom (0x07/0x08/0x0B) | no |
| 0x0D | 84 | `0x0D-EEPROM-POLL` | `PROTO_EEPROM_PARALLEL` | EEPROM — 5V parallel, SDP + DQ7 poll | eeprom28c (0x0D, single-protocol) | no |
| 0x0E | 20 | `0x0E-SRAM-32PIN` | `PROTO_SRAM_32PIN` | SRAM — 32-pin battery-backed NVRAM | sram (0x0E/0x27/0x28/0x29) | no |
| 0x10 | 39 | `0x10-FLASH-INTEL` | `PROTO_FLASH_INTEL` | Flash — Intel 28F command-register, 12V VPP mandatory | flash_intel (0x10, single-protocol) | no |
| 0x27 | 2 | `0x27-SRAM-24PIN` | `PROTO_SRAM_24PIN` | SRAM — 24-pin async, 5V | sram (0x0E/0x27/0x28/0x29) | no |
| 0x28 | 34 | `0x28-SRAM-STD` | `PROTO_SRAM_28PIN` | SRAM/FRAM — 28-pin (NAME-04: FM1608, see §1.10) | sram (0x0E/0x27/0x28/0x29) | no |
| 0x29 | 20 | `0x29-SRAM-512K-1M` | `PROTO_SRAM_32PIN_NVRAM` | SRAM — 32-pin large battery-backed NVRAM, 512K–1M | sram (0x0E/0x27/0x28/0x29) | no |
| 0x34 | 1 | `0x34-EEPROM-X88C64` | `PROTO_EEPROM_8051BUS` | EEPROM — XICOR 8051-bus, PCB-blocked (FUT-01) (NAME-04: X88C64, see §1.12) | not-implemented (0x34, PCB-blocked) | no |
| 0x35 | 0 | `(none)` | `PROTO_PHANTOM_0x35` | (phantom — 0 DB chips, dispatch-preserved for forward-compat) | 5v_page dispatch arm | YES |
| 0x39 | 0 | `(none)` | `PROTO_PHANTOM_0x39` | (phantom — 0 DB chips, dispatch-preserved for forward-compat) | 5v_page dispatch arm | YES |

**Handler-family layer (D-09 — names the 7 existing `configure_*` dispatch groupings, grounded in `memory.cpp` lines 74–103):**

| Handler-family | `configure_*` function | File | Protocols |
|----------------|------------------------|------|-----------|
| eprom | `configure_eprom()` | `eprom.cpp` | 0x07, 0x08, 0x0B (many-to-one) |
| sram | `configure_sram()` | `sram.cpp` | 0x0E, 0x27, 0x28, 0x29 (many-to-one; the 0x0E/0x29 D-05 collision is resolved here — `PROTO_SRAM_32PIN` vs `PROTO_SRAM_32PIN_NVRAM`) |
| 5v_page | `configure_flash_5v_page()` | `flash_5v_page.cpp` | 0x05 (+ phantom dispatch arms 0x35/0x39) |
| nor_unlock | `configure_flash_nor_unlock()` | `flash_nor_unlock.cpp` | 0x06 (single-protocol) |
| eeprom28c | `configure_eeprom28c()` | `eeprom_28c.cpp` | 0x0D (single-protocol) |
| flash_intel | `configure_flash_intel()` | `flash_intel.cpp` | 0x10 (single-protocol) |
| not-implemented | `configure_not_implemented()` | `not_implemented.cpp` | 0x34 (PCB-blocked) + infeasible 0x11/0x2A/0x2B/0x2C (out of scope, §2.2) |
<!-- firestarter-claims-end -->

---

## 1. Real Protocol Buckets

Each section below gives the NAME-01 four facets (write algorithm, erase model, VPP behavior, pin roles) with datasheet-anchored citations. Datasheet citations use the form `datasheets/<slug>/<file>.pdf p.N §section` wherever page and section are recoverable.

> **Note (Phase 140 / TABLE-04, F-140-08):** the `datasheets/<slug>/<file>.pdf` paths cited
> throughout this section do **not** resolve on this branch — that tree exists only on
> `v1.16-protocol-first-architecture-rebuild`. To recover a cited PDF, run
> `git show v1.16-protocol-first-architecture-rebuild:datasheets/<slug>/<file>.pdf > <file>.pdf`
> from the firmware repo root. For the three 27C write-algorithm rows (§§1.3, 1.4, 1.5),
> per-value attribution is machine-readable and gate-enforced at
> `tests/golden/eprom_params_citations.json` (Phase 140 / TABLE-04) — that sidecar, not this
> prose, is authoritative for any value in `eprom_params_t`.

---

### 1.1 — 0x05 PROTO_FLASH_5V_PAGE: 5V Page-Write Flash (EEPROM-like)

**Folder slug (col 1):** `0x05-FLASH-AMD-STD`
**Canonical name (col 2):** `PROTO_FLASH_5V_PAGE` — Flash — 5V page-write (EEPROM-like)
**Handler:** `configure_flash_5v_page()` → `flash_5v_page.cpp`
**DB chip count:** 27 (AT29C, W29C, SST29EE series)

**Write algorithm:** Optional SDP unlock (3 bus cycles: 0xAA→0x5555, 0x55→0x2AAA, 0xA0→0x5555), then up to 64–256 bytes written sequentially to addresses within the same page. All bytes must complete within tBLC (inter-byte window, typically 100–150 µs). After the last byte, the chip's internal write cycle begins (~5–10 ms). DQ7 data polling confirms completion: read the last written address; when DQ7 matches the written bit, the cycle is done.
Citation: `datasheets/0x05-FLASH-AMD-STD/W29C020.pdf` p.9 §Write Operation; `datasheets/0x05-FLASH-AMD-STD/W29C040.pdf` p.11 §Page Write.

**Erase model:** Implicit auto-erase — each page write erases the target page internally before programming. No explicit chip-level erase command. Chip-level erase is available via a 6-cycle SDP sequence (0x80→0x5555 then 0x10→0x5555) on chips that support it. The write path (`flash_5v_page_write_init`) performs **no blank check** on this protocol either (Phase 153, ERASE-02), for the same reason as `0x0D` above — the silicon auto-erases per page during the write, so a pre-write blank check was a false precondition rather than a safety net. The firmware implements no chip-level erase for this protocol at all: the host continues to clear `FLAG_CAN_ERASE` for algorithm 5, and the firmware's `eprom_erase` continues to refuse a standalone erase on it, so the request is refused twice over rather than routed anywhere. The silicon's own 6-cycle software chip-erase sequence named above is a recorded backlog item (Backlog 999.63, "Software chip-erase for the `0x05` family"), not a firmware capability today — this paragraph is not a claim that the part cannot be erased at all, only that erasing it is not yet implemented.
Citation: `datasheets/0x05-FLASH-AMD-STD/W29C040.pdf` p.12 §Chip Erase.

**VPP behavior:** None (5V-only operation). The internal charge pump on the chip drives write electricals; the RURP VPP regulator is not used for this bucket.
Citation: `datasheets/0x05-FLASH-AMD-STD/W29C020.pdf` p.3 §Pin Description (VCC = 5V only).

**Pin roles:** 32-pin DIP. Standard JEDEC 27-series pinout extension. Address lines A0–A18, data D0–D7, CE active-low (chip enable), OE active-low (output enable), WE active-low (write enable). Page-size is data-driven from `handle->mem_size` at runtime — see INV-04 (5v_page 256B page boundary in §3) for the 0x05/0x0B page-size derivation detail, which applies to W29C040 (512KB → 256B page) and smaller siblings.

---

### 1.2 — 0x06 PROTO_FLASH_NOR_UNLOCK: AMD/SST Unlock-Sequence NOR Flash

**Folder slug (col 1):** `0x06-FLASH-AMD-ALT`
**Canonical name (col 2):** `PROTO_FLASH_NOR_UNLOCK` — Flash — AMD/SST unlock-sequence NOR
**Handler:** `configure_flash_nor_unlock()` → `flash_nor_unlock.cpp`
**DB chip count:** 190 (AM29F, SST39SF, W39F, MX29F, A29F series — the dominant protocol)

**Write algorithm:** 3-cycle software unlock before each byte program: write 0xAA→0x5555, 0x55→0x2AAA, 0xA0→0x5555, then data byte to target address PA. The internal program state machine completes in ~10–20 µs per byte. DQ7 data polling: read target address; when DQ7 matches the written bit, done. DQ5 high = timeout indication.
Citation: `datasheets/0x06-FLASH-AMD-ALT/SST39SF040.pdf` p.7 §Byte-Program Operation.

**Erase model:** Sector erase (6-cycle sequence ending with 0x30→sector address) or chip erase (6-cycle sequence ending with 0x10→0x5555). Chip erase time: ~100 ms (SST39SF040: 100 ms max; AM29F040: 32 sectors × ~25 ms). See INV-09 (SST39SF040 keep-Flash/EEPROM in §3) for the `FLAG_CAN_ERASE` / `electrical.type` classification invariant that gates the erase path for 0x06 chips.
Citation: `datasheets/0x06-FLASH-AMD-ALT/SST39SF040.pdf` p.8 §Chip-Erase Operation.

**VPP behavior:** None required (5V-only operation). No VPP regulator use for this bucket. The 12V VPP value appearing in some DB records is a legacy minipro artifact — not used electrically by `configure_flash_nor_unlock()`.
Citation: `datasheets/0x06-FLASH-AMD-ALT/SST39SF040.pdf` p.4 §DC Characteristics (VCC = 4.5–5.5V).

**Pin roles:** 32-pin DIP. Address A0–A18, data D0–D7, CE, OE, WE. AMD unlock command addresses (0x5555/0x2AAA) are A14-don't-care on 512KB space — firmware uses 0x5555/0x2AAA consistently.

---

### 1.3 — 0x07 PROTO_EPROM_28PIN: 28-pin UV-EPROM / EE-EPROM, 13 V VPP

**Folder slug (col 1):** `0x07-EPROM-STD`
**Canonical name (col 2):** `PROTO_EPROM_28PIN` — EPROM — 28-pin UV/EE, 13V VPP
**Handler:** `configure_eprom()` → `eprom.cpp`
**DB chip count:** 170 (AM27Cxxx, 27Cxxx, W27C512, W27E512, ST M27C512, AT27xxx series)

**Write algorithm:** Pulse width is a **database datum**, read from `handle->pulse_delay` on every
write path — never a protocol constant. 1000 µs is only the `pulse_delay == 0` fallback
(`eprom.cpp:71-76`); the row's modal database value is 100 µs, across 113 of its 170 chips. Set
address and data, assert CE (PGM) low for `pulse_delay` µs, de-assert CE, read back and verify; on
mismatch, increment the retry counter and repeat, up to `max_pulses = 25` (Winbond W27C512 and ST
M27C512 flowcharts — Microchip 27C512A specifies 10; 25 is the maximum across the row's cited
datasheets, chosen so no compliant part is refused by a backstop set too low). **No overprogram
pulse is applied on this row** (`overprogram_factor = 0`): Winbond W27C512 Rev A4's "SMART
PROGRAMMING ALGORITHM 2" flowchart has no overprogram step; ST M27C512 Rev 3 §2.6 states, verbatim,
"No overprogram pulses are applied since the verify in MARGIN MODE provides the necessary margin";
Microchip 27C512A DS11173G §1.6 agrees. **Named, scoped divergence (F-140-05):** the 22 Intel-family
1 ms parts on this row (Intel 2764/2764A/27128/27128A/27512, TI TMS2764, NEC UPD2764, ST M2764A and
the rest of the 1000 µs sub-population) genuinely want a `3 x N` margin pulse; serving them correctly
requires splitting `0x07` into a second dispatch key, which TABLE-05's single-dispatch-key
constraint forbids this milestone — recorded as a Phase 146 follow-up, not silently dropped. The
firmware's shipped loop (`eprom.cpp:449-478`) is instead a **per-byte pulse-to-verify loop**:
`eprom_internal_program_pulse()` asserts the program-voltage route, waits `EPROM_VPP_SETUP_US`
(1000 µs) before the strobe and `EPROM_VPP_HOLD_US` (100 µs) after it, then releases the route; the
byte is re-read and, on mismatch, the same fixed-width pulse (never grown) repeats until it converges
or `max_pulses` (25 on this row) is exhausted, at which point the byte fails as `MSG_ERR_MAX_PULSES`
(0xBD) — not an Intel 3N margin pulse. This row's `verify_mode` column (`eprom_params.cpp`) is
`VERIFY_PER_PULSE_PLUS_FINAL`, so convergence of the per-byte loop is followed by one additional
full-array verify pass. Full per-value attribution:
`tests/golden/eprom_params_citations.json` (Phase 140 / TABLE-04).
Citation: Winbond W27C512 Data Sheet, Rev A4 (Nov 1999), "SMART PROGRAMMING ALGORITHM 2" flowchart
(this document has no §6.2 — do not cite one); ST M27C512 datasheet, Rev 3 (May 2007), §2.6 / Fig. 4;
Microchip 27C512A DS11173G (2004), §1.6; shipped loop `eprom.cpp:449-478`,
`eprom_internal_program_pulse()` `eprom.cpp:247-253`, settle constants `include/eprom.h:166-167`.

**Erase model:** UV light erasure for UV-EPROM variants (no electrical erase). Electrically-erasable 0x07 EE-EPROMs (W27C512, W27E512, SST27SF512) are erased via `eprom_internal_erase()` which applies VPE to A9 pin. `FLAG_CAN_ERASE` is derived from `electrical.type == "EEPROM"` (Phase 77 fix) and must be set for auto-erase-before-write.
Citation: `datasheets/0x07-EPROM-STD/W27C512.pdf` p.9 §6.4 Erase Operation.

**VPP behavior:** VPP = 12.5–13V via `CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE` (Rev 2+ drop path). The regulator produces VPE (~2V above VPP); `CTRL_VPP_VPE_DROP_ENABLE` (0x100 on Rev 2) drops it through a resistor divider to reach the 13V VPP level. VPP reaches the chip in one of two ways. A 28-pin part with VPP on pin 1 (`DIP28_2764`, `DIP28_27256`) gets it from the pin-1 VPP switch, and that switch reaches chip pin 1 (socket pin 3) only through a jumper. On Rev 0/1, JP3 at "28pin" connects it to socket pin 3. On Rev 2.x, the switch output goes through the bridged JP5 to socket pin 1, and JP4 joins socket pin 1 to socket pin 3. On Rev 2.0/2.1 that is JP4 "Closed", which comes from the schematic and is not measured. On Rev 2.2/2.3 it is the JP4 28-pin pole, which was measured on the v1.37 Phase 182 bench (2026-09-10). A part with VPP on OE (`DIP28_27512`, pin 22) gets VPP through the OE switch and needs no VPP jumper. `firestarter info` prints the jumper setting for each pin map. See INV-05 (VPP-skip-on-read in §3): VPP is NOT enabled for the read path.
Citation: `datasheets/0x07-EPROM-STD/W27C512.pdf` p.5 §5 Pin Description (pin 1 = VPP, 28-pin).

**Pin roles:** 28-pin DIP. A0–A15, D0–D7, CE (pin 20), OE/VPP (pin 22 on CMOS 27C variants — shared OE/PGM, but 0x07 is handled via the CE path). A part with VPP on pin 1 needs the jumper setting in **VPP behavior** above. A part with VPP on OE needs no VPP jumper. Chip ID via A9 VPP (read manufacturer/device ID by raising A9 to VPP level via `CTRL_VPP_A9_ENABLE`).

**Host pulse-override:** The per-run pulse width can be overridden from the host via
`firestarter write --pulse-us N` (1–65535 µs). That bound is **minipro parity** — `-o pulse=N` is a
`uint16` — and is **not** a wire-type or hardware limit: `pulse-delay` is parsed by the unclamped
`extract_long` macro chain (`json_parser.c:279-282`, invoked at `:305`) into an unclamped `uint32_t`,
so a value above 65535 is reachable on the wire independently of the host flag. The firmware-side
backstop is the pre-flight, per-byte energy-budget refusal in `configure_eprom()`
(`eprom.cpp:106-108`), `MSG_ERR_PULSE_TOO_WIDE` (0xAE); this row ships `energy_cap_us = 0` (uncapped,
`eprom_params.cpp`), so that refusal is structurally unreachable here.
Citation: `firestarter_app/firestarter/cli_handlers.py:568-578` (option help text); `json_parser.c:279-282,305`; `eprom.cpp:106-108`.

**Program-VCC ceiling (accepted debt):** The raised program-VCC all four vendor algorithms assume
for threshold margin — the ~6.25 V ceiling named below — is unreachable on this shield, which has no
VCC-raise path (`include/eprom_params.h`'s `verify_mode` header comment). This milestone buys timing,
pulse-count and verify fidelity and **not** silicon-margin fidelity; it is hardware-bound and
recorded here rather than attempted.
Citation: `include/eprom_params.h:32-34`.

---

### 1.4 — 0x08 PROTO_EPROM_32PIN: 32-pin UV-EPROM / EE-EPROM, 13 V VPP

**Folder slug (col 1):** `0x08-EPROM-QUICK`
**Canonical name (col 2):** `PROTO_EPROM_32PIN` — EPROM — 32-pin UV/EE, 13V VPP
**Handler:** `configure_eprom()` → `eprom.cpp`
**DB chip count:** 127 (AM27C010, AM27C020, AM27C040, W27C020, AT27C010 series — 1 Mbit–8 Mbit)

**Write algorithm:** Family: Intel Quick-Pulse / AMD Flashrite / ST PRESTO II. Pulse width is a
**database datum**, not a protocol constant — 100 µs is the modal database value (104 of 127 chips)
and is only the `pulse_delay == 0` fallback in firmware (`eprom.cpp:71-76`; see INV-06 pulse-delay
defaults in §3). `max_pulses = 25`, from the Winbond W27C020 and ST M27C1001 flowcharts; the row's
representative part, AMD Am27C020, states only "until it verifies or the maximum is reached"
without giving a number. **No overprogram pulse is applied on this row** (`overprogram_factor = 0`),
resolved from three independent vendors: ST M27C1001 §2.6, verbatim, "No overprogram pulse is
applied since the verify in Margin mode provides necessary margin to each programmed cell"; AMD
Am27C020's Flashrite description; Winbond W27C020's flowchart — all three independently omit an
overprogram step. This agrees with `PROJECT.md`'s prose ("not for Quick-Pulse / Flashrite / PRESTO,
so it is gated per row") and **contradicts `PROJECT.md`'s own throughput table**, which gives 0x08 a
`3 x N x pulse` overpulse (D-06, named in the Phase 140 record; not edited here). 32-pin format adds
address lines A16–A18 via CONTROL_REGISTER bits. See INV-03 (0x08 P1-as-VPP in §3) for the VPP
routing distinction vs 0x07.
Citation: Winbond W27C020 (Preliminary) datasheet, "SMART PROGRAMMING ALGORITHM" flowchart; ST
M27C1001 datasheet, Fig. 5; AMD Am27C020 datasheet (FINAL), Flashrite description. See
`tests/golden/eprom_params_citations.json` for the full per-cell citation.

**Erase model:** UV light erasure for UV-EPROM variants. Some 0x08 parts (W27C020, W27E040) are electrically erasable EE-EPROMs — `FLAG_CAN_ERASE` applies identically to 0x07. AM27C020 write failures (0-bits-programmed) are an open defect (FUT-06); the 0x08 write/VPP path on 32-pin Large EPROM is under investigation.

**VPP behavior:** VPP = 12–13V via `CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE` (same as 0x07). See INV-03: for 0x08, VPP is routed to socket pin 1 directly via `CTRL_VPP_P1_ENABLE (0x08)` — this is the P1-as-VPP path, distinct from the 0x07/0x0B paths.
Citation: `datasheets/0x08-EPROM-QUICK/W27C020.pdf` p.4 §Pin Description (pin 1 = VPP, 32-pin DIP).

**Pin roles:** 32-pin DIP. A0–A18 (full 19-bit address, CONTROL_REGISTER carries A16–A18), D0–D7, CE, OE, PGM/WE. VPP on pin 1 (P1_VPP_ENABLE).

**Host pulse-override:** The per-run pulse width can be overridden from the host via
`firestarter write --pulse-us N` (1–65535 µs). That bound is **minipro parity** — `-o pulse=N` is a
`uint16` — and is **not** a wire-type or hardware limit: `pulse-delay` is parsed by the unclamped
`extract_long` macro chain (`json_parser.c:279-282`, invoked at `:305`) into an unclamped `uint32_t`,
so a value above 65535 is reachable on the wire independently of the host flag. The firmware-side
backstop is the pre-flight, per-byte energy-budget refusal in `configure_eprom()`
(`eprom.cpp:106-108`), `MSG_ERR_PULSE_TOO_WIDE` (0xAE); this row ships `energy_cap_us = 0` (uncapped,
`eprom_params.cpp`), so that refusal is structurally unreachable here, the same as the 0x07 row above.
Citation: `firestarter_app/firestarter/cli_handlers.py:568-578` (option help text); `json_parser.c:279-282,305`; `eprom.cpp:106-108`.

**Program-VCC ceiling (accepted debt):** The raised program-VCC all four vendor algorithms assume
for threshold margin — the ~6.25 V ceiling named below — is unreachable on this shield, which has no
VCC-raise path (`include/eprom_params.h`'s `verify_mode` header comment). This milestone buys timing,
pulse-count and verify fidelity and **not** silicon-margin fidelity; it is hardware-bound and
recorded here rather than attempted.
Citation: `include/eprom_params.h:32-34`.

---

### 1.5 — 0x0B PROTO_EPROM_24PIN: 24-pin UV-EPROM, 12–25 V Direct-VPE Rail

**Folder slug (col 1):** `0x0B-EPROM-LEGACY`
**Canonical name (col 2):** `PROTO_EPROM_24PIN` — EPROM — 24-pin legacy, 12–25V direct-VPE rail
**Handler:** `configure_eprom()` → `eprom.cpp`
**DB chip count:** 32 (2716, 2732, 2732A, ETC2716, and small 24-pin EEPROMs)

**Write algorithm:** TI TMS 2516-25/35/45 JL (December 1979, revised May 1982) specifies a **single
50 ms pulse per location** (`t_w(PR)` = 45 / **50** / 55 ms), permits verification immediately after
each location, and specifies **no final full-array pass** and **no overprogram**. The firmware ships
a looped pulse-verify with a **per-byte accumulated-energy cap of 50 ms** (`energy_cap_us`),
satisfying both readings (milestone D-02). 500 µs is only the `pulse_delay == 0` fallback
(`eprom.cpp:71-76`; see INV-06 in §3). **Recorded, not applied here (F-140-07):** the justification
published for the 50 ms figure — "100 x 500 µs is the classic 2716 total programming time" — is
factually wrong: this same TI TMS 2516 datasheet states its own total programming time for all bits
is **100 seconds**, and 50 ms is the per-location pulse width, not a total. The **value** (50000 µs)
has a genuine primary datasheet basis; the **reason** published for it does not. Phase 146 / CLOSE-04
reconciles the posted text; this phase records the correction without editing it. The VPP pin
is pin 21 on the 2716 and 2532. On the 2732, VPP shares OE on pin 20 (`firestarter_app` `pinouts.json`). A13 is hardwired
high for 24-pin socket mode (MSB register bit 5 = `ADDRESS_LINE_13`).
Citation: TI TMS 2516 datasheet ("TMS 2516-25/35/45 JL"), December 1979 (revised May 1982), AC
"recommended timing requirements for programming" table, parameter `t_w(PR)`, and p.138 "start
programming" / p.139 "program verification". See `tests/golden/eprom_params_citations.json` for the
full per-cell citation.

**Erase model:** UV light erasure only for UV-EPROM variants (2716, 2732, 2732A, 2516). Small 24-pin EEPROMs in this bucket (AT28C04, 28C16) erase via `eprom_internal_erase()` applying VPE to A9 pin.

**Hardware reference for this bucket:** the small 24-pin EEPROMs need a DIP24-to-DIP32
adapter before they fit the socket — see [Pin Maps](Pin-Maps) for the pin map, the reroute
the adapter makes, and the 5 V-only guarantee that applies to these parts.

**VPP behavior:** VPP = 12–25V via `CTRL_VPP_REGULATOR_ENABLE` ONLY — the direct-VPE rail (no drop resistor). See INV-01 (0x0B direct-VPE rail in §3): unlike 0x07/0x08, 0x0B uses `FLAG_VPE_AS_VPP` to apply VPE directly without `CTRL_VPP_VPE_DROP_ENABLE`. The RURP trimpot must be set to the target voltage before programming. NMOS variants (Intel 2716, 2732) historically required 25V — RURP is physically capable of this via the adjustable regulator; firmware warns on under-voltage and proceeds (Phase 79 operator override D-07, best-effort).
Citation: `datasheets/0x0B-EPROM-LEGACY/2516_EPROM.pdf` p.2 §Vpp Programming Voltage.

**Pin roles:** 24-pin DIP. A0–A12 (no A13 — hardwired), D0–D7, CE, OE, VPP (pin 21 on the 2716 and 2532; pin 20, shared with OE, on the 2732). Pin 21 of a 24-pin part is socket pin 25. Only the JP4 24-pin pole on Rev 2.2/2.3 connects the pin-1 VPP switch to socket pin 25. Rev 0 to Rev 2.1 have no path from a VPP switch to socket pin 25. The RURP firmware calculates the 24-pin MSB register value specially in `mem_util_calculate_msb_register()`.

**Host pulse-override:** The per-run pulse width can be overridden from the host via
`firestarter write --pulse-us N` (1–65535 µs). That bound is **minipro parity** — `-o pulse=N` is a
`uint16` — and is **not** a wire-type or hardware limit: `pulse-delay` is parsed by the unclamped
`extract_long` macro chain (`json_parser.c:279-282`, invoked at `:305`) into an unclamped `uint32_t`,
so a value above 65535 is reachable on the wire independently of the host flag. The firmware-side
backstop is the pre-flight, per-byte energy-budget refusal in `configure_eprom()`
(`eprom.cpp:106-108`), `MSG_ERR_PULSE_TOO_WIDE` (0xAE); this row ships `energy_cap_us = 50000` (50 ms,
`eprom_params.cpp`), so — unlike the 0x07/0x08 rows above, where the refusal is unreachable — a
`--pulse-us` value above 50000 is refused here before any high voltage is enabled.
Citation: `firestarter_app/firestarter/cli_handlers.py:568-578` (option help text); `json_parser.c:279-282,305`; `eprom.cpp:106-108`.

**Program-VCC ceiling (accepted debt):** The raised program-VCC all four vendor algorithms assume
for threshold margin — the ~6.25 V ceiling — is unreachable on this shield, which has no VCC-raise
path (`include/eprom_params.h`'s `verify_mode` header comment). This milestone buys timing,
pulse-count and verify fidelity and **not** silicon-margin fidelity; it is hardware-bound and
recorded here rather than attempted.
Citation: `include/eprom_params.h:32-34`.

---

### 1.6 — 0x0D PROTO_EEPROM_PARALLEL: 5 V Parallel EEPROM, SDP + DQ7 Page Poll

**Folder slug (col 1):** `0x0D-EEPROM-POLL`
**Canonical name (col 2):** `PROTO_EEPROM_PARALLEL` — EEPROM — 5V parallel, SDP + DQ7 page poll
**Handler:** `configure_eeprom28c()` → `eeprom_28c.cpp`
**DB chip count:** 84 (AT28C010, AT28C040, X28C010, M28010, CAT28C series, WE series)

**Write algorithm:** Page write (64–128 bytes per page, all within tBLC = 150 µs inter-byte window). Before every write, firmware automatically emits a 6-cycle SDP-disable sequence (0xAA→0x5555, 0x55→0x2AAA, 0x80→0x5555, 0xAA→0x5555, 0x55→0x2AAA, 0x20→0x5555) on the protocol-`0x0D`-local emitter, reporting one unconditional INFO line before the sequence and a second carrying the `micros()`-measured emission duration after it (Phase 117/118). The user can decline the emission with `firestarter write --skip-sdp-unlock` (Phase 120), in which case the write proceeds without it. **The SDP protection state is not readable, before or after** — a successful emission proves only that the sequence was sent over the bus, never that the part's protection was actually enabled beforehand or actually disabled afterward; nothing about a part's protection state may be inferred from it. After the last data byte, the chip's internal write cycle fires (~5–10 ms), confirmed by DQ7 data polling.
Citation: `datasheets/0x0D-EEPROM-POLL/AT28C256.pdf` p.6 §Software Data Protection; p.7 §Page Write.

**Erase model:** Firmware implements a **standalone chip erase** for protocol `0x0D` (Phase 153, ERASE-03/ERASE-04), reachable as `firestarter erase`, dispatched through a `CMD_ERASE` arm in `configure_eeprom28c()`'s handler (`eeprom28c_erase_execute` in `eeprom_28c.cpp`). The mechanism is the **software** six-byte load sequence from Atmel Application Note *Software Chip Erase*, Rev. 0544B-10/98: six bus writes drive every byte in the device to `0xFF`, the device internally times the erase cycle (t_EC, 20 ms max) so no completion poll is issued or permitted, and software data protection remains **enabled** after the erase completes — this operation does not lock or unlock SDP as a side effect of erasing. Per `D-153-03`, the datasheet's separate **hardware** Chip Erase mode — which drives 12 V onto the OE pin on this 28-pin layout — is **deliberately not implemented**; this handler energises no programming rail of any kind, consistent with the VPP-behavior paragraph below. Per `D-153-02`, the erase is prefixed with the same SDP-disable sequence the write path emits (reusing the existing SDP-unlock operation verbatim), because the application note is silent on whether the six-byte erase code is decoded on a protected part and this family's SDP protection state is not readable either way — a phantom erase in the silent-failure direction would report success having erased nothing, and no oracle could ever catch it after the fact; the cost of disabling SDP first on an already-unprotected part is six harmless extra bus writes. Per `D-153-04`, the erase is device-global by construction — the AN 0544B sequence erases the whole part in one operation — so `erase --sector-address` does not apply to it. `erase --blank-check` now works on this protocol, where it never did before: the device-side blank-check machinery left the firmware in `3.1.0` (v1.41 phase 205), and its host-side replacement is protocol-agnostic, so a family the firmware's own pre-flight never reached gets a post-erase verdict anyway — a behaviour **gain**, not a documented no-op. `blank` remains a host-side read-and-compare (v1.41 phase 202/204) and sends no blank-check ordinal at all. The write path (`eeprom28c_write_init`) performs **no blank check at all** on this protocol — unlike the family's guarded protocols, this write's own init never gated a blank check behind the skip flag in the first place, so `-b`/`--no-blank-check` is unread here, and nothing about writing a non-blank AT28C part depends on passing it. The host's general erase-capability advertisement (`FLAG_CAN_ERASE`) is now **set** for all 84 chips in this bucket, and the host and the firmware agree. **Honesty statement:** this erase ships **software-proven and unvalidated on silicon** — no AT28C part was involved in developing or verifying it. Native full-stream-equality tests prove the six-byte sequence is emitted correctly on the bus, positionally, against the tree's own tables; nothing here claims that any physical part actually erases.
Citation: `datasheets/0x0D-EEPROM-POLL/AT28C256.pdf` p.6 §Software Data Protection; Atmel Application Note *Software Chip Erase*, Rev. 0544B-10/98 (doc0544.pdf).

**VPP behavior:** None (5V internal charge pump). No VPP regulator involvement. Some older SDP-locked parts may have a 12V SDP-bypass path, but the Firestarter path uses software SDP-disable.
Citation: `datasheets/0x0D-EEPROM-POLL/AT28C256.pdf` p.4 §DC Characteristics (VCC = 5V).

**Pin roles:** 32-pin DIP. A0–A17, D0–D7, CE, OE, WE. DQ7 data poll: read last-written address; `(data & 0x80) == (expected & 0x80)` → done. DQ6 toggle: alternates during write cycle.

---

### 1.7 — 0x0E PROTO_SRAM_32PIN: 32-pin Battery-Backed NVRAM

**Folder slug (col 1):** `0x0E-SRAM-32PIN`
**Canonical name (col 2):** `PROTO_SRAM_32PIN` — SRAM — 32-pin battery-backed NVRAM, optional 12V write-protect bypass
**Handler:** `configure_sram()` → `sram.cpp`
**DB chip count:** 20 (DS1245Y, DS1249AB, M48T128Y, BQ4013YMA, and sibling series)

**Write algorithm:** Standard SRAM read/write — no programming protocol. Write: assert address, assert CE+WE low, present data, de-assert WE high (data latches on rising WE edge), de-assert CE. Read: assert address, assert CE+OE low, read data within tACC.
Citation: `datasheets/0x0E-SRAM-32PIN/DS1245Y.pdf` p.3 §Pin Description.

**Erase model:** None — SRAM: contents are volatile or battery-backed. No erase step.

**VPP behavior:** Optional 12V via `CTRL_VPP_P1_ENABLE (0x08)` to bypass the Dallas write-protect circuit (applies 12V to pin 1 or pin 31 depending on device). Without this, Dallas NVRAM write-protect logic (powered by the internal battery) may block writes. M48Txx timekeeping chips do not require this.
Citation: `datasheets/0x0E-SRAM-32PIN/DS1245Y.pdf` p.5 §Write-Protect Override.

**Pin roles:** 32-pin DIP. A0–A18, D0–D7, CE, OE, WE. Write-protect pin varies by device (consult per-chip DB entry). The RURP `configure_sram()` handler uses the bus-config `vpp-pin` field from `chip_database.json` to derive the correct P1/A9 routing.

---

### 1.8 — 0x10 PROTO_FLASH_INTEL: Intel 28F Command-Register NOR Flash, 12 V VPP Mandatory

**Folder slug (col 1):** `0x10-FLASH-INTEL`
**Canonical name (col 2):** `PROTO_FLASH_INTEL` — Flash — Intel 28F command-register, 12V VPP mandatory
**Handler:** `configure_flash_intel()` → `flash_intel.cpp`
**DB chip count:** 39 (Intel 28F010/256/512, AM28F010, P28F010, TMS28F010, SST28SF040 series)

**Write algorithm:** Command-register architecture (no address-based unlock). Byte program: write 0x40 (Setup Program) to any address, write data byte to PA, wait ~10 µs, write 0xC0 (Program Verify) to any address, read PA, compare. On mismatch: write 0x00 (Reset), retry. Maximum 25 write-verify cycles per byte.
Citation: `datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf` p.9 §Quick-Pulse Programming Algorithm.

**Erase model:** Bulk chip erase only (no sector erase on original 28F010). Pre-condition: all bytes must be pre-programmed to 0x00 before erase. Erase: write 0x20 (Erase Setup) twice to any address, wait ~12 ms, write 0xA0 (Erase Verify), read all bytes (expect 0xFF). On failure: write 0x00, retry up to 1000 iterations.
Citation: `datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf` p.10 §Quick-Erase Algorithm.

**VPP behavior:** 12V VPP MANDATORY via `CTRL_VPP_P1_ENABLE (0x08)` — VPP ≤ 6.5V inhibits all program and erase operations. The RURP `CTRL_VPP_REGULATOR_ENABLE` must be set to hold VPP high during the entire program/erase cycle.
Citation: `datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf` p.6 §VPP Characteristics (VPP < 6.5V = write inhibit).

**Pin roles:** 32-pin DIP. Address A0–A19, D0–D7 (bidirectional), CE, OE, WE, VPP on pin 1. The 0x40/0xC0/0x20/0xA0/0x00/0xFF command sequence is written to the chip's command register — any address suffices. Unlike the unlock-sequence NOR path (0x06 / `PROTO_FLASH_NOR_UNLOCK`), there is no address-based unlock; commands go directly.

---

### 1.9 — 0x27 PROTO_SRAM_24PIN: 24-pin Async SRAM, 5 V

**Folder slug (col 1):** `0x27-SRAM-24PIN`
**Canonical name (col 2):** `PROTO_SRAM_24PIN` — SRAM — 24-pin async, 5V
**Handler:** `configure_sram()` → `sram.cpp`
**DB chip count:** 2 (6116 / 2K×8, DS1220(TEST))

**Write algorithm:** Standard SRAM read/write. Same as 0x0E. CE+WE pulsed for write; CE+OE for read.
Citation: `datasheets/0x27-SRAM-24PIN/6116.pdf` p.3 §Pin Description.

**Erase model:** None — volatile SRAM.

**VPP behavior:** None (5V SRAM).

**Pin roles:** 24-pin DIP. JEDEC 6116 pinout: A0–A10 on pins 8–1 and 19–23, D0–D7 on pins 9–11/13–17, /WE on pin 21, /OE on pin 20, /CS on pin 18. The 24-pin socket requires A13 hardwired high via MSB register bit 5 (same `ADDRESS_LINE_13` mode as 0x0B).
Citation: `datasheets/0x27-SRAM-24PIN/6116.pdf` p.2 §Pin Configuration.

---

### 1.10 — 0x28 PROTO_SRAM_28PIN: 28-pin SRAM / FRAM (NAME-04: FM1608 SRAM→FRAM correction)

**Folder slug (col 1):** `0x28-SRAM-STD`
**Canonical name (col 2):** `PROTO_SRAM_28PIN` — SRAM/FRAM — 28-pin, 5V (see NAME-04 call-out below)
**Handler:** `configure_sram()` → `sram.cpp`
**DB chip count:** 34 (W24256, W2464, DS1225, BQ4011YMA, 6264, 62256, FM1608, and siblings)

**Write algorithm:** Standard SRAM read/write. Same CE+WE/CE+OE bus cycle as 0x0E/0x27.
Citation: `datasheets/0x28-SRAM-STD/FM1608.pdf` p.4 §Write Timing.

**Erase model:** None — volatile or battery-backed SRAM; FRAM contents persist without power.

**VPP behavior:** None (5V SRAM/FRAM). Optional 12V write-protect bypass applies to Dallas NVRAM siblings in this bucket (same as 0x0E).

**Pin roles:** 28-pin DIP. A0–A14, D0–D7, CE, OE, WE. Standard 62256/6264 JEDEC SRAM pinout.

**NAME-04 call-out — FM1608 identity correction:**

The FM1608 (Ramtron ferroelectric RAM) is correctly classified as `0x28 SRAM_STD / FRAM`
in Phase 86. Its true `infoic.xml` identity tuple is:

```
type=4 / proto=0x07 / variant=0x4126  →  resolved to SRAM_STD (0x28) / electrical.type FRAM
```

The `variant` high byte `0x41` is the Ramtron FRAM class discriminator (minipro `database.c`
variant decode, Phase 86 `86-CONTEXT.md`). The algorithm axis is `0x28` (SRAM_STD), NOT `0x07`
(EPROM_STD) — the `proto=0x07` in the raw XML is overridden by the variant decode.

**Historical conflation retired:** The recurring phrase "FM1608 algorithm 40 = 0x28" in old
planning notes was a **decimal-40 ↔ hex-0x28 conflation** (decimal 40 IS hex 0x28 — they are
the same number). The older framing obscured the fact that the raw XML proto is `0x07`, not
`0x28`. The ground truth is: FM1608 proto=0x07+variant=0x4126 → dispatches to `configure_sram()`
via the variant-decode rule that maps this tuple to algorithm=0x28. This conflation is now
**retired**. See INV-07 (FM1608 SRAM→FRAM in §3) for the runtime invariant.

---

### 1.11 — 0x29 PROTO_SRAM_32PIN_NVRAM: 32-pin Large Battery-Backed NVRAM

**Folder slug (col 1):** `0x29-SRAM-512K-1M`
**Canonical name (col 2):** `PROTO_SRAM_32PIN_NVRAM` — SRAM — 32-pin large battery-backed NVRAM, 512K–1M
**Handler:** `configure_sram()` → `sram.cpp`
**DB chip count:** 20 (DS1245AB(TEST), DS1249AB(TEST), DS1250AB(TEST), BQ4013YMA(TEST), M48T128Y(TEST) series)

**Write algorithm:** Same SRAM read/write as 0x0E. These are the same physical chips as the 0x0E bucket but listed with `(TEST)` name suffix in the minipro DB — the test-mode variant uses built-in memory test patterns rather than externally loaded data. At the RURP hardware level, read/write cycles are identical.
Citation: `datasheets/0x29-SRAM-512K-1M/DS1245Y.pdf` p.3 §Functional Description (DS1245Y is a sibling substitute; see datasheets/README.md §D-02 note on DS1250Y substitution).

**Erase model:** None — SRAM/NVRAM.

**VPP behavior:** Optional 12V write-protect bypass (same as 0x0E, Dallas battery-backed series).

**Pin roles:** 32-pin DIP. A0–A18, D0–D7, CE, OE, WE. Same socket as 0x0E.

---

### 1.12 — 0x34 PROTO_EEPROM_8051BUS: XICOR 8051-Bus EEPROM, PCB-blocked (FUT-01) (NAME-04 correction)

**Folder slug (col 1):** `0x34-EEPROM-X88C64`
**Canonical name (col 2):** `PROTO_EEPROM_8051BUS` — EEPROM — XICOR 8051-bus (PCB-blocked, document-only)
**Handler:** `configure_not_implemented()` → `not_implemented.cpp`
**DB chip count:** 1 (X88C64P)

**Write algorithm:** XICOR X88C64P is a 5V DIP-24 serial EEPROM using an 8051-multiplexed address/data bus (ALE/WR/RD). Page write via toggle-bit (I/O6) polling. Requires ALE pin routing on the RURP control bus — investigation showed the control register is fully allocated with no free 74HC573 strobe for ALE (Phase 78, verdict: PCB-BLOCKED HIGH). No handler is committed; this bucket returns `MSG_ERR_PROTOCOL_NOT_IMPLEMENTED (0xBB)`.
Citation: `datasheets/0x34-EEPROM-X88C64/X88C64.pdf` (Xicor 1990 Data Book) §X88C64 Programming.

**Erase model:** Internal auto-erase (byte-erase before write). Not relevant for the current not-implemented disposition.

**VPP behavior:** None (5V EEPROM, internal charge pump). The `CTRL_VPP_*` bits are not used.

**Pin roles:** 24-pin DIP. 8051-multiplexed ALE/WR/RD bus — NOT a standard JEDEC parallel address+data bus. This pin multiplexing is the root cause of the PCB-blocked verdict (FUT-01).

**NAME-04 call-out — X88C64 EEPROM identity correction:**

The X88C64 is correctly classified as `electrical.type EEPROM` in Phase 86. Its true
`infoic.xml` identity tuple is:

```
type=1 / proto=0x34 / variant=0x3100 / flags=0x00414200
```

`flags & 0x10 == 0` — the "electrically erasable" bit is NOT set, which caused the pre-Phase-86
build to classify it as `UV-EPROM`. The Phase-86 variant decode corrects this by using
`electrical.type = "EEPROM"` derived from proto `0x34` (XICOR bus EEPROM class discriminator)
rather than the flags bit. The host remains the authoritative safety layer — `support_status:
protocol-not-implemented` prevents any write dispatch before a serial byte is sent.

**Deferral (FUT-01):** PCB-blocked. A6 ALE-routing requires a free 74HC573 strobe that is
not available on current RURP Rev 2.x hardware. No handler will be committed until the PCB
constraint is resolved. Do NOT plan handler work here.

---

## 2. Honest non-protocols

The protocol IDs below appear in the firmware dispatch chain but are **NOT real programming
protocol buckets** — they have zero chips in `chip_database.json` and are not feasible on RURP
hardware. They are named here as non-protocols, not as buckets with documented behavior.

### 2.1 — Phantom Buckets (dispatched-but-dead)

These IDs appear in the `configure_flash_5v_page()` dispatch for forward-compatibility but have zero
DB chips. The host excludes both from `KNOWN_PROTOCOLS` and routes any chip that somehow
reaches them to `not_implemented`.

| hex | firmware name | reason |
|-----|---------------|--------|
| `0x35` | `PROTO_PHANTOM_0x35` | `IC2_ALG_ITE` is an ITE EC microcontroller label in minipro, NOT a memory programming algorithm. Zero chips in `chip_database.json`. Firmware dispatch preserved for forward-compat. |
| `0x39` | `PROTO_PHANTOM_0x39` | No `IC2_ALG` constant exists for this value in minipro source. Zero chips in `chip_database.json`. Firmware dispatch preserved for forward-compat. |

These are not 5V page-write flash (`PROTO_FLASH_5V_PAGE`) variants, EPROM variants, or any
other real protocol. They are dead dispatch arms. An old description of 0x35 as "AT29C
series" and 0x39 as "AT49F series" was pre-Phase-86 speculation about minipro intent — the
Phase-86 DB regeneration confirmed zero DB chips for both.

### 2.2 — Infeasible Buckets (fail-closed on RURP hardware)

These protocols are infeasible on the RURP parallel bus shield. The firmware routes them to
`configure_not_implemented()` (Phase 64, DISP-04), returning `MSG_ERR_PROTOCOL_NOT_IMPLEMENTED
(0xBB)` with zero hardware side effects. There are no DB chips under these IDs in
`chip_database.json`.

| hex | minipro name | reason infeasible |
|-----|-------------|------------------|
| `0x11` | FWH/LPC serial flash | FWH / LPC-bus serial protocol — requires dedicated serial FWH signaling that the RURP parallel bus cannot provide. 3.3V operation; incompatible with RURP. |
| `0x2A` | GAL/PLD (`IC2_ALG_GAL16`) | GAL16V8 PLD — requires high-voltage serial JEDEC programming; RURP parallel bus cannot deliver the required protocol. |
| `0x2B` | GAL/PLD (likely `IC2_ALG_GAL20`) | GAL20V8 PLD — same rationale as 0x2A. The exact `IC2_ALG` constant is unconfirmed from a citeable source (`protocol-id.md` lacks a 0x2B row — a documentation gap carried from Phase 86). |
| `0x2C` | PIC microcontroller (`IC2_ALG_GAL22`) | GAL22V10 PLD or PIC MCU — requires ICSP serial protocol; incompatible with RURP parallel memory bus. |

These are not undiscovered programming opportunities — they are structurally infeasible on the
RURP hardware and will remain fail-closed. Do not confuse them with the PCB-blocked 0x34 (which
is feasible in principle but blocked by a routing constraint).

---

## 3. Invariant Traceability Matrix

This section is the single source of truth for the **INV-01..INV-09 behavioral invariants** —
the one-off protocol behaviors that any firmware recompose (Phases 88/89) must preserve.
Each row maps an INV id to its one-line behavior, the owning handler file, the planned native
test function name that pins it, and the matrix-assigned suite path that hosts it.

**SAFE-02 handoff:** The INV ids are the handoff contract to Phases 88/89. The invariant ids
must survive a recompose grep-intact: one `grep -rn INV-04` must hit this doc row + the native
test function name (delivered by Plan 03) + the owning handler header block (delivered by
Plan 02). Each INV's test MUST live in the suite path this matrix assigns it — not just
anywhere under the native tree.

**Per-INV suite path contract (Plan 03 must honor this mapping):**
- INV-01, INV-02, INV-03, INV-05, INV-06, INV-08 → `test/native/avr/test_val_eprom/`
- INV-04 → `test/native/avr/test_val_5v_page/`
- INV-07 → `test/native/avr/test_val_sram/`
- INV-09 → `test/native/avr/test_val_nor_unlock/`

| INV id | One-line behavior | Owning handler file | Planned native test function name | Suite path |
|--------|-------------------|---------------------|----------------------------------|------------|
| INV-01 | `PROTO_EPROM_24PIN` (0x0B) uses `FLAG_VPE_AS_VPP` direct-VPE rail (no `CTRL_VPP_VPE_DROP_ENABLE` drop) | `eprom.cpp` | `test_inv01_eprom_0x0B_direct_vpe_rail` | `test/native/avr/test_val_eprom/` |
| INV-02 | `PROTO_EPROM_24PIN` (0x0B) read operations skip VPP enable. On `DIP24_2732` VPP shares OE (pin 20), so VPP on a read would conflict with OE. On `DIP24_2716` and `DIP24_2532` VPP is on pin 21 | `eprom.cpp` | `test_inv02_eprom_0x0B_oe_vpp_read_skip` | `test/native/avr/test_val_eprom/` |
| INV-03 | `PROTO_EPROM_32PIN` (0x08) routes VPP to socket pin 1 via `CTRL_VPP_P1_ENABLE` (not drop path) | `eprom.cpp` | `test_inv03_eprom_0x08_p1_as_vpp` | `test/native/avr/test_val_eprom/` |
| INV-04 | `PROTO_FLASH_5V_PAGE` (0x05) 5v_page page size is data-driven from `handle->mem_size` (256B for W29C040 512KB; 128B for 128KB; 64B for 32KB) | `flash_5v_page.cpp` | `test_inv04_5v_page_256b_page_boundary` | `test/native/avr/test_val_5v_page/` |
| INV-05 | `PROTO_EPROM_28PIN` (0x07) — VPP is NOT enabled on the read path — firmware skips VPP init at configure time for `CMD_READ` (VPP-skip-on-read) | `eprom.cpp` | `test_eprom_0x07_read_configure_only_does_not_enable_vpp` | `test/native/avr/test_val_eprom/` |
| INV-06 | Pulse-delay defaults: `PROTO_EPROM_32PIN` (0x08) → 100 µs; `PROTO_EPROM_24PIN` (0x0B) → 500 µs; all other (`PROTO_EPROM_28PIN` (0x07) default) → 1000 µs | `eprom.cpp` | `test_inv06_eprom_pulse_delay_defaults` | `test/native/avr/test_val_eprom/` |
| INV-07 | `PROTO_SRAM_28PIN` — FM1608 routes to `configure_sram()` as SRAM_STD/FRAM (algorithm=0x28), NOT `configure_eprom()` (BLOCKER-2 mitigation) | `sram.cpp` | `test_inv07_sram_fm1608_routes_to_sram` | `test/native/avr/test_val_sram/` |
| INV-08 | `PROTO_EPROM_28PIN` (0x07) — **(dispatch-only scope)** WARNING-5 (0x07 EE-EPROM chips reclassified to 0x0D) is delivered by Phase-86 variant decode — no `build_db.py` runtime override. The correct `electrical.type` flows from the DB and the dispatch chain honors it. The WARNING-5 retirement itself is **host-side** (`build_db.py`, gated by `diff_db.py`) and is NOT firmware-testable; the native test below pins ONLY the downstream firmware consequence — that 0x07 still dispatches to `configure_eprom`. | `eprom.cpp` / build path | `test_inv08_eprom_warning5_decode_preserved` | `test/native/avr/test_val_eprom/` |
| INV-09 | `PROTO_FLASH_NOR_UNLOCK` (0x06) — SST39SF040 retains `electrical.type = Flash/EEPROM` — the `FLAG_CAN_ERASE` + `configure_flash_nor_unlock()` combination must not be confused with the UV-EPROM path | `flash_nor_unlock.cpp` | `test_inv09_nor_unlock_sst39sf040_keep_flash_eeprom` | `test/native/avr/test_val_nor_unlock/` |

### Cross-links to per-bucket sections

- INV-01, INV-02: [§1.5 (0x0B PROTO_EPROM_24PIN)](#15----0x0b-proto_eprom_24pin-24-pin-uv-eprom-12-25-v-direct-vpe-rail)
- INV-03: [§1.4 (0x08 PROTO_EPROM_32PIN)](#14----0x08-proto_eprom_32pin-32-pin-uv-eprom--ee-eprom-13-v-vpp)
- INV-04: [§1.1 (0x05 PROTO_FLASH_5V_PAGE)](#11----0x05-proto_flash_5v_page-5v-page-write-flash-eeprom-like)
- INV-05: [§1.3 (0x07 PROTO_EPROM_28PIN)](#13----0x07-proto_eprom_28pin-28-pin-uv-eprom--ee-eprom-13-v-vpp)
- INV-06: [§1.4 (0x08 PROTO_EPROM_32PIN)](#14----0x08-proto_eprom_32pin-32-pin-uv-eprom--ee-eprom-13-v-vpp), [§1.5 (0x0B PROTO_EPROM_24PIN)](#15----0x0b-proto_eprom_24pin-24-pin-uv-eprom-12-25-v-direct-vpe-rail)
- INV-07: [§1.10 (0x28 PROTO_SRAM_28PIN)](#110----0x28-proto_sram_28pin-28-pin-sram--fram-name-04-fm1608-sramfram-correction)
- INV-08: [§1.3 (0x07 PROTO_EPROM_28PIN)](#13----0x07-proto_eprom_28pin-28-pin-uv-eprom--ee-eprom-13-v-vpp)
- INV-09: [§1.2 (0x06 PROTO_FLASH_NOR_UNLOCK)](#12----0x06-proto_flash_nor_unlock-amdsst-unlock-sequence-nor-flash)

---

*Phase 87 — Naming + Documentation Pass | Authored 2026-06-26*
*Canonical bucket set re-verified from `chip_database.json` (746 chips, 12 real buckets)*
*Datasheets committed in Phase 85; datasheet citation anchors are best-available locators per D-discretion*
*INV-01..INV-09 ids are the SAFE-02 handoff to Phases 88/89 — grep-intact through recompose*
*Phase 100 — Canonical Protocol Name Set (3-field schema: `PROTO_` token + display name + handler-family) | Draft authored 2026-07-01 | Operator-approved 2026-07-01 — final name set: 0x0E/0x29 SRAM collision resolved (`PROTO_SRAM_32PIN` / `PROTO_SRAM_32PIN_NVRAM`), phantom tokens `PROTO_PHANTOM_0x35`/`PROTO_PHANTOM_0x39`, 0x34 `PROTO_EEPROM_8051BUS`; all other names approved as drafted*

<!-- firestarter-claim-stamp: db-sha256-16=ccbc8d2c4866a5af verified=2026-08-31 -->
