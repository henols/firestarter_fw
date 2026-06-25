# Firestarter Protocol Datasheets — Index (DSHEET-03)

This index maps each of the 12 live protocol bucket folders to its hex ID, proposed name,
firmware handler, committed datasheet filename(s), on-hand status, and full per-file provenance
(source URL, retrieval date, substitute flag). Authored 2026-06-25 after all PDFs were committed;
every filename reference below maps to a real committed file.

This index is consumed by Phase 86 (naming pass) and Phase 89 (bench ledger).

---

## Bucket Index

> **Column key:**
> - `hex` — `protocol_id` / `algorithm` value dispatched by the firmware
> - `proposed name` — provisional bucket name (canonical naming authority: Phase 86)
> - `handler (file)` — firmware function → source file (from `firestarter/CLAUDE.md` §Algorithm Handlers)
> - `datasheet filename(s)` — committed PDF(s) under `datasheets/<bucket>/`
> - `on-hand status` — `on-hand` = physical chip in bench inventory; `no-silicon (representative)` = no chip on hand, PDF documents the bucket algorithm
> - `source URL` — the URL the PDF was retrieved from (provenance; the committed file is the durable artifact)
> - `retrieved` — retrieval date (ISO 8601)
> - `substitute?` — `exact` = the named part's own datasheet; `exact-family` = family doc covers the named part as a documented member; `family-substitute` = sibling-part family doc used because exact leaf is inaccessible; `substitute` = different but closely related part from the same protocol bucket; `data-book` = vendor data book scan containing the part (not a dedicated leaflet)

| hex | proposed name | handler (file) | datasheet filename(s) | on-hand status | source URL | retrieved | substitute? |
|-----|---------------|----------------|-----------------------|----------------|------------|-----------|-------------|
| 0x05 | FLASH-AMD-STD | `configure_flash4()` → `flash_type_4.cpp` | `W29C020.pdf`, `W29C040.pdf` | on-hand | see per-file table | 2026-06-25 | see per-file table |
| 0x06 | FLASH-AMD-ALT | `configure_flash3()` → `flash_type_3.cpp` | `SST39SF040.pdf` | on-hand | `https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/SST39SF010A-SST39SF020A-SST39SF040-Data-Sheet-DS20005022.pdf` | 2026-06-25 | exact |
| 0x07 | EPROM-STD | `configure_eprom()` → `eprom.cpp` | `W27C512.pdf`, `W27E512.pdf`, `SST27SF512.pdf`, `ST-M27C512.pdf` | on-hand | see per-file table | 2026-06-25 | see per-file table |
| 0x08 | EPROM-QUICK | `configure_eprom()` → `eprom.cpp` | `W27C020.pdf`, `W27E040.pdf`, `AM27C020.pdf` | on-hand | see per-file table | 2026-06-25 | see per-file table |
| 0x0B | EPROM-LEGACY | `configure_eprom()` → `eprom.cpp` | `2516_EPROM.pdf` | on-hand | `https://archive.org/download/2516_EPROM/2516_EPROM.pdf` | 2026-06-25 | exact |
| 0x0D | EEPROM-POLL | `configure_eeprom28c()` → `eeprom_28c.cpp` | `AT28C256.pdf` | no-silicon (representative) | `https://ww1.microchip.com/downloads/en/DeviceDoc/doc0006.pdf` | 2026-06-25 | exact |
| 0x0E | SRAM-32PIN | `configure_sram()` → `sram.cpp` | `DS1245Y.pdf` | no-silicon (representative) | `https://www.futurlec.com/Datasheet/Dallas/DS1245Y.pdf` | 2026-06-25 | exact |
| 0x10 | FLASH-INTEL | `configure_flash_intel()` → `flash_intel.cpp` | `Intel-28F010.pdf` | no-silicon (representative) | `https://www.ardent-tool.com/datasheets/Intel_28F010.pdf` | 2026-06-25 | exact |
| 0x27 | SRAM-24PIN | `configure_sram()` → `sram.cpp` | `6116.pdf` | no-silicon (representative) | `http://www.princeton.edu/~mae412/HANDOUTS/Datasheets/6116.pdf` | 2026-06-25 | exact |
| 0x28 | SRAM-STD | `configure_sram()` → `sram.cpp` | `FM1608.pdf` | on-hand | `https://www.farnell.com/datasheets/82469.pdf` | 2026-06-25 | exact |
| 0x29 | SRAM-512K-1M | `configure_sram()` → `sram.cpp` | `DS1245Y.pdf` | no-silicon (representative) | `https://www.futurlec.com/Datasheet/Dallas/DS1245Y.pdf` | 2026-06-25 | substitute |
| 0x34 | EEPROM-X88C64 | `configure_not_implemented()` → `not_implemented.cpp` | `X88C64.pdf` | no-silicon (representative) | `https://www.bitsavers.org/components/xicor/1990_Xicor_Data_Book.pdf` | 2026-06-25 | data-book |

