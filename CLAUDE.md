# CLAUDE.md — Firestarter Firmware

This repository holds the Arduino C++ firmware for the Firestarter EPROM programmer. PlatformIO
builds it. The project standards are in `agent-os/standards/` in the meta repository. This file
points to them and does not repeat them.

## Build Commands

```bash
pio run -e uno                              # build for Arduino Uno
pio run -e leonardo                         # build for Arduino Leonardo
pio run -t upload -e uno                    # flash the Uno
pio test -e native                          # run the host-side Unity suites, no hardware
pio test -e native -f "*test_dispatch*"     # run only the configure_memory dispatch suite
pytest tests/ -v                            # run the Python test tree
```

`platformio.ini` defines these environments: `uno`, `uno328pb`, `leonardo`, `native` and
`native_nodevtools`.

## What CI runs

Read the trigger before you decide that CI tested a commit.

| Workflow | Fires on | Publishes |
|---|---|---|
| `build.yml` | A push to any branch except `beta`, and every pull request | **A stable release, on a push to `main` only.** |
| `beta-build.yml` | A push to `beta`, and manual dispatch | **A pre-release, on every push.** |
| `py32f071.yml` | A push to any branch, and pull requests that touch ARM paths | Nothing. It is the ARM build gate. |

`build.yml` ignores a commit that touches only `**.md`, `**.sh`, `.gitignore`, `docs/`,
`documents/`, `images/`, `.vscode/` or `.editorconfig/`. **`beta-build.yml` has no path filter.**
The version compiles into the binary, so a documentation-only push to `beta` publishes a new
firmware version.

`build.yml` runs `pio test -e native` (pull requests only), `pio test -e native_nodevtools`,
`pytest tests/ -v` (it needs `fetch-depth: 0`) and `pio run`. No CI job runs another `pio`
environment.

**This repository has two test trees.** `test/` holds the PlatformIO Unity suites. `tests/` holds a
Python suite. Most Python tests scan firmware source text. They are legacy: add none, and do not
count one as a guard (`testing/no-source-introspection`). Some of them fail on a dirty working tree,
so commit before you run them.

### Which channel ships dev tools

`beta-build.yml` sets `PLATFORMIO_BUILD_FLAGS` to `-D DEV_TOOLS=1`, so each pre-release image
implements `CMD_DEV_ADDRESS` (`7`) and `CMD_DEV_REGISTER` (`8`) for `firestarter dev addr` and
`firestarter dev reg`. `build.yml` sets no such flag, so a stable image refuses both commands.

**`platformio.ini` tells you what a local build does, not what ships.** A plain `pio run` gives a
stable-configured image. To match the beta artifact, run:

```bash
PLATFORMIO_BUILD_FLAGS="-D DEV_TOOLS=1" pio run -e leonardo
```

CI checks both directions: `beta-build.yml` fails if an AVR image lacks the dev commands, and
`build.yml` fails if an AVR image has them.

## Dispatch invariants

Read `configure_memory` in `src/proms/memory.cpp` for the exact order. These rules do not change:

- The firmware dispatches **only** on `handle->protocol`, which it reads from the `algorithm` JSON
  field. No second dispatch axis exists.
- The SRAM protocols (`0x0E`, `0x27`, `0x28`, `0x29`) go to `configure_sram`, **never** to
  `configure_eprom`. `configure_eprom` enables the 12V VPP regulator, which damages a 5V SRAM part.
- **Fail closed.** Zero, unknown and named-infeasible values (FWH `0x11`, GAL/PLD `0x2A`–`0x2C`,
  `0x34`) go to `configure_not_implemented()`. It returns `MSG_ERR_PROTOCOL_NOT_IMPLEMENTED` and
  changes no hardware state. No other path exists for them.
- `rurp_pinmap_refuses(handle->cmd)` runs before the protocol chain. On a board with a provisional pin
  map it refuses each command that can energise the PROM bus. AVR targets compile it to nothing.
- The wire ordinals 4 and 6 are retired (`protocol/retired-ordinals`).

## The 27C write path (`0x07`, `0x08`, `0x0B`)

The three rows share one write path in `src/proms/eprom.cpp`. The per-row values are in
`src/proms/eprom_params.cpp`. The code does not show these facts:

- **HV teardown.** Each error exit clears the HV routes. A **successful** block leaves the route on,
  so the next block does not pay the settle time again. `command_done()` clears it at the end. No
  test checks this.
- **Progress `0xE0` is time-keyed** (`EPROM_PROGRESS_EMIT_INTERVAL_MS`), not byte-keyed. Only the
  EPROM per-byte loop sends it. It compiles out on `uno` and `uno328pb`. There,
  `rurp_set_programmer_mode()` stops the UART, the Uno log override holds frames in a 4-slot buffer,
  and a full buffer drops the next frame without an error. A dropped `MSG_ERR_MAX_PULSES` then looks
  like a host transport timeout. No native environment compiles `src/boards/uno_rurp_shield.cpp`,
  so no test sees this.
