# PY32F071 Flash Path & PCB Requirements — Design Record

**Phase 129 Plan 06. Requirements: PCB-01, PCB-02, PCB-03, PCB-04, PCB-05.**

**Subset layer.** This document is the firmware-repo subset of the authoritative record at `.planning/v1.23-FLASH-PATH-DECISION.md` in the Firestarter meta-repo (D-01). The five sections below correspond to that record's §2–§6, keyed by marker: `[SHARED:S1]` the three-tier flash path (§2), `[SHARED:S2]` PCB requirements before the first schematic (§3), `[SHARED:S3]` the flash budget, as actually reserved (§4), `[SHARED:S4]` USB vendor and product identity (§5), `[SHARED:S5]` socket empty before any PY32F071 firmware install (§6) — each kept byte-identical with the parent, enforced by `tests/test_flash_path_record_sync.py` rather than by lockstep discipline alone. The parent additionally carries what this copy does not: the sourced silicon substrate and its three corrections (§1, specifically §1.6), the rejected-route survey (§7), the tracked obligations (§8), and the open questions (§9).

**Starting a schematic?** Read the checklist and the flash budget, below. **About to install firmware on a board?** Read the socket section, below, first. **Implementing the self-flash bootloader?** Read the three-tier section and the flash budget, below, then the parent's open questions (§9).

## The three-tier flash path [SHARED:S1]

This record fixes three flash-path routes onto a PY32F071 board, in priority order, each with a fixed role: a **self-flash bootloader** as the intended primary path, **factory USB DFU** as the maintainer/manufacturing recovery route, and **SWD** as the last resort.

**Tier 1 — self-flash bootloader over the existing USB CDC + COBS transport. Intended primary.** The host sends the new firmware image over the serial port it already owns, using the same USB CDC + COBS framing the application firmware already speaks; a small bootloader in the reserved region (§4) writes the image to the application region. Zero new host dependencies: `pyserial` is already a `firestarter_app` dependency, so no binary discovery, no `avrdude.conf` analogue, and no driver install are needed — structurally identical to how the Arduino Uno's bootloader works today. This removes both the libusb/WinUSB driver friction of Tier 2 and the BOOT0 strap dance. It is **not built in v1.23**: it is FUT-N05, its own milestone, and §4 records the flash budget it will need. It carries three reliability properties from the seed, each `[UNVERIFIED-UNTIL-SILICON]` because they describe the intended behaviour of software that does not exist on hardware that does not exist: the bootloader is never in its own update path; the application image is CRC-verified before the jump; and an interrupted transfer leaves the board in the bootloader, a recoverable state rather than a brick.

**Tier 2 — factory USB DFU. Maintainer/manufacturing recovery.** The part's own factory bootloader, resident in system memory, reached over the same two USB pins as the application (§1.4), entered by the strap condition §1.2 describes. v1.23 landed a pure-Python DFU client, `firestarter_app/firestarter/py32_dfu.py`, so no external binary is required; the residual cost is `pyusb` plus a libusb backend, and a WinUSB driver on Windows. Reaching this tier depends on the strap, and the strap depends on an option bit whose factory value §1.2 records as unresolved.

**Tier 3 — SWD. Last resort.** On PA13/PA14 plus `nRST` (§1.3). Unconditional: it requires physical board access and a probe, and it is the only route that survives a bad option-byte state.

External tools are acceptable on the recovery path — Tier 2 and Tier 3 belong to the maintainer and the factory — but never on the end-user path, which is the operator constraint the seed records and this record inherits.

**Landing the factory USB DFU path in v1.23 does not retire the self-flash bootloader seed.** The DFU path shortens the road to the bootloader rather than replacing it: it proves the transfer sequence over real USB, while remaining the recovery route the PCB requirements in §3 already assume.

This section is shared verbatim with `firestarter/platform/py32f071/FLASH-PATH-AND-PCB.md`; any edit must land in both copies in the same change.

## PCB requirements before the first schematic [SHARED:S2]

Every item below is free to decide today and unrecoverable after layout — one of them (R3) is unrecoverable after *part selection*, a step earlier than layout itself. Each row is deliberately a checkbox plus exactly one rationale line and one consequence line, so a schematic author can work straight down it and Phase 130's honesty ledger can cite a specific row.

