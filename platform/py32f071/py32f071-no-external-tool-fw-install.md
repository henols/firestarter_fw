---
title: PY32F071 firmware install with no external tools (self-flash bootloader over the existing transport)
trigger_condition: v1.28 PY32F071 Port is activated, OR the first PY32F071 PCB/schematic is specified — whichever comes first, because this decision imposes PCB requirements
planted_date: 2026-07-28
status: partially realised — the factory-USB-DFU runner-up shipped in v1.23 (the trigger fired); the self-flash primary route has not, and the seed stays live for FUT-N05
---

# PY32F071 firmware install with no external tools

How `firestarter fw --install` should reach a PY32F071 board, given the operator
constraint stated during /gsd-explore on 2026-07-28:

> "I want the most reliable and simple approach without having to install
> external tools."

and the fact that **no PCB exists yet** (paper-only, confirmed same day) — which
means the flash path can be *designed into* the board instead of worked around.

Evidence for the current branch state and the host-side seams this plugs into:
[`notes/py32f071-port-branch-state.md`](../notes/py32f071-port-branch-state.md).

## Decision: a self-flashing bootloader over the transport the app already speaks

A small bootloader in the first few KB of the 128 KiB flash, speaking the **same
USB CDC + COBS framing** the firmware already uses. The app sends the new image
over the serial port it already owns; the bootloader writes it to the application
region.

**Zero new host dependencies.** `pyserial` is already a firestarter_app
dependency. `_install_with_avrdude` is replaced by pure Python over the existing
transport — no binary discovery, no `avrdude.conf` analogue, no driver install.

This is structurally identical to how the Uno works today: bootloader preloaded
once at manufacture, then flashed over serial forever after. The PY32 self-flash
bootloader is the moral equivalent of the Arduino bootloader.

### Why every factory-bootloader route was rejected

The part *does* have a capable factory bootloader — Puya **UM1504** documents USB
DFU for PY32F071/F072/F403: system memory `0x1FFF0000–0x1FFF2F00`, USB on
PA11/PA12, DEV/PID `0x0448`, BL ID `0xA0`, entered with **BOOT0 high +
nBOOT1 = 1**. Reaching it from the host is the problem:

| Route | Why rejected |
|---|---|
| Puya `PY32DfuTool` | **Windows x64 only.** Unusable from a cross-platform Python CLI. |
| `dfu-util` | External binary — same PATH-discovery burden as avrdude, which is the thing the constraint is aimed at. |
| Vendored Python DFU over `pyusb` | Needs libusb. On Windows, raw USB access to a DFU device means installing a WinUSB driver via **Zadig** — an external tool install in all but name. Best *runner-up* if the bootloader work is deferred. |
| `puyaisp` (UART ISP) | Only needs `pyserial`, but drives the **UART** bootloader through a second USB-to-serial converter wired to PA2/PA3 (or PA9/PA10, PA14/PA15), with BOOT0 (PF4) high and nRST (PF2) pulsed. A dongle plus test-point access on a board that *has* native USB — a UX regression from today's single cable. |

Note the shared defect: the factory bootloader is entered by a **pin condition**,
so without firmware help every update is a button dance.

### Reliability comes from the standard pattern, not from optimism

- Bootloader **never self-updates** — it is written once and is not in the
  update path.
- Application image is **CRC-verified before the jump**; a failed CRC falls back
  into the bootloader rather than jumping into a half-written image.
- An interrupted transfer leaves the board in the bootloader, which is a
  *recoverable* state, not a brick.

## PCB requirements this imposes (the reason the trigger includes "first schematic")

Because the board is still paper, these are cheap now and expensive later:

1. **BOOT0 / nBOOT1 strapping** reachable (jumper, pad or button) so the factory
   USB DFU path stays available as a maintainer/manufacturing recovery route.
2. **SWD pads** exposed — the unconditional last resort.
3. **Contiguous 8-bit GPIO port** for the PROM data bus, so the one-snapshot
   `IDR` / atomic `BSRR` design on PR #48 holds. PR #48's provisional map uses
   PB0–PB7; confirm against the final package and pin multiplexing.
4. **Flash budget**: bootloader + application + the dual-slot CRC config region
   must fit 128 KiB with room to spare. Reserve the layout before writing either
   half.
5. Decide whether the app-side "reboot into bootloader" is a **protocol command**
   (preferred, matches the Leonardo 1200-baud-touch shape at `avr_tool.py:115`)
   or a strap-only operation.

External tools are acceptable on the recovery path — that path is for the
maintainer and the factory, never for an end user.

## Status: the USB DFU half is implemented (2026-07-28)

The operator asked for the USB install to be built rather than only designed, so
the **factory-USB-DFU** route — the runner-up in the table above, not the
self-flash bootloader — now exists on `firestarter_app` branch
`feature/py32f071-fw-install` @ `311eacf` (off `beta`), queued as milestone
**v1.29** in [`ROADMAP.md`](../ROADMAP.md). It is a pure-Python DFU client
(`firestarter/py32_dfu.py`), so no external *binary* is needed; the residual cost
is `pyusb` + a libusb backend, and a WinUSB driver on Windows.

That does **not** retire this seed. The self-flash bootloader is still the only
route with zero host-side USB plumbing, and it is what removes both the driver
friction and the `BOOT0` strap. The DFU path shortens the road to it rather than
replacing it: it proves the transfer sequence, and it stays as the
maintainer/manufacturing recovery route the PCB requirements below already
assume.