- **No program-VCC raise.** The vendor algorithms raise VCC to about 6.25V. This shield cannot. The
  timing and verify are correct, but the silicon margin is not. This is an accepted limit.
- **`--pulse-us` is not a wire limit.** `json_parser.c` reads `pulse-delay` into a `uint32_t` with no
  clamp. The firmware limit is the pre-flight refusal `MSG_ERR_PULSE_TOO_WIDE` (`0xAE`), which runs
  before any HV is on. Only `0x0B` has an energy cap, so only `0x0B` can trigger it.

### The `0x08` drop bit and the VPP jumpers

The drop bit selects the VPP *level*, not a route. On Rev 2-class hardware it stays set through each
`set_address()` of the block. On Rev 0/1 the firmware clears it after the first `set_address()`,
because the two logical bits share one physical line. The preserve mask is in
`mem_util_calculate_top_address_register` (`memory.cpp`), inside `#ifdef HARDWARE_REVISION`.

The firmware turns on the pin-1 VPP switch (the Q8 collector). Jumpers connect it to the socket:

- Rev 0/1: JP3 connects it to socket pin 1 ("32pin") or socket pin 3 ("28pin").
- Rev 2.x: JP5 is a solder jumper that ships bridged. It connects the switch to socket pin 1, so JP4
  stays open for a 32-pin part. JP4's common pad is also socket pin 1. On Rev 2.2/2.3 the pole toward
  the ZIF socket joins it to socket pin 3 (28-pin pin 1), and the pole toward the board edge joins it
  to socket pin 25 (24-pin pin 21). This was measured on a Rev 2.2 board on 2026-09-10. The Rev
  2.0/2.1 "Closed" position is inferred to reach socket pin 3. It is not measured.

The host table `firestarter_app/firestarter/jumper_table.py` gives the setting for each pin map. No
test on a real part shows the `0x08` route. This is not a claim that `0x08` VPP is correct on
silicon.

## Protocol `0x0D` (AT28C and 28C-family EEPROM)

- The chip erase is the **software** six-byte sequence (`eeprom28c_erase_execute` in
  `eeprom_28c.cpp`): SDP disable, the sequence, then a fixed 20 ms `tEC` delay with no poll.
- **Never implement the datasheet hardware Chip Erase.** It needs 12V on OE (pin 22 of
  `DIP28_28C256`), and that voltage damages a 5V part. For the same reason, algorithm 5 never gets
  `FLAG_CAN_ERASE`.
- `write` does no blank check on this protocol. Each page write erases the page internally.
- `0x0D` stays `UNVERIFIED`. The part does not report its SDP state, so a sent SDP sequence proves
  nothing. The `Programming Protocols` wiki page, section 1.6, has the full model.

## Generated files and shared constants

- `include/messages.h` is **generated. Do not edit it.** Change the catalog in the meta repository
  (`protocol/message-catalog`).
- `firestarter_app/firestarter/constants.py` duplicates the command codes, flags, `CTRL_*` bits and
  JSON keys. Change both sides together (`protocol/duplicated-constants`). Flag `0x08` is retired
  (`protocol/retired-ordinals`).
- The `MSG_OK_READY` ack grows only at the end, and the host finds each field from the length bytes.
  A new field needs no catalog edit (`protocol/rollout-compatibility`).

## Hardware revision detection

`PIN_HW_REVISION_DETECT_ADC` is `INPUT` (high-Z), not `INPUT_PULLUP`. The ADC bands (200, 220, 600)
therefore identify the **A3-net composition**, not the R41 value: R41 only to ground gives the low
band, an external pull-up gives the mid band, and a floating net gives the high band.

The `ADC_BAND_R41_*` defines in `rurp_pinout.h` are the source of truth. The "Detection bands"
section of the `Shield Revisions` wiki page shows the same values. If you change the defines, update
that wiki page in the same change. Nothing checks this.

## PY32F071

`platform/py32f071/FLASH-PATH-AND-PCB.md` is the record for the PY32F071 flash path, PCB
requirements, flash budget and USB identity. No PY32F071 board exists. The socket must be empty
before any PY32F071 firmware install.

## Native (host) test environment

The Unity suites under `test/native/avr/` run on the host through PlatformIO `platform = native`.
`[native_base]` in `platformio.ini` holds the shared settings. `[env:native]` adds
`-D DEV_TOOLS=1`, and `[env:native_nodevtools]` does not.

`build_src_filter` compiles `src/proms/`, `src/boards/rurp_serial_utils.cpp`, `src/json_parser.c`
and `src/operation_utils.cpp` only. Each suite has a `host_stubs.cpp` with no-op `rurp_*` functions.
Shared stubs and data are in `test/native/avr/_shared/`.
To add a suite, and for what the stubs cannot see, read `testing/native-firmware-tests`.