- [ ] **R1 — BOOT0 / nBOOT1 strap reachable.**
  - *Why:* `PF8-BOOT0` defaults to input with its pull-down enabled, so the strap must actively pull PF8 high — a jumper, button or test point to VDD — for the tier-2 factory-DFU recovery route in §2 to exist at all; an external pull-down is optional but cheap insurance against a floating strap net; and while the self-flash route in §2 would remove the strap dance, it is FUT-N05 and does not exist yet, so the strap must stay reachable in the meantime. Note that `PF8` is not bonded on `QFN56`, a second, independent reason that package is ruled out in R3.
  - *Breaks if omitted:* the only recovery route that needs no probe is unreachable, and the board is SWD-only from the first bad image onward.

- [ ] **R2 — SWD pads exposed, including nRST.**
  - *Why:* `PA13`/`PA14` are `SWDIO`/`SWCLK` after reset but both carry other alternate functions, so reassigning either pin is possible and would foreclose the unconditional last-resort recovery path; `nRST` belongs on the same header because §1.2's option-byte hazard (`nBOOT1 = 0` selects SRAM, so `BOOT0 = 1` reaches a boot area with nothing in it, and the factory value is unresolved — §9 Open Question 1) is precisely the state only SWD can recover from.
  - *Breaks if omitted:* a single bad option-byte write bricks the board permanently; a usable header carries SWDIO, SWCLK, GND, a VDD sense and `nRST`.

- [ ] **R3 — Contiguous PB0–PB7 data bus, and a package that can carry it.**
  - *Why:* the one-snapshot `IDR` read and atomic `BSRR` write design needs the eight data lines on one port in one contiguous run; `PB2`/`PB3` are not bonded on `QFN56`, and `PB2`–`PB7` are not bonded on `QFN32`, so this is a **part-selection** decision before it is a pin-assignment one — viable packages are `LQFP64`, `CSP64`, `QFN64`, `LQFP48` and `QFN48`; `QFN56` and `QFN32` are ruled out.
  - *Breaks if omitted:* the bus becomes a split-port read/write pair or a shift-and-mask, losing atomicity, and no layout change can recover it once the part is chosen — this discharges the seed's own open instruction to confirm the bus against the final package and pin multiplexing.

- [ ] **R4 — HSE crystal footprint laid out and depopulated; HSE pins left unassigned.**
  - *Why:* the port already runs `HSE_OFF` and derives USB's 48 MHz from HSI × PLL, and the part has a Clock Check System that can trim HSI against `USBD_SOF` — but the SDK exemplifies the LSE reference, not `USBD_SOF`, so untrimmed HSI meeting USB full-speed tolerance is unproven `[UNVERIFIED-UNTIL-SILICON]`.
  - *Breaks if omitted:* retrofitting a crystal to a board with no footprint is a respin, whereas depopulating one costs two unplaced parts — and assigning the HSE pins to any other signal forecloses the hedge, which is the actual unrecoverable decision.

- [ ] **R5 — VPP sense on PA4 / ADC channel 4, with its divider a board decision.**
  - *Why:* the provisional map puts the VPP measurement on `PA4` because that matches the Puya `ADC` example, and every board must declare its VPP control mode — the v1.23 seam returns manual-adjustment on every board, so this sense path is the only VPP feedback that exists. `[ASSUMED — PA4 follows the vendor ADC example; no schematic has confirmed it]`
  - *Breaks if omitted:* no voltage readback at all, and the divider ratio cannot be added after layout without cutting the board.

- [ ] **R6 — Test points on the data bus and the control strobes.**
  - *Why:* there is no bus-trace oracle on the ARM target — the golden-trace harness runs on `native`, not on ARM — so ARM-emitted sequences could diverge from the AVR ones with nothing able to notice; physical probes are the only oracle a first board will have.
  - *Breaks if omitted:* first bring-up has no way to distinguish a firmware fault from a wiring fault, on a board whose pin map has never been checked against hardware.

