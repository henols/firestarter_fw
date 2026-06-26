# Firestarter Protocol Reference

This is the canonical GitHub-visible protocol vocabulary for the Firestarter EPROM programmer
firmware. It maps every `protocol_id` present in `chip_database.json` to its
`datasheets/<hex>-<NAME>/` folder slug (col 1, phase-85 committed, not renamed) and a
descriptive algorithm-axis name (col 2), covering the four NAME-01 behavioral facets per
bucket: write algorithm, erase model, VPP behavior, and pin roles — each datasheet-cited with
an anchored locator. It also documents the Phase-86 FM1608 and X88C64 identity corrections
(NAME-04), names the phantom and infeasible buckets as honest non-protocols, and carries the
INV-01..INV-09 invariant traceability matrix that anchors the SAFE-02 handoff to Phases 88/89.

**Reader router:**

- To look up a protocol bucket by hex ID, see [§1 (real protocol buckets)](#1-real-protocol-buckets).
- To understand the FM1608 SRAM→FRAM identity correction or the X88C64 EEPROM identity correction, read the relevant bucket section inside §1 — the NAME-04 call-outs are there.
- To see why a phantom (0x35/0x39) or infeasible (0x11/0x2A/0x2B/0x2C) protocol ID routes to `configure_not_implemented()`, read [§2 (Honest non-protocols)](#2-honest-non-protocols).
- To find the INV-01..INV-09 traceability matrix (invariants, owning handlers, planned native test function names, suite paths), read [§3 (invariant traceability matrix)](#3-invariant-traceability-matrix).
- For deeper background on the RURP control-register map, per-bucket algorithm prose, and minipro `protocol_id` taxonomy see `.planning/research/PROTOCOLS.md` (research-grade, not the deliverable) and `.planning/v1.13-PROTOCOL-ENUMERATION.md` (12-bucket landscape + erase-scope findings) in the Firestarter meta-repo.

**Canonical bucket set** (re-verified from `chip_database.json` before authoring — DB is authoritative):

| hex | DB chip count | handler | datasheets folder (col 1) | algorithm-axis name (col 2) |
|-----|--------------|---------|---------------------------|----------------------------|
| 0x05 | 27 | `flash_type_4.cpp` | `0x05-FLASH-AMD-STD` | FLASH-AMD-STD — 5V page-write flash (EEPROM-like) |
| 0x06 | 190 | `flash_type_3.cpp` | `0x06-FLASH-AMD-ALT` | FLASH-AMD-ALT — AMD/SST unlock-sequence NOR flash |
| 0x07 | 170 | `eprom.cpp` | `0x07-EPROM-STD` | EPROM-STD — 28-pin UV-EPROM / EE-EPROM, 13 V VPP |
| 0x08 | 127 | `eprom.cpp` | `0x08-EPROM-QUICK` | EPROM-QUICK — 32-pin UV-EPROM / EE-EPROM, 13 V VPP |
| 0x0B | 32 | `eprom.cpp` | `0x0B-EPROM-LEGACY` | EPROM-LEGACY — 24-pin legacy UV-EPROM, 12–25 V VPP direct-VPE rail |
| 0x0D | 84 | `eeprom_28c.cpp` | `0x0D-EEPROM-POLL` | EEPROM-POLL — 5 V parallel EEPROM, SDP + DQ7 page poll |
| 0x0E | 20 | `sram.cpp` | `0x0E-SRAM-32PIN` | SRAM-32PIN — 32-pin battery-backed NVRAM, optional 12 V write-protect bypass |
| 0x10 | 39 | `flash_intel.cpp` | `0x10-FLASH-INTEL` | FLASH-INTEL — Intel 28F command-register NOR flash, 12 V VPP mandatory |
| 0x27 | 2 | `sram.cpp` | `0x27-SRAM-24PIN` | SRAM-24PIN — 24-pin async SRAM, 5 V |
| 0x28 | 34 | `sram.cpp` | `0x28-SRAM-STD` | SRAM-STD — 28-pin SRAM/FRAM (NAME-04: FM1608 SRAM→FRAM, see §1.10) |
| 0x29 | 20 | `sram.cpp` | `0x29-SRAM-512K-1M` | SRAM-512K-1M — 32-pin large battery-backed NVRAM, 512 K–1 M |
| 0x34 | 1 | `not_implemented.cpp` | `0x34-EEPROM-X88C64` | EEPROM-X88C64 — XICOR 8051-bus EEPROM, PCB-blocked (FUT-01) (NAME-04: X88C64 EEPROM, see §1.12) |

---

## 1. Real Protocol Buckets

Each section below gives the NAME-01 four facets (write algorithm, erase model, VPP behavior, pin roles) with datasheet-anchored citations. Datasheet citations use the form `datasheets/<slug>/<file>.pdf p.N §section` wherever page and section are recoverable.

---

### 1.1 — 0x05 FLASH-AMD-STD: 5V Page-Write Flash (EEPROM-like)

**Folder slug (col 1):** `0x05-FLASH-AMD-STD`
**Algorithm-axis name (col 2):** FLASH-AMD-STD — 5V page-write flash (EEPROM-like)
**Handler:** `configure_flash4()` → `flash_type_4.cpp`
**DB chip count:** 27 (AT29C, W29C, SST29EE series)

**Write algorithm:** Optional SDP unlock (3 bus cycles: 0xAA→0x5555, 0x55→0x2AAA, 0xA0→0x5555), then up to 64–256 bytes written sequentially to addresses within the same page. All bytes must complete within tBLC (inter-byte window, typically 100–150 µs). After the last byte, the chip's internal write cycle begins (~5–10 ms). DQ7 data polling confirms completion: read the last written address; when DQ7 matches the written bit, the cycle is done.
Citation: `datasheets/0x05-FLASH-AMD-STD/W29C020.pdf` p.9 §Write Operation; `datasheets/0x05-FLASH-AMD-STD/W29C040.pdf` p.11 §Page Write.

**Erase model:** Implicit auto-erase — each page write erases the target page internally before programming. No explicit chip-level erase command. Chip-level erase is available via a 6-cycle SDP sequence (0x80→0x5555 then 0x10→0x5555) on chips that support it.
Citation: `datasheets/0x05-FLASH-AMD-STD/W29C040.pdf` p.12 §Chip Erase.

**VPP behavior:** None (5V-only operation). The internal charge pump on the chip drives write electricals; the RURP VPP regulator is not used for this bucket.
Citation: `datasheets/0x05-FLASH-AMD-STD/W29C020.pdf` p.3 §Pin Description (VCC = 5V only).

**Pin roles:** 32-pin DIP. Standard JEDEC 27-series pinout extension. Address lines A0–A18, data D0–D7, CE active-low (chip enable), OE active-low (output enable), WE active-low (write enable). Page-size is data-driven from `handle->mem_size` at runtime — see INV-04 (flash4 256B page boundary in §3) for the 0x05/0x0B page-size derivation detail, which applies to W29C040 (512KB → 256B page) and smaller siblings.

---

### 1.2 — 0x06 FLASH-AMD-ALT: AMD/SST Unlock-Sequence NOR Flash

**Folder slug (col 1):** `0x06-FLASH-AMD-ALT`
**Algorithm-axis name (col 2):** FLASH-AMD-ALT — AMD/SST unlock-sequence NOR flash
**Handler:** `configure_flash3()` → `flash_type_3.cpp`
**DB chip count:** 190 (AM29F, SST39SF, W39F, MX29F, A29F series — the dominant protocol)

**Write algorithm:** 3-cycle software unlock before each byte program: write 0xAA→0x5555, 0x55→0x2AAA, 0xA0→0x5555, then data byte to target address PA. The internal program state machine completes in ~10–20 µs per byte. DQ7 data polling: read target address; when DQ7 matches the written bit, done. DQ5 high = timeout indication.
Citation: `datasheets/0x06-FLASH-AMD-ALT/SST39SF040.pdf` p.7 §Byte-Program Operation.

**Erase model:** Sector erase (6-cycle sequence ending with 0x30→sector address) or chip erase (6-cycle sequence ending with 0x10→0x5555). Chip erase time: ~100 ms (SST39SF040: 100 ms max; AM29F040: 32 sectors × ~25 ms). See INV-09 (SST39SF040 keep-Flash/EEPROM in §3) for the `FLAG_CAN_ERASE` / `electrical.type` classification invariant that gates the erase path for 0x06 chips.
Citation: `datasheets/0x06-FLASH-AMD-ALT/SST39SF040.pdf` p.8 §Chip-Erase Operation.

**VPP behavior:** None required (5V-only operation). No VPP regulator use for this bucket. The 12V VPP value appearing in some DB records is a legacy minipro artifact — not used electrically by `configure_flash3()`.
Citation: `datasheets/0x06-FLASH-AMD-ALT/SST39SF040.pdf` p.4 §DC Characteristics (VCC = 4.5–5.5V).

**Pin roles:** 32-pin DIP. Address A0–A18, data D0–D7, CE, OE, WE. AMD unlock command addresses (0x5555/0x2AAA) are A14-don't-care on 512KB space — firmware uses 0x5555/0x2AAA consistently.

---

### 1.3 — 0x07 EPROM-STD: 28-pin UV-EPROM / EE-EPROM, 13 V VPP

**Folder slug (col 1):** `0x07-EPROM-STD`
**Algorithm-axis name (col 2):** EPROM-STD — 28-pin UV-EPROM / EE-EPROM, 13 V VPP
**Handler:** `configure_eprom()` → `eprom.cpp`
**DB chip count:** 170 (AM27Cxxx, 27Cxxx, W27C512, W27E512, ST M27C512, AT27xxx series)

**Write algorithm:** JEDEC Intelligent Programming (1 ms pulse × N + 3× overpulse): set address and data, assert CE (PGM) low for `pulse_delay` µs (default 1000 µs for 0x07), de-assert CE, read back and verify. If mismatch: increment retry counter, repeat. After success: apply overpulse = 3 × retry_count pulses (max ~25 ms total). The ST M27C512 PRESTO IIB variant uses 100 µs pulses set via the DB `pulse-delay` field.
Citation: `datasheets/0x07-EPROM-STD/W27C512.pdf` p.7 §6.2 Programming Algorithm; `datasheets/0x07-EPROM-STD/ST-M27C512.pdf` p.8 §PRESTO IIB.

**Erase model:** UV light erasure for UV-EPROM variants (no electrical erase). Electrically-erasable 0x07 EE-EPROMs (W27C512, W27E512, SST27SF512) are erased via `eprom_internal_erase()` which applies VPE to A9 pin. `FLAG_CAN_ERASE` is derived from `electrical.type == "EEPROM"` (Phase 77 fix) and must be set for auto-erase-before-write.
Citation: `datasheets/0x07-EPROM-STD/W27C512.pdf` p.9 §6.4 Erase Operation.

**VPP behavior:** VPP = 12.5–13V via `CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE` (Rev 2+ drop path). The regulator produces VPE (~2V above VPP); `CTRL_VPP_VPE_DROP_ENABLE` (0x100 on Rev 2) drops it through a resistor divider to reach the 13V VPP level. VPP is applied to the 28-pin socket via JP4 jumper routing. See INV-05 (VPP-skip-on-read in §3): VPP is NOT enabled for CMD_READ or CMD_BLANK_CHECK operations.
Citation: `datasheets/0x07-EPROM-STD/W27C512.pdf` p.5 §5 Pin Description (pin 1 = VPP, 28-pin).

**Pin roles:** 28-pin DIP. A0–A15, D0–D7, CE (pin 20), OE/VPP (pin 22 on CMOS 27C variants — shared OE/PGM, but 0x07 is handled via the CE path). JP4 jumper required on RURP to route VPP to pin 1 for 28-pin DIP programming. Chip ID via A9 VPP (read manufacturer/device ID by raising A9 to VPP level via `CTRL_VPP_A9_ENABLE`).

---

### 1.4 — 0x08 EPROM-QUICK: 32-pin UV-EPROM / EE-EPROM, 13 V VPP

**Folder slug (col 1):** `0x08-EPROM-QUICK`
**Algorithm-axis name (col 2):** EPROM-QUICK — 32-pin UV-EPROM / EE-EPROM, 13 V VPP
**Handler:** `configure_eprom()` → `eprom.cpp`
**DB chip count:** 127 (AM27C010, AM27C020, AM27C040, W27C020, AT27C010 series — 1 Mbit–8 Mbit)

**Write algorithm:** Same Intelligent Programming algorithm as 0x07. Default pulse width = 100 µs for 0x08 (see INV-06 pulse-delay defaults in §3). 32-pin format adds address lines A16–A18 via CONTROL_REGISTER bits. See INV-03 (0x08 P1-as-VPP in §3) for the VPP routing distinction vs 0x07.
Citation: `datasheets/0x08-EPROM-QUICK/W27C020.pdf` p.7 §Programming Algorithm; `datasheets/0x08-EPROM-QUICK/AM27C020.pdf` p.10 §Quick-Pulse Programming.

**Erase model:** UV light erasure for UV-EPROM variants. Some 0x08 parts (W27C020, W27E040) are electrically erasable EE-EPROMs — `FLAG_CAN_ERASE` applies identically to 0x07. AM27C020 write failures (0-bits-programmed) are an open defect (FUT-06); the 0x08 write/VPP path on 32-pin Large EPROM is under investigation.

**VPP behavior:** VPP = 12–13V via `CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE` (same as 0x07). See INV-03: for 0x08, VPP is routed to socket pin 1 directly via `CTRL_VPP_P1_ENABLE (0x08)` — this is the P1-as-VPP path, distinct from the 0x07/0x0B paths.
Citation: `datasheets/0x08-EPROM-QUICK/W27C020.pdf` p.4 §Pin Description (pin 1 = VPP, 32-pin DIP).

**Pin roles:** 32-pin DIP. A0–A18 (full 19-bit address, CONTROL_REGISTER carries A16–A18), D0–D7, CE, OE, PGM/WE. VPP on pin 1 (P1_VPP_ENABLE).

---

### 1.5 — 0x0B EPROM-LEGACY: 24-pin UV-EPROM, 12–25 V Direct-VPE Rail

**Folder slug (col 1):** `0x0B-EPROM-LEGACY`
**Algorithm-axis name (col 2):** EPROM-LEGACY — 24-pin legacy UV-EPROM, 12–25 V VPP direct-VPE rail
**Handler:** `configure_eprom()` → `eprom.cpp`
**DB chip count:** 32 (2716, 2732, 2732A, ETC2716, and small 24-pin EEPROMs)

**Write algorithm:** Same Intelligent Programming pulse algorithm. Default pulse width = 500 µs for 0x0B (see INV-06 in §3). VPP pin location varies by chip revision — pin 21 on 2716, pin 18's A10 doubles as OE/VPP on 2732. A13 is hardwired high for 24-pin socket mode (MSB register bit 5 = `ADDRESS_LINE_13`).
Citation: `datasheets/0x0B-EPROM-LEGACY/2516_EPROM.pdf` p.3 §Programming Procedure.

**Erase model:** UV light erasure only for UV-EPROM variants (2716, 2732, 2732A, 2516). Small 24-pin EEPROMs in this bucket (AT28C04, 28C16) erase via `eprom_internal_erase()` applying VPE to A9 pin.

**VPP behavior:** VPP = 12–25V via `CTRL_VPP_REGULATOR_ENABLE` ONLY — the direct-VPE rail (no drop resistor). See INV-01 (0x0B direct-VPE rail in §3): unlike 0x07/0x08, 0x0B uses `FLAG_VPE_AS_VPP` to apply VPE directly without `CTRL_VPP_VPE_DROP_ENABLE`. The RURP trimpot must be set to the target voltage before programming. NMOS variants (Intel 2716, 2732) historically required 25V — RURP is physically capable of this via the adjustable regulator; firmware warns on under-voltage and proceeds (Phase 79 operator override D-07, best-effort).
Citation: `datasheets/0x0B-EPROM-LEGACY/2516_EPROM.pdf` p.2 §Vpp Programming Voltage.

**Pin roles:** 24-pin DIP. A0–A12 (no A13 — hardwired), D0–D7, CE, OE, VPP (varies by chip; commonly pin 21 for 2716/2732 family). The RURP firmware calculates the 24-pin MSB register value specially in `mem_util_calculate_msb_register()`.

---

### 1.6 — 0x0D EEPROM-POLL: 5 V Parallel EEPROM, SDP + DQ7 Page Poll

**Folder slug (col 1):** `0x0D-EEPROM-POLL`
**Algorithm-axis name (col 2):** EEPROM-POLL — 5 V parallel EEPROM, SDP + DQ7 page poll
**Handler:** `configure_eeprom28c()` → `eeprom_28c.cpp`
**DB chip count:** 84 (AT28C010, AT28C040, X28C010, M28010, CAT28C series, WE series)

**Write algorithm:** Page write (64–128 bytes per page, all within tBLC = 150 µs inter-byte window). SDP (Software Data Protection) unlock sequence if enabled: 0xAA→0x5555, 0x55→0x2AAA, 0xA0→0x5555, then page bytes within tBLC. After the last byte, the chip's internal write cycle fires (~5–10 ms). Firmware permanently disables SDP via a 6-cycle sequence before writing: 0xAA→0x5555, 0x55→0x2AAA, 0x80→0x5555, 0xAA→0x5555, 0x55→0x2AAA, 0x20→0x5555.
Citation: `datasheets/0x0D-EEPROM-POLL/AT28C256.pdf` p.6 §Software Data Protection; p.7 §Page Write.

**Erase model:** Electrically erasable — internal auto-erase before each page write. A dedicated chip erase (SDP sequence + 0xA0→0x5555 in a specific SDP-enable sequence) exists on some parts. The `FLAG_CAN_ERASE` and `electrical.type == "EEPROM"` derivation is authoritative (Phase 77/86).

**VPP behavior:** None (5V internal charge pump). No VPP regulator involvement. Some older SDP-locked parts may have a 12V SDP-bypass path, but the Firestarter path uses software SDP-disable.
Citation: `datasheets/0x0D-EEPROM-POLL/AT28C256.pdf` p.4 §DC Characteristics (VCC = 5V).

**Pin roles:** 32-pin DIP. A0–A17, D0–D7, CE, OE, WE. DQ7 data poll: read last-written address; `(data & 0x80) == (expected & 0x80)` → done. DQ6 toggle: alternates during write cycle.

---

### 1.7 — 0x0E SRAM-32PIN: 32-pin Battery-Backed NVRAM

**Folder slug (col 1):** `0x0E-SRAM-32PIN`
**Algorithm-axis name (col 2):** SRAM-32PIN — 32-pin battery-backed NVRAM, optional 12 V write-protect bypass
**Handler:** `configure_sram()` → `sram.cpp`
**DB chip count:** 20 (DS1245Y, DS1249AB, M48T128Y, BQ4013YMA, and sibling series)

**Write algorithm:** Standard SRAM read/write — no programming protocol. Write: assert address, assert CE+WE low, present data, de-assert WE high (data latches on rising WE edge), de-assert CE. Read: assert address, assert CE+OE low, read data within tACC.
Citation: `datasheets/0x0E-SRAM-32PIN/DS1245Y.pdf` p.3 §Pin Description.

**Erase model:** None — SRAM: contents are volatile or battery-backed. No erase step.

**VPP behavior:** Optional 12V via `CTRL_VPP_P1_ENABLE (0x08)` to bypass the Dallas write-protect circuit (applies 12V to pin 1 or pin 31 depending on device). Without this, Dallas NVRAM write-protect logic (powered by the internal battery) may block writes. M48Txx timekeeping chips do not require this.
Citation: `datasheets/0x0E-SRAM-32PIN/DS1245Y.pdf` p.5 §Write-Protect Override.

**Pin roles:** 32-pin DIP. A0–A18, D0–D7, CE, OE, WE. Write-protect pin varies by device (consult per-chip DB entry). The RURP `configure_sram()` handler uses the bus-config `vpp-pin` field from `chip_database.json` to derive the correct P1/A9 routing.

---

### 1.8 — 0x10 FLASH-INTEL: Intel 28F Command-Register NOR Flash, 12 V VPP Mandatory

**Folder slug (col 1):** `0x10-FLASH-INTEL`
**Algorithm-axis name (col 2):** FLASH-INTEL — Intel 28F command-register NOR flash, 12 V VPP mandatory
**Handler:** `configure_flash_intel()` → `flash_intel.cpp`
**DB chip count:** 39 (Intel 28F010/256/512, AM28F010, P28F010, TMS28F010, SST28SF040 series)

**Write algorithm:** Command-register architecture (no address-based unlock). Byte program: write 0x40 (Setup Program) to any address, write data byte to PA, wait ~10 µs, write 0xC0 (Program Verify) to any address, read PA, compare. On mismatch: write 0x00 (Reset), retry. Maximum 25 write-verify cycles per byte.
Citation: `datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf` p.9 §Quick-Pulse Programming Algorithm.

**Erase model:** Bulk chip erase only (no sector erase on original 28F010). Pre-condition: all bytes must be pre-programmed to 0x00 before erase. Erase: write 0x20 (Erase Setup) twice to any address, wait ~12 ms, write 0xA0 (Erase Verify), read all bytes (expect 0xFF). On failure: write 0x00, retry up to 1000 iterations.
Citation: `datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf` p.10 §Quick-Erase Algorithm.

**VPP behavior:** 12V VPP MANDATORY via `CTRL_VPP_P1_ENABLE (0x08)` — VPP ≤ 6.5V inhibits all program and erase operations. The RURP `CTRL_VPP_REGULATOR_ENABLE` must be set to hold VPP high during the entire program/erase cycle.
Citation: `datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf` p.6 §VPP Characteristics (VPP < 6.5V = write inhibit).

**Pin roles:** 32-pin DIP. Address A0–A19, D0–D7 (bidirectional), CE, OE, WE, VPP on pin 1. The 0x40/0xC0/0x20/0xA0/0x00/0xFF command sequence is written to the chip's command register — any address suffices. Unlike AMD flash, there is no address-based unlock; commands go directly.

---

### 1.9 — 0x27 SRAM-24PIN: 24-pin Async SRAM, 5 V

**Folder slug (col 1):** `0x27-SRAM-24PIN`
**Algorithm-axis name (col 2):** SRAM-24PIN — 24-pin async SRAM, 5 V
**Handler:** `configure_sram()` → `sram.cpp`
**DB chip count:** 2 (6116 / 2K×8, DS1220(TEST))

**Write algorithm:** Standard SRAM read/write. Same as 0x0E. CE+WE pulsed for write; CE+OE for read.
Citation: `datasheets/0x27-SRAM-24PIN/6116.pdf` p.3 §Pin Description.

**Erase model:** None — volatile SRAM.

**VPP behavior:** None (5V SRAM).

**Pin roles:** 24-pin DIP. JEDEC 6116 pinout: A0–A10 on pins 8–1 and 19–23, D0–D7 on pins 9–11/13–17, /WE on pin 21, /OE on pin 20, /CS on pin 18. The 24-pin socket requires A13 hardwired high via MSB register bit 5 (same `ADDRESS_LINE_13` mode as 0x0B).
Citation: `datasheets/0x27-SRAM-24PIN/6116.pdf` p.2 §Pin Configuration.

---

### 1.10 — 0x28 SRAM-STD: 28-pin SRAM / FRAM (NAME-04: FM1608 SRAM→FRAM correction)

**Folder slug (col 1):** `0x28-SRAM-STD`
**Algorithm-axis name (col 2):** SRAM-STD — 28-pin SRAM/FRAM, 5 V (see NAME-04 call-out below)
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

### 1.11 — 0x29 SRAM-512K-1M: 32-pin Large Battery-Backed NVRAM

**Folder slug (col 1):** `0x29-SRAM-512K-1M`
**Algorithm-axis name (col 2):** SRAM-512K-1M — 32-pin large battery-backed NVRAM, 512 K–1 M
**Handler:** `configure_sram()` → `sram.cpp`
**DB chip count:** 20 (DS1245AB(TEST), DS1249AB(TEST), DS1250AB(TEST), BQ4013YMA(TEST), M48T128Y(TEST) series)

**Write algorithm:** Same SRAM read/write as 0x0E. These are the same physical chips as the 0x0E bucket but listed with `(TEST)` name suffix in the minipro DB — the test-mode variant uses built-in memory test patterns rather than externally loaded data. At the RURP hardware level, read/write cycles are identical.
Citation: `datasheets/0x29-SRAM-512K-1M/DS1245Y.pdf` p.3 §Functional Description (DS1245Y is a sibling substitute; see datasheets/README.md §D-02 note on DS1250Y substitution).

**Erase model:** None — SRAM/NVRAM.

**VPP behavior:** Optional 12V write-protect bypass (same as 0x0E, Dallas battery-backed series).

**Pin roles:** 32-pin DIP. A0–A18, D0–D7, CE, OE, WE. Same socket as 0x0E.

---

### 1.12 — 0x34 EEPROM-X88C64: XICOR 8051-Bus EEPROM, PCB-blocked (FUT-01) (NAME-04 correction)

**Folder slug (col 1):** `0x34-EEPROM-X88C64`
**Algorithm-axis name (col 2):** EEPROM-X88C64 — XICOR 8051-bus EEPROM (PCB-blocked, document-only)
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

These IDs appear in the `configure_flash4()` dispatch for forward-compatibility but have zero
DB chips. The host excludes both from `KNOWN_PROTOCOLS` and routes any chip that somehow
reaches them to `not_implemented`.

| hex | firmware name | reason |
|-----|--------------|--------|
| `0x35` | `FLASH_EEPROM` | `IC2_ALG_ITE` is an ITE EC microcontroller label in minipro, NOT a memory programming algorithm. Zero chips in `chip_database.json`. Firmware dispatch preserved for forward-compat. |
| `0x39` | `FLASH_EEPROM2` | No `IC2_ALG` constant exists for this value in minipro source. Zero chips in `chip_database.json`. Firmware dispatch preserved for forward-compat. |

These are not FLASH-AMD-STD variants, EPROM variants, or any other real protocol. They are
dead dispatch arms. The old `.planning/research/PROTOCOLS.md` description of 0x35 as "AT29C
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
- INV-04 → `test/native/avr/test_val_flash4/`
- INV-07 → `test/native/avr/test_val_sram/`
- INV-09 → `test/native/avr/test_val_flash3/`

| INV id | One-line behavior | Owning handler file | Planned native test function name | Suite path |
|--------|-------------------|---------------------|----------------------------------|------------|
| INV-01 | 0x0B uses `FLAG_VPE_AS_VPP` direct-VPE rail (no `CTRL_VPP_VPE_DROP_ENABLE` drop) | `eprom.cpp` | `test_inv01_eprom_0x0B_direct_vpe_rail` | `test/native/avr/test_val_eprom/` |
| INV-02 | 0x0B shares OE/VPP pin — read operations skip VPP enable to avoid OE conflict | `eprom.cpp` | `test_inv02_eprom_0x0B_oe_vpp_read_skip` | `test/native/avr/test_val_eprom/` |
| INV-03 | 0x08 routes VPP to socket pin 1 via `CTRL_VPP_P1_ENABLE` (not drop path) | `eprom.cpp` | `test_inv03_eprom_0x08_p1_as_vpp` | `test/native/avr/test_val_eprom/` |
| INV-04 | flash4 page size is data-driven from `handle->mem_size` (256B for W29C040 512KB; 128B for 128KB; 64B for 32KB) | `flash_type_4.cpp` | `test_inv04_flash4_256b_page_boundary` | `test/native/avr/test_val_flash4/` |
| INV-05 | VPP is NOT enabled for CMD_READ or CMD_BLANK_CHECK — firmware skips VPP init on read path (VPP-skip-on-read) | `eprom.cpp` | `test_inv05_eprom_vpp_skip_on_read` | `test/native/avr/test_val_eprom/` |
| INV-06 | Pulse-delay defaults: 0x08 → 100 µs; 0x0B → 500 µs; all other (0x07 default) → 1000 µs | `eprom.cpp` | `test_inv06_eprom_pulse_delay_defaults` | `test/native/avr/test_val_eprom/` |
| INV-07 | FM1608 routes to `configure_sram()` as SRAM_STD/FRAM (algorithm=0x28), NOT `configure_eprom()` (BLOCKER-2 mitigation) | `sram.cpp` | `test_inv07_sram_fm1608_routes_to_sram` | `test/native/avr/test_val_sram/` |
| INV-08 | **(dispatch-only scope)** WARNING-5 (0x07 EE-EPROM chips reclassified to 0x0D) is delivered by Phase-86 variant decode — no `build_db.py` runtime override. The correct `electrical.type` flows from the DB and the dispatch chain honors it. The WARNING-5 retirement itself is **host-side** (`build_db.py`, gated by `diff_db.py`) and is NOT firmware-testable; the native test below pins ONLY the downstream firmware consequence — that 0x07 still dispatches to `configure_eprom`. | `eprom.cpp` / build path | `test_inv08_eprom_warning5_decode_preserved` | `test/native/avr/test_val_eprom/` |
| INV-09 | SST39SF040 (0x06) retains `electrical.type = Flash/EEPROM` — the `FLAG_CAN_ERASE` + `configure_flash3()` combination must not be confused with the UV-EPROM path | `flash_type_3.cpp` | `test_inv09_flash3_sst39sf040_keep_flash_eeprom` | `test/native/avr/test_val_flash3/` |

### Cross-links to per-bucket sections

- INV-01, INV-02: [§1.5 (0x0B EPROM-LEGACY)](#15----0x0b-eprom-legacy-24-pin-uv-eprom-12-25-v-direct-vpe-rail)
- INV-03: [§1.4 (0x08 EPROM-QUICK)](#14----0x08-eprom-quick-32-pin-uv-eprom--ee-eprom-13-v-vpp)
- INV-04: [§1.1 (0x05 FLASH-AMD-STD)](#11----0x05-flash-amd-std-5v-page-write-flash-eeprom-like)
- INV-05: [§1.3 (0x07 EPROM-STD)](#13----0x07-eprom-std-28-pin-uv-eprom--ee-eprom-13-v-vpp)
- INV-06: [§1.4 (0x08 EPROM-QUICK)](#14----0x08-eprom-quick-32-pin-uv-eprom--ee-eprom-13-v-vpp), [§1.5 (0x0B EPROM-LEGACY)](#15----0x0b-eprom-legacy-24-pin-uv-eprom-12-25-v-direct-vpe-rail)
- INV-07: [§1.10 (0x28 SRAM-STD)](#110----0x28-sram-std-28-pin-sram--fram-name-04-fm1608-sramfram-correction)
- INV-08: [§1.3 (0x07 EPROM-STD)](#13----0x07-eprom-std-28-pin-uv-eprom--ee-eprom-13-v-vpp)
- INV-09: [§1.2 (0x06 FLASH-AMD-ALT)](#12----0x06-flash-amd-alt-amdsst-unlock-sequence-nor-flash)

---

*Phase 87 — Naming + Documentation Pass | Authored 2026-06-26*
*Canonical bucket set re-verified from `chip_database.json` (746 chips, 12 real buckets)*
*Datasheets committed in Phase 85; datasheet citation anchors are best-available locators per D-discretion*
*INV-01..INV-09 ids are the SAFE-02 handoff to Phases 88/89 — grep-intact through recompose*