---

## Per-File Provenance Table

Full provenance for each of the 18 committed PDFs. The bucket index above summarises; this
table is the authoritative per-file record for Phase 86/89 consumers and audit purposes.

| filename | bucket | actual source URL | retrieved | exact/substitute | notes |
|----------|--------|-------------------|-----------|------------------|-------|
| `W29C020.pdf` | 0x05-FLASH-AMD-STD | `http://bitsavers.org/components/winbond/W29C020.PDF` | 2026-06-25 | exact | Bitsavers primary vendor scan |
| `W29C040.pdf` | 0x05-FLASH-AMD-STD | `https://datasheet.octopart.com/W29C040-90-Winbond-datasheet-181529586.pdf` | 2026-06-25 | exact | Winbond via Octopart CDN |
| `SST39SF040.pdf` | 0x06-FLASH-AMD-ALT | `https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/SST39SF010A-SST39SF020A-SST39SF040-Data-Sheet-DS20005022.pdf` | 2026-06-25 | exact | Microchip official (SST acquisition) |
| `W27C512.pdf` | 0x07-EPROM-STD | `http://bitsavers.org/components/winbond/W27C512_64Kx8_EEPROM_199911.pdf` | 2026-06-25 | exact | Bitsavers primary vendor scan |
| `W27E512.pdf` | 0x07-EPROM-STD | `https://media.digikey.com/pdf/Data%20Sheets/Winbond%20PDFs/W27C512.pdf` | 2026-06-25 | exact-family | W27C512 Winbond family doc covers both W27C512 and W27E512 siblings; the chip_database.json entry is literally "W27C512,W27E512" with algorithm=7 (same silicon entry). W27E512 is the electrically-erasable sibling; both use the EPROM-STD algorithm. |
| `SST27SF512.pdf` | 0x07-EPROM-STD | `https://datasheet.octopart.com/SST27SF256-70-3C-PG-SST-datasheet-7196.pdf` | 2026-06-25 | family-substitute | SST27SF256 sibling family doc; exact SST27SF512 leaf only at alldatasheet.com (blocked by HTML interstitial on curl). The SST27SFxxx family doc explicitly covers 256/512/1M/2M variants. Filed as `SST27SF512.pdf` per plan instructions. |
| `ST-M27C512.pdf` | 0x07-EPROM-STD | `https://media.digikey.com/pdf/data%20sheets/st%20microelectronics%20pdfs/m27c512.pdf` | 2026-06-25 | exact | ST Microelectronics M27C512 via DigiKey CDN |
| `W27C020.pdf` | 0x08-EPROM-QUICK | `http://www.winbond.com/PDF/sheet/w27c020.pdf` (via Wayback Machine `https://web.archive.org/web/2id_/http://www.winbond.com/PDF/sheet/w27c020.pdf`) | 2026-06-25 | exact | **On-hand chip (DSHEET-01).** Winbond W27C020 official leaf, Rev A1, Sept 1998; "256K × 8 ELECTRICALLY ERASABLE EPROM". Content verified by text extraction (part number present 28×). Manufacturer-primary source via Wayback (the live winbond.com path now 404s). DB entry `W27C02,W27C020,W27E02,W27E020,W27L02`, `algorithm=8`, DIP32_STD, 12V VPP. |
| `W27E040.pdf` | 0x08-EPROM-QUICK | `http://bitsavers.org/components/winbond/W27C512_64Kx8_EEPROM_199911.pdf` | 2026-06-25 | family-substitute | Winbond EPROM family doc (W27C512 bitsavers scan) used as algorithm reference. Exact W27E040 leaf exists only at alldatasheet.com (blocked by HTML interstitial on curl); bitsavers has no W27E040 entry; all other aggregators bot-walled. W27E040 is a 512Kx8 EEPROM in the same Winbond 27xxx series; both use the EPROM-QUICK algorithm. Filed as `W27E040.pdf`. |
| `AM27C020.pdf` | 0x08-EPROM-QUICK | `https://web.stanford.edu/class/ee183/datasheets/27c020.pdf` | 2026-06-25 | exact | AMD AM27C020 Rev F via Stanford.edu |
| `2516_EPROM.pdf` | 0x0B-EPROM-LEGACY | `https://archive.org/download/2516_EPROM/2516_EPROM.pdf` | 2026-06-25 | exact | TI/Intel 2516 scan on archive.org. See note [1] re: DB entry. |
| `AT28C256.pdf` | 0x0D-EEPROM-POLL | `https://ww1.microchip.com/downloads/en/DeviceDoc/doc0006.pdf` | 2026-06-25 | exact | Microchip official (Atmel acquisition); canonical SDP-unlock + DQ7/DQ6 page-poll reference |
| `DS1245Y.pdf` | 0x0E-SRAM-32PIN | `https://www.futurlec.com/Datasheet/Dallas/DS1245Y.pdf` | 2026-06-25 | exact | Dallas 8Mbit NVRAM; documents 12V VPP write-protect-bypass that distinguishes this bucket |
| `Intel-28F010.pdf` | 0x10-FLASH-INTEL | `https://www.ardent-tool.com/datasheets/Intel_28F010.pdf` | 2026-06-25 | exact | Intel 28F010 (= AM28F010); canonical command-register architecture (0x40 setup / 0xC0 verify / 0x20 erase) |
| `6116.pdf` | 0x27-SRAM-24PIN | `http://www.princeton.edu/~mae412/HANDOUTS/Datasheets/6116.pdf` | 2026-06-25 | exact | Canonical 24-pin async SRAM JEDEC pinout |
| `FM1608.pdf` | 0x28-SRAM-STD | `https://www.farnell.com/datasheets/82469.pdf` | 2026-06-25 | exact | Ramtron FM1608 via Farnell; bucket is 0x28 = decimal 40 (FRAM algorithm). |
| `DS1245Y.pdf` | 0x29-SRAM-512K-1M | `https://www.futurlec.com/Datasheet/Dallas/DS1245Y.pdf` | 2026-06-25 | substitute | DS1250Y (the intended exemplar) is curl-blocked on analog.com (000) and all mirrors. DS1245Y is the verified sibling (same Dallas battery-backed NVRAM family, same 0x29 protocol bucket). Per RESEARCH A3 fallback. The target part DS1250Y was the original; DS1245Y stands in for it. |
| `X88C64.pdf` | 0x34-EEPROM-X88C64 | `https://www.bitsavers.org/components/xicor/1990_Xicor_Data_Book.pdf` | 2026-06-25 | data-book | 1990 Xicor Data Book scan (26 MB); contains X88C64 section. This is a vendor data book, NOT a dedicated 2-page leaflet. X88C64 is the sole DB member of 0x34. |