- [ ] **R7 — USB connector on PA11/PA12, and the D+ pull-up decided before layout.**
  - *Why:* `PA11`/`PA12` are the USB PHY pins for both the application and the factory bootloader, with no remap; whether the PHY provides an internal software-controlled D+ pull-up or a discrete `1.5 kΩ` resistor to 3V3 is required could not be answered from the datasheet, the CMSIS device header or the CherryUSB port — §9 Open Question 2.
  - *Breaks if omitted:* a board that needs the resistor and omits it does not enumerate at all, and a board that fits one where the PHY already has one enumerates wrongly or not at all; confirm from the reference manual's USB chapter before the first schematic — do not guess.

### Deliberately undecided

The items below are named because silence in a document like this reads as "no constraint", and each is genuinely open rather than merely unwritten.

- **Socket.** Whether the PROM socket is a **ZIF** socket or a plain DIP socket is undecided; it depends on a target insertion-cycle count and a cost ceiling that do not exist yet.
- **Connector.** The connector family for the USB port and for any programming/SWD header is undecided; it depends on the enclosure and the target form factor, neither of which exists yet.
- **Power budget.** The power budget, including whether VPP generation is on-board or supplied externally, is undecided; it depends on the VPP topology chosen, which itself depends on the divider named in R5 and on a component-cost ceiling that does not exist yet.

This section is shared verbatim with `firestarter/platform/py32f071/FLASH-PATH-AND-PCB.md`; any edit must land in both copies in the same change.

## Flash budget, as actually reserved [SHARED:S3]

**(a) The reserved map, transcribed from the linker script.**

| Region / symbol | Origin | Length | Note |
|---|---|---|---|
| `BOOTLOADER (rx)` | `0x08000000` | `0` | named seam only — see (d) |
| `FLASH (rx)` | `0x08000000` | `120K` | the application occupies `0x08000000` to `0x0801DFFF`, sectors 0 to 14 |
| `CONFIG (r)` | `0x0801E000` | `8K` | Sector 15, pages 480–511 |
| `RAM (xrw)` | `0x20000000` | `16K` | |
| `__config_page_size` | — | `256` | |
| `__config_slot_a_start` | `0x0801E000` | 256 B | page 480 |
| `__config_slot_b_start` | `0x0801E100` | 256 B | page 481, a different page erase unit from slot A |
| `__config_region_end` | `0x08020000` | — | |

`[VERIFIED: read from platform/py32f071/linker/PY32F071xB_FLASH.ld at the firmware HEAD this phase recorded]`. Geometry underneath the table: page size `256` B, sector size `8192` B, main flash `0x08000000` to `0x0801FFFF` — citation chain `platform/py32f071/CONFIG-STORAGE.md` §"Flash geometry" → Puya PY32F07X Reference Manual V0.2 §4.1/§4.2.1/Table 4-1, corroborated against the pinned SDK header constants (`FLASH_PAGE_SIZE`, `FLASH_SECTOR_SIZE`). These are the addresses Phase 126 actually **reserved**, not an estimate — the whole reason this record exists.

**(b) What the application uses today.** `text + data` = `27,372` B of the 122,880 B `FLASH` region, and the vector table is exactly `192 B` (48 entries); these figures come from a **local** build and may be compared only against another local build of the same tree with the same toolchain — never against a CI figure, because the local and CI compilers differ and produced different absolute sizes for the same source `[VERIFIED: local ARM build, delta-comparable only]`.

**(c) The bootloader budget.** Two independent anchors. Puya's own factory bootloader occupies `12,032` bytes of system memory and serves USART, I2C *and* USB DFU without a HAL `[CITED: Puya UM1503/UM1504 §1.1 Table 1-1]`. This tree's own measured objects — the RCC, flash, CherryUSB device-core and port, COBS/CRC framing, CDC glue, GPIO, NVIC, `SystemInit`, timing and startup translation units — already total roughly `14.6 KiB` before any bootloader logic exists `[VERIFIED: per-object arm-none-eabi-size from a local build]`. Not counted: the receive/validate/program state machine, a page staging buffer, image CRC, jump-to-application, descriptor `.rodata`, and newlib fragments — the resulting estimate is roughly 17 to 20 KiB `[ASSUMED — components measured, the bootloader's own logic estimated]`.

The sector-quantised verdict, in the 8 KiB units the part actually erases:

| Reservation | Verdict, and the migration it would cost |
|---|---|
| 1 sector (8 KiB) | Infeasible — the USB, flash and clock objects alone exceed one sector before any bootloader logic exists. |
| 2 sectors (16 KiB) | Reachable only by dropping the HAL for direct register access, as Puya's own bootloader evidently does — not a safe planning figure. |
| **3 sectors (24 KiB) — the defensible reservation** | Roughly 17 to 20 KiB measured-plus-estimated with headroom, leaving `FLASH` at 96 KiB against today's `27,372` B application; reserving it moves the application's `ORIGIN`, a flash-map **migration** rather than a resize — every already-flashed unit needs a full **re-flash** over DFU or SWD before this reservation ever pays for itself. |

**(d) The cost, stated in its corrected form — D-12, as the operator corrected it.** `BOOTLOADER` keeps `LENGTH = 0` today, and giving it a real length is explicitly rejected for this milestone (D-11) — the reservation above is an **intent**, and this paragraph is the cost attached to it, never a number that reads as already paid for. The cost is that giving `BOOTLOADER` a non-zero length moves the application's `ORIGIN`, which is a flash-map **migration** rather than a resize: every already-flashed unit needs a full **re-flash** over DFU or SWD instead of an in-place update — precisely the recovery paths a self-flash bootloader exists to avoid, and it must be paid once before the self-flash path can ever pay for itself.

The correction, restated here because the firmware subset copies this section but not §1.6: the vector table moving is the **cheap** half, because the part declares `__VTOR_PRESENT` and the compiled `SystemInit` writes `SCB->VTOR` at every boot — earlier statements in this project's planning record and in the linker comment attributed the cost to the absence of that register and were wrong; §1.6 records how the correction was established, and Phase 130's `CLOSE-01` sweep owns the `REQUIREMENTS.md` and `ROADMAP.md` prose. This record does not enumerate any no-VTOR workaround scheme here: those exist for parts that genuinely lack the register, and describing them would document a problem this part does not have.

**(e) Supersessions and hand-offs.** This section supersedes the seed's "first few KB" bootloader-size figure specifically, and no other part of the seed. `CONFIG`'s 7680 B of deliberate slack is reclaimable by FUT-N05 or by additional config slots without moving any address. FUT-N05 owns the bootloader itself. FUT-N06 owns the raw-binary release asset a self-flash protocol would want.

This section is shared verbatim with `firestarter/platform/py32f071/FLASH-PATH-AND-PCB.md`; any edit must land in both copies in the same change.

## USB vendor and product identity [SHARED:S4]

**(a) What the descriptor currently presents, and where it came from.** `platform/py32f071/src/usb_cdc.c` defines `FIRESTARTER_USB_VID 0x1209U` at line 20 and `FIRESTARTER_USB_PID 0x0001U` at line 24, consumed inside the `USB_DEVICE_DESCRIPTOR_INIT` call that builds `firestarter_cdc_descriptor` `[VERIFIED: platform/py32f071/src/usb_cdc.c, this phase's commit]`. `0x1209` is the pid.codes community vendor id, and `1209:0001` is that registry's documented private-testing product id — **not** an allocated PID (see (b) for the terms). The values are no longer undocumented *in this tree*: `usb_cdc.c` now carries a provenance-and-warning comment directly below the two `#ifndef`/`#endif` guards, wording the pid.codes obligation as the terms **asking** that source referencing `1209:0001` warn that the PID is not universally unique — never as a requirement `[RESEARCH C-6: the terms say should, not must]`. What it replaced, retained as the record of what the board previously presented: the pair `0x36B7`/`0xFFFF`, with `0x36B7` registered to Puya Semiconductor (Shanghai) Co., Ltd. `[CITED: the-sz.com USB ID database, accessed 2026-08-02 — single-source for the allocation holder]`, copied verbatim from the pinned SDK's own USB CDC example, `Projects/PY32F071-STK/Applications/USB_Device/USBD_Virtual_COM_Port/Src/usbd_cdc_if.c` lines 9–10 (`#define USBD_VID 0x36b7` / `#define USBD_PID 0xFFFF`), whose companion Windows driver INF `pycdc.inf` lines 28 and 31 matched it (`USB\VID_36B7&PID_FFFF`), both at `GIT_TAG 0ed2f4b4d3391eccfd4491006a30295fd78e32c2` `[VERIFIED: pinned SDK blobs]`. The consequence in one sentence a reader cannot misread: until this change, the board did not squat an empty slot — it presented **another company's registered vendor identity**, specifically the silicon vendor's, on a product they did not make. Every PY32 project that starts from Puya's CDC example without changing the descriptor presents the same pair, so a collision between two such devices on one host was, and for those other projects remains, a common failure mode rather than a hypothetical one.