Operator-facing procedure and bootloader-entry table:
`firestarter_app/doc/PY32F071-FIRMWARE-INSTALL.md` on that branch.

**Phase 129, 2026-08-02.** This trigger has now fired, and half of this seed
shipped while the other half did not.

1. **The trigger fired.** The port milestone above was activated — as
   **v1.23**, not the slot it was originally queued under (see point 5 below)
   — so the first of this seed's two trigger disjuncts has been met. The
   second disjunct, a specified PCB or schematic, has **not**: the board is
   still paper, which is exactly why the PCB consequences this seed already
   lists were recorded now, in a phase of that same milestone, rather than
   waiting for a schematic that does not exist yet.
2. **What landed and what did not.** The **factory-USB-DFU** route — the
   *runner-up* in this seed's own rejected-routes table above, never the
   decision — is what shipped in v1.23, exactly as the paragraph above this
   note already says. The **self-flash bootloader**, this seed's *primary*
   route, did **not** land. Landing the runner-up does not retire this seed:
   the sentence two paragraphs above already states this in the seed's own
   terms, and it remains true.
3. **Where the decision now lives.** [`../v1.23-FLASH-PATH-DECISION.md`](../milestones/v1.23-FLASH-PATH-DECISION.md)
   is now **canonical** for the flash-path decision: it is the only document
   citing the flash addresses Phase 126 actually reserved, and it carries the
   PCB checklist that this seed's five PCB requirements above fed into.
   **FUT-N05** is the requirement that owns the remaining self-flash
   bootloader work.
4. **One number this seed no longer gets to assert.** The record's
   §"Flash budget, as actually reserved" supersedes this seed's estimate,
   above, that the bootloader fits "in the first few KB" of flash: this
   tree's own measured objects that such a bootloader must carry already
   total roughly 14.6 KiB before any bootloader logic exists, and the
   record's flash-budget section holds the replacement figure — a 3-sector
   (24 KiB) reservation — with its migration cost attached. This
   supersession is scoped to that one number only; the rest of this seed
   stands.
5. **One stale reference, flagged rather than rewritten.** The paragraph
   above this note refers to the DFU work as "queued as milestone v1.29" in
   `ROADMAP.md`. That reference is stale — the work was activated as
   **v1.23** instead (point 1 above) — and correcting it belongs to
   **Phase 130 CLOSE-03**, which owns the milestone-slot renumber together
   with the stale prior-art correction. It is flagged here so a reader is
   not misled, and is deliberately not rewritten in this phase: two phases
   editing the same claim in different directions is how a correction gets
   lost.
6. **One open question this seed can now close by reference.** The PCB
   requirement above about confirming the contiguous data bus against the
   final package and pin multiplexing is discharged by the record's PCB
   checklist row **R3** ("Contiguous PB0–PB7 data bus, and a package that can
   carry it"), which names the packages that cannot carry the bus at all
   (`QFN56`, `QFN32`) and those that can (`LQFP64`, `CSP64`, `QFN64`,
   `LQFP48`, `QFN48`).

## Open questions

- Does the Puya factory USB bootloader enumerate as something `dfu-util` can
  actually drive (standard DFU 1.1 vs DfuSe-flavoured, and its real USB VID/PID —
  UM1504's `0x0448` is a device ID, not necessarily the USB PID)? Answerable
  with one `lsusb` + one `dfu-util -l` **once silicon exists**. Needed for the
  recovery path even though self-flash wins the primary path.
- Can the application firmware jump to `0x1FFF0000` in software on PY32F071
  (Cortex-M0+ VTOR / SYSCFG `MEM_MODE` remap, STM32F0-style), or is BOOT0 the
  only entry? Determines whether recovery needs physical access.
- Does the host asset pattern need `.bin` alongside `.hex`? The pattern
  `firestarter_{board}.hex` at `firmware.py:155`/`:237`/`:336` bakes in the
  extension; a self-flash protocol most likely wants raw binary.
- Ordering vs **v1.26 White-Box Voltage-Reading Calibration**: the ROADMAP
  already sequences v1.28 after it so the VREFINT + two-point calibration model
  is defined once. Closed-loop DAC VPP (PR #45) partly supersedes v1.26's
  hand-set-pot procedure (`feedback_operator_adjusts_pot_solo`) — resolve which
  one owns the model before either is planned.

<!--
VENDORED SEED -- do not edit in place.

Source: the meta repository henols/firestarter, `.planning/seeds/py32f071-no-external-tool-fw-install.md`
Source blob: 76ed3093eca33fd7b5e2c0ee351cfecbab662365
Vendored:    2026-09-27, at meta commit 1ad9f2ee (the tip of origin/beta)

Vendored alongside v1.23-FLASH-PATH-DECISION.md when the meta repository's `.planning/` tree was
retired, so that tests/test_flash_path_record_sync.py reads only files inside its own repo -- the
rule in agent-os standards/testing/standalone-checkout.md.

The provenance is a FOOTER, not a header, because this file's YAML frontmatter must begin on line
1: _frontmatter() refuses to guess where the block starts, and a header here made it raise.

Internal relative links (for example `../milestones/...`) point at the original `.planning/`
layout and are historical. They are kept verbatim so the record's content is unaltered.
-->