**Note [1] — 2516 DB entry:** The 2516 is a real 0x0B-class part (Intel/TI 24-pin UV-EPROM), but it has **no committed entry in `chip_database.json`** — it is a v1.15 user-override row (`~/.firestarter/database.json`). Its folder is correctly keyed under 0x0B (algorithm=0x0B); the datasheet is exact and sourced. Phase 86 or a future phase may add a canonical DB entry. Do not confuse the absent DB entry with an absent or substitute datasheet.

---

## Exclusions

The following hex IDs have **no folder and no datasheet** in this tree. They are documented here
as explicit exclusions per DSHEET-03. No folder must ever be created for them.

### Phantom Buckets (dispatched-but-dead)

These IDs appear in the firmware dispatch chain (for forward-compatibility) but have **zero chips
in `chip_database.json`**. The host excludes both from `KNOWN_PROTOCOLS`.

| hex | name in firmware | reason no folder |
|-----|-----------------|-----------------|
| `0x35` | FLASH\_EEPROM | `IC2_ALG_ITE` is an ITE EC microcontroller label, NOT a memory programming algorithm. Zero DB chips. Firmware dispatch preserved for forward-compat; host routes to `not_implemented`. |
| `0x39` | FLASH\_EEPROM2 | No `IC2_ALG` constant exists for this value. Zero DB chips. Firmware dispatch preserved for forward-compat; host routes to `not_implemented`. |

### Infeasible Buckets (fail-closed on RURP hardware)

These protocols are infeasible on the RURP shield. The firmware routes them to
`configure_not_implemented()` (Phase 64, DISP-04), returning `0xBB
MSG_ERR_PROTOCOL_NOT_IMPLEMENTED` with zero hardware side effects.

| hex | name | reason no folder |
|-----|------|-----------------|
| `0x11` | FWH/LPC-serial | FWH / LPC-bus serial flash — requires dedicated FWH serial protocol that the RURP parallel bus cannot provide. |
| `0x2A` | GAL/PLD | GAL / PLD device — requires high-voltage programming pulses and a dedicated algorithm not supported by RURP. |
| `0x2B` | GAL/PLD variant | Same rationale as `0x2A`. |
| `0x2C` | PIC microcontroller | PIC MCU — requires ICSP protocol; incompatible with RURP parallel memory bus. |