**(b) The decision.** pid.codes, vendor id `0x1209` — the established free registry for open-source hardware. The **target** identity is an allocated `1209:<pid>`. The **interim** identity is `1209:0001`, the registry's documented private-testing product id, whose own terms permit exactly this use and forbid shipping: "This PID is reserved for use in private testing. Anyone may assign it to their device while they're testing in-house, but it MUST NOT be used on any device that will be redistributed, sold, or manufactured. Source code and configuration that references this VID/PID should warn users that the PID is not universally unique and should not be used outside test environments." The interim id is strictly better than the status quo: it replaces another company's registered identity with one explicitly sanctioned for this use, and it does not weaken the ship gate, because the test id's own terms forbid shipping.

**(c) The ship gate.**

**Ship gate: no PY32F071 board ships, and no release advertises a USB identity, until a PID allocated under VID 0x1209 exists.**

This is deliberately a condition rather than a warning, so a future reader can fail it.

**(d) What this phase does and does not change.** `usb_cdc.c` **is** edited, in v1.23 Phase 130 under D-11, and this **reverses Phase 129 D-06**, which declined the same edit only because that phase was documentation-only with no cut planned. Record the reversal *as* a reversal, with its constraint named: publishing an image is the new fact that D-06 did not have to weigh. PCB-04's clause that `usb_cdc.c` itself stays unedited **this phase** is Phase-129-scoped and remains true as written; `REQUIREMENTS.md` is not amended for it — the reversal is recorded in `130-LEDGER.md` and `130-NONREGRESSION.md` instead.

The ordering claim is inverted from what this paragraph previously said: an interim, non-allocated id lands **before** any allocation, precisely so a published image does not present a third party's identity while the allocation waits on a schematic — (e) below rates that confidence LOW.

The ship gate in (c) is **unchanged and still binds**: no board ships and no release advertises a USB identity until a PID allocated under VID `0x1209` exists. Whether a caveated disclosure of an explicitly non-allocated test id counts as *advertising a USB identity* is a judgment a future reader may decide differently — which (c) deliberately permits — and the reasoning is recorded in `.planning/phases/130-close-honesty-ledger-claim-gate-release-decision/130-DECISION.md`'s pre-flight section and carried as a negative-space row in `130-LEDGER.md`, rather than resolved by amending the gate.

The host-side fact, retained unchanged in substance so PCB-04 is never read as a host bug: `firestarter_app/firestarter/py32_dfu.py`'s `find_dfu_interfaces` discovers DFU devices by **interface class** `0xFE/0x01`, not by vendor and product id, because the identity the Puya bootloader presents is not confirmed. Also, from the same family of confusions: `0x0448` is a bootloader-parameter table **device** id sitting beside a separate bootloader id column (Puya UM1504 Table 1-1) — it is not a USB product id, and this record confirms `REQUIREMENTS.md` §"Out of Scope"'s note ("Hardcoding `--usb-id 0448` as a default") is correct on that point. One further line, also retained: the datasheet §2.3 describes the boot loader as downloading through the USART interface and does not mention USB, while UM1504 documents USB DFU for this part — UM1504 is the authority for the USB DFU capability, and a reader who checks the datasheet first should not conclude the DFU path is imaginary.

The descriptor change needed an ARM pass before the merge: Task 3 of this plan ran one locally, and a local build supports a **delta** claim only, about this one changed translation unit — never an absolute size, which requires a CI run URL plus SHA `[D-07]`.

**(e) How the allocation is obtained, and what it depends on.** Fork the `pidcodes/pidcodes.github.com` registry, add an organisation page and a product-id page carrying a named open-source license, open a pull request; ids are allocated in the order pull requests are submitted. Two facts CONTEXT.md does not account for.
*Sequencing:* the registry's own `howto.md` requires a publicly available repository containing modifiable PCB design files as well as source, and says to hold off requesting an id until the project meets them — so the request may not be fileable until a schematic exists, putting the allocation downstream of the very board the ship gate protects. The registry's FAQ is softer, saying a project need not have shipped; the two are in mild tension and are resolved case by case, so confidence that a firmware-only request would be accepted is **LOW** `[ASSUMED — the howto and the FAQ disagree; the operator can resolve it by asking]`.
*Latency:* `[VERIFIED: GitHub API, measured 2026-08-02]` 64 open pull requests, the oldest opened 2026-01-20, and no merge to the registry's default branch since 2026-04-29. These are volatile figures that must be re-read before being relied on. The practical consequence: treat "file the pull request" and "receive the id" as two separately tracked events, and file early.

**(f) Who does what.** The phase decides the route; it does not request the id. The allocation is a public pull request filed under the operator's name — **no agent files it** (D-08). It is tracked in §8.

This section is shared verbatim with `firestarter/platform/py32f071/FLASH-PATH-AND-PCB.md`; any edit must land in both copies in the same change.

## Socket empty before any PY32F071 firmware install [SHARED:S5]

**Before any PY32F071 firmware install — DFU, SWD or otherwise — the PROM socket must be empty.**

This instruction is stronger here than the comparable warning elsewhere in this project, for four reasons.

1. **The map is provisional by declaration, not by omission.** `include/boards/py32f071_rurp_shield.h` defines `RURP_PY32F071_PINMAP_PROVISIONAL` as a marker, and `platform/py32f071/README.md` states that every assignment other than the ADC channel is a placeholder chosen for a simple contiguous bus. The map is provisional and unconfirmed against any board — nothing here asserts anything about its correctness.
2. **The specific hazard is direction, not just identity.** A provisional map can assign a signal the wrong **direction**. A pin the real board wires to a PROM output, configured by the firmware as a push-pull output, creates a driver fight through the PROM's output stage — which destroys parts silently, on first power-up. This is qualitatively different from the AVR comparable, where the mapping has accumulated three board revisions of bench measurement since v1.0.
3. **The startup levels are asserted but unmeasured.** The README lists safe active-low `/CE` and `/OE` startup levels among what is implemented, and then, three sections later, lists validating those same startup levels among what is still required. Record the tension rather than picking a side: the code intends safety, and nothing has measured it `[UNVERIFIED-UNTIL-SILICON]`.
4. **A DFU install is the acute case.** Installing over DFU power-cycles and re-enumerates the board with `BOOT0` strapped high — a state in which the application's GPIO initialisation never runs at all. Whatever the factory bootloader leaves the pins in is what a seated chip sees. Neither the application's startup levels nor any Firestarter code is in control during a DFU install, which is precisely when a user is most likely to have a chip in the socket from the previous session.

**Placement.** This instruction is repeated verbatim in the firmware subset (`platform/py32f071/FLASH-PATH-AND-PCB.md`), and as a pointer plus the same one-sentence instruction in `platform/py32f071/README.md` §"Hardware validation still required", which is the nearest existing text a bring-up reader already opens. Propagating it into the host installer documentation (`firestarter_app/doc/PY32F071-FIRMWARE-INSTALL.md`), and any installer-time prompt, is **out of scope this phase** because the host repository is untouched (D-04); it is tracked as an obligation in §8.

This section is shared verbatim with `firestarter/platform/py32f071/FLASH-PATH-AND-PCB.md`; any edit must land in both copies in the same change.

## Claim ceiling

No PY32F071 hardware exists as a physical board, and nothing recorded in this document is a claim about behaviour observed on that silicon. The figures above are quoted from published reference documents, corroborated from the pinned SDK's own sources, read from files already in this tree, or measured in a local build performed for this phase. The ARM toolchain is not installed by default in this development environment, but is installable from the same packages CI uses; a local build supports **delta** claims only — same tree, same toolchain, two builds — never an absolute-size comparison against a CI figure, nor any claim that the image runs. The full list of claims this milestone may and may not make is recorded once, in `.planning/REQUIREMENTS.md` §"Validation Ceiling", and this document defers to that list by reference rather than restating its wording.