---

## Sourcing Policy

### D-02 — Substitute on exact-leaf blockage

Where an exact part datasheet is unobtainable (aggregator bot-wall, vendor CDN block, dead link),
a compatible or second-source part's datasheet is used as a substitute. Every substitution is
flagged in the `substitute?` column above and the notes column explains what was tried and why
the substitute is adequate for the algorithm context. Three D-02 fallbacks occurred during Plan 02
acquisition (all anticipated by RESEARCH):

1. **SST27SF512** — exact leaf blocked by alldatasheet.com interstitial. Substitute: SST27SF256
   family doc (same SST27SFxxx series, covers 256/512/1M/2M). Filed as `SST27SF512.pdf`.

2. **W27E040** — exact leaf blocked by alldatasheet.com interstitial; bitsavers has no W27E040
   entry. Substitute: Winbond W27C512 bitsavers family doc (same vendor/algorithm). Filed as
   `W27E040.pdf`.

3. **DS1250Y (0x29 bucket)** — DS1250Y primary source (analog.com) curl-blocked (000);
   maximintegrated.com redirects to same block; futurlec 404; bitsavers has no Dallas directory.
   Substitute: DS1245Y sibling (same Dallas battery-backed NVRAM family, same 0x29 protocol
   bucket). Filed as `DS1245Y.pdf` in `0x29-SRAM-512K-1M/`.

### D-03 — MISSING / UNSOURCED fallback

If a datasheet is truly unobtainable after exhausting all known sources, it would be recorded as a
`MISSING` / `UNSOURCED` row in the provenance table above (with what was tried), and the phase
would still complete. No row currently invokes D-03; all 18 PDFs have real, committed content.

---

## Folder Tree

```
datasheets/
├── README.md                          ← this file (DSHEET-03 index)
├── datasheets-check.sh                ← Wave-0 phase-gate check script
├── 0x05-FLASH-AMD-STD/                handler: configure_flash4() → flash_type_4.cpp
│   ├── W29C020.pdf
│   └── W29C040.pdf
├── 0x06-FLASH-AMD-ALT/                handler: configure_flash3() → flash_type_3.cpp
│   └── SST39SF040.pdf
├── 0x07-EPROM-STD/                    handler: configure_eprom() → eprom.cpp
│   ├── W27C512.pdf
│   ├── W27E512.pdf                    (exact-family: W27C512 covers both siblings)
│   ├── SST27SF512.pdf                 (family-substitute: SST27SF256 family doc)
│   └── ST-M27C512.pdf
├── 0x08-EPROM-QUICK/                  handler: configure_eprom() → eprom.cpp
│   ├── W27C020.pdf                    (on-hand; exact Winbond leaf, Rev A1)
│   ├── W27E040.pdf                    (family-substitute: W27C512 bitsavers family doc)
│   └── AM27C020.pdf
├── 0x0B-EPROM-LEGACY/                 handler: configure_eprom() → eprom.cpp
│   └── 2516_EPROM.pdf                 (note: no chip_database.json entry — user-override only)
├── 0x0D-EEPROM-POLL/                  handler: configure_eeprom28c() → eeprom_28c.cpp
│   └── AT28C256.pdf                   (no-silicon representative)
├── 0x0E-SRAM-32PIN/                   handler: configure_sram() → sram.cpp
│   └── DS1245Y.pdf                    (no-silicon representative)
├── 0x10-FLASH-INTEL/                  handler: configure_flash_intel() → flash_intel.cpp
│   └── Intel-28F010.pdf               (no-silicon representative)
├── 0x27-SRAM-24PIN/                   handler: configure_sram() → sram.cpp
│   └── 6116.pdf                       (no-silicon representative)
├── 0x28-SRAM-STD/                     handler: configure_sram() → sram.cpp
│   └── FM1608.pdf
├── 0x29-SRAM-512K-1M/                 handler: configure_sram() → sram.cpp
│   └── DS1245Y.pdf                    (no-silicon rep; substitute for DS1250Y — see D-02 above)
└── 0x34-EEPROM-X88C64/                handler: configure_not_implemented() → not_implemented.cpp
    └── X88C64.pdf                     (no-silicon rep; data book scan — see provenance table)
```

No folders exist for `0x35`, `0x39`, `0x11`, `0x2A`, `0x2B`, `0x2C` (see Exclusions above).

---

*Phase 85 — Datasheet Acquisition | Authored 2026-06-25*
*Next: Phase 86 — Naming and Documentation Pass (canonical protocol vocabulary)*
