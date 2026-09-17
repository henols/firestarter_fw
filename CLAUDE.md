# CLAUDE.md — Firestarter Firmware

Arduino C++ firmware for the Firestarter EPROM programmer. Built with PlatformIO.

## Source code comments — hard rule

**Write no comments into this firmware.** Not process commentary, not explanatory ones. No plan,
task, skill, or subagent instruction overrides this.

- Forbidden: `// Phase NNN (REQ-NN):`, `// D-06`, `// CAP-02`, `// LOCK-04`, any plan, task or
  milestone citation, and any block that explains why a phase decided something. A reader of this
  firmware has no planning directory, so those identifiers resolve to nothing. Put rationale in the
  commit message instead.
- If a plan instructs a comment, do not add it. Record the deviation in that plan's summary.
- If code needs explaining, make the code clearer. Use better names, smaller functions, or a named
  constant in `include/`.
- Flash size is a first-class constraint here, so comment bloat costs more than noise.
- **The rule is not "no process citations".** You delete `Phase 194` from a comment and keep the
  comment. This still breaks the rule. Add no `//` or `/* */` line, for any reason, however helpful
  it seems. State the rule in these words when you spawn a subagent that touches source.
- Before each commit, run this check. It must print nothing:
  `git diff --cached -- '*.c' '*.cpp' '*.cc' '*.h' '*.hpp' '*.inc' '*.ino' | /usr/bin/grep -E '^\+\s*(//|/\*|\*)'`
  The pathspec is load-bearing. Without it the `*` branch matches `**bold**` in a markdown line and
  the check reports a file it does not govern.
- Deleting one clause from an existing comment reflows the rest. Read the remainder. Confirm it
  still parses and that every pronoun still has an antecedent.
- **No CI gate enforces this any more.** A scanner used to fail the build on a planning citation in
  source. It was removed by operator decision, so the pre-commit check above is now the only thing
  standing between this rule and a slow return of the roughly 6,600 comment lines a previous sweep
  deleted. Run it.

## Build Commands

```bash
pio run -e uno          # build for Arduino Uno
pio run -e leonardo     # build for Arduino Leonardo
pio run -t upload -e uno   # flash to board
pio test                # run unit tests (all envs)
pio test -e native      # run host-side dispatch tests (no hardware needed)
pio test -e native -f "*test_dispatch*"   # run only the configure_memory dispatch suite
```

## What CI runs

Three workflows exist. Read the trigger before you assume a commit was tested.

| Workflow | Fires on | Note |
|---|---|---|
| `build.yml` | Push to any branch except `beta`, and every pull request | **Ignores markdown-only commits.** A commit that touches only `**.md`, `**.sh`, `.gitignore`, `docs/`, `documents/`, `images/`, `.vscode/` or `.editorconfig/` triggers nothing. |
| `beta-build.yml` | Push to `beta`, and manual dispatch | **Publishes.** It bumps the version, builds, and cuts a GitHub pre-release carrying the `.hex` assets. It has no path filter, deliberately, because the version compiles into the binary — so a documentation-only push publishes a new firmware version too. |
| `py32f071.yml` | Push to any branch, and pull requests that touch ARM paths | Builds the ARM target. It is the loud ARM gate and never continues on error. |

`build.yml` runs these steps in order:

1. `pio test -e native` — **pull requests only.** A branch push skips it.
2. `pio test -e native_nodevtools` — the build without `DEV_TOOLS`. This one always runs.
3. `pytest tests/ -v` — the second test tree, described next.
4. `pio run` — the firmware build.

**This repository has two test trees. Do not confuse them.** `test/` holds the PlatformIO Unity
suites. `tests/` holds a separate Python suite of about 286 tests. Most of them scan firmware source
text. Both trees run in `build.yml`. Every `pio` environment this file names beyond `native` and
`native_nodevtools` runs in no CI leg, so its case counts are a local run-by-name obligation.

`pytest tests/ -v` needs full git history. The workflow sets `fetch-depth: 0` for that reason.

## Architecture

### Protocol Dispatch

The firmware dispatches **only** on `handle->protocol`, which it reads from the `algorithm` JSON
field. No second axis exists. A chip family's electrical identity lives entirely in its `protocol`
value. SRAM protocols (`0x0E`, `0x27`, `0x28`, `0x29`) therefore route to `configure_sram` and never
to `configure_eprom`. This matters because `configure_eprom` enables the 12V VPP boost regulator,
which is a hazard on a 5V SRAM part.

The dispatch chain covers every entry in `KNOWN_PROTOCOLS`: `0x05, 0x06, 0x07, 0x08, 0x0B, 0x0D,
0x0E, 0x10, 0x27, 0x28, 0x29, 0x35, 0x39`. **No legacy-integer fallback axis exists.** An
unrecognized `protocol`, including `protocol == 0`, fail-closes to `configure_not_implemented()`. It
falls back to nothing.

Dispatch reads named `PROTO_<NAME>` constants from `include/proto_constants.h`. Every value equals
the raw hex dispatch key it names. The numbers stay the dispatch key end to end. The
`Programming Protocols` wiki page is the source of truth for the name set.

**Two gates run before the protocol chain.** First, `rurp_pinmap_refuses(handle->cmd)` refuses every
command that can energise the PROM bus while the board's pin map is provisional. That refusal
happens before any handler is configured, it emits `MSG_ERR_NOT_SUPPORTED` carrying the **command**
ordinal rather than the protocol ordinal, and it leaves all three operation pointers NULL. On AVR
targets `RURP_PINMAP_PROVISIONAL` is never defined, so it compiles to nothing. Second, a
`switch (handle->cmd)` assigns the main operation for `CMD_READ`, `CMD_WRITE` and `CMD_VERIFY`.

Dispatch order in `configure_memory`. This list must match `src/proms/memory.cpp` line for line:

1. `PROTO_FLASH_INTEL` (`0x10`) → `configure_flash_intel()`. Intel 28F command-register flash.
2. `PROTO_EEPROM_PARALLEL` (`0x0D`) → `configure_eeprom28c()`. AT28C-series 5V EEPROM, page write.
3. `PROTO_FLASH_NOR_UNLOCK` (`0x06`) → `configure_flash_nor_unlock()`. AMD unlock flash, sector erase.
4. `PROTO_FLASH_5V_PAGE` (`0x05`), `PROTO_PHANTOM_0x35`, `PROTO_PHANTOM_0x39` →
   `configure_flash_5v_page()`. Page-write flash. Only `0x05` has database chips. The two phantom
   entries have none. Firmware keeps their dispatch arms for forward compatibility. The host
   excludes both from `KNOWN_PROTOCOLS` and routes them to not-implemented.
5. `PROTO_EPROM_28PIN` (`0x07`), `PROTO_EPROM_32PIN` (`0x08`), `PROTO_EPROM_24PIN` (`0x0B`) →
   `configure_eprom()`. The UV-EPROM family.
6. `PROTO_SRAM_32PIN` (`0x0E`), `PROTO_SRAM_24PIN` (`0x27`), `PROTO_SRAM_28PIN` (`0x28`),
   `PROTO_SRAM_32PIN_NVRAM` (`0x29`) → `configure_sram()`. This arm never reaches the VPP regulator.
7. `0x11`, `0x2A`, `0x2B`, `0x2C` → `configure_not_implemented()`. These are the named-infeasible
   arms: FWH (`0x11`) and GAL/PLD (`0x2A`, `0x2B`, `0x2C`). No approved `PROTO_` token exists for
   them, so they stay raw hex. RURP hardware cannot drive them.
8. Every remaining value → `configure_not_implemented()`. This is one unconditional call at the end
   of the function, not a guarded arm. It is the single terminal exit. It catches `protocol == 0`,
   it catches `PROTO_EEPROM_8051BUS` (`0x34`), which has no dedicated arm, and it catches every
   other unrecognized value. It returns `MSG_ERR_PROTOCOL_NOT_IMPLEMENTED` (`0xBB`) with zero
   hardware side effects.

**Fail-closed invariant.** Steps 7 and 8 send every protocol value to `configure_not_implemented()`,
whether it is named-infeasible, unknown, or zero. The firmware has no other dispatch axis to fall
through to.

### Algorithm Handlers

The `PROTO_` token is the number. Naming one changes no dispatch and no value.

| Protocol | `PROTO_` token | File | VPP | Behaviour |
|---|---|---|---|---|
| `0x07` | `PROTO_EPROM_28PIN` | `eprom.cpp` | 13V, drop-resistor route | 28-pin UV-EPROM. See the 27C section below. |
| `0x08` | `PROTO_EPROM_32PIN` | `eprom.cpp` | 13V, drop-resistor route | 32-pin UV-EPROM. See the 27C section below. |
| `0x0B` | `PROTO_EPROM_24PIN` | `eprom.cpp` | 12–25V, direct VPE route | 24-pin UV-EPROM. See the 27C section below. |
| `0x0D` | `PROTO_EEPROM_PARALLEL` | `eeprom_28c.cpp` | None (5V) | SDP disable, then DQ7 page poll. See the 0x0D section below. |
| `0x0E`, `0x27`, `0x28`, `0x29` | `PROTO_SRAM_32PIN`, `_24PIN`, `_28PIN`, `_32PIN_NVRAM` | `sram.cpp` | None (5V) | Generic read and write. This path never enables the VPP regulator. |
| `0x06` | `PROTO_FLASH_NOR_UNLOCK` | `flash_nor_unlock.cpp` | None (5V) | AMD unlock, sector erase. |
| `0x05` | `PROTO_FLASH_5V_PAGE` | `flash_5v_page.cpp` | None (5V) | Page write, then DQ7 poll. |
| `0x35` | `PROTO_PHANTOM_0x35` | `flash_5v_page.cpp` | None (5V) | Phantom. 0 database chips. The upstream label names an ITE EC MCU, not a memory algorithm. |
| `0x39` | `PROTO_PHANTOM_0x39` | `flash_5v_page.cpp` | None (5V) | Phantom. 0 database chips. No upstream algorithm constant exists. |
| `0x10` | `PROTO_FLASH_INTEL` | `flash_intel.cpp` | 12V via `CTRL_VPP_P1_ENABLE` | Command register, status-register polling. |
| `0x34` | `PROTO_EEPROM_8051BUS` | `not_implemented.cpp` | None (5V) | PCB-blocked. No dedicated arm. It falls through the generic fail-closed guard. |

### The three 27C rows (`0x07`, `0x08`, `0x0B`)

All three share one write path in `eprom.cpp`. The shared behaviour is stated once here. The
per-row table after it carries only the differences.

**Per-byte pulse-to-verify loop.** The path emits a fixed-width pulse, verifies the byte, and
repeats until the byte converges or a budget runs out.

**Pulse width.** The width comes from the database `pulse-delay` field. A row's stated fallback
applies only when `pulse_delay == 0`.

**Route selection.** One shared function, `eprom_hv_route_mask()`, resolves the high-voltage route.
It is driven by the row's `vpp_path` value. Both `eprom_check_vpp()` and the write path call it, so
the measured voltage is the applied voltage. Neither duplicates a `protocol ==` predicate.
`--vpe-as-vpp` overrides the resolved route toward the direct-VPE path.

**High-voltage teardown.** Every **error** exit from the write path disables every control-register
high-voltage route, through a single-exit wrapper. A **successful** block deliberately leaves the
route energised, so the once-per-block settle is not paid twice. `command_done()` is the
operation-level disable. Its guarantee is asserted as a source contract, not behaviourally. See
`tests/golden/eprom_params_citations.json`.

**No overprogram.** No 27C row applies an overprogram pulse. DQ7 polling is a flash-family
mechanism and no 27C row uses it.

**Intra-block progress.** The loop emits `MSG_DATA_PROGRESS` (`0xE0`) from inside the per-byte
loop. The emission is time-gated at `EPROM_PROGRESS_EMIT_INTERVAL_MS` (1000 ms). It is not
byte-counted. The payload is the absolute chip address plus `handle->mem_size`, the same contract
`mem_util_blank_check` uses. Two boundaries apply:

- **EPROM path only.** Flash, EEPROM (`0x0D`), SRAM and every other family keep block-granularity
  progress.
- **`leonardo` and native only.** On `SERIAL_ON_IO` targets (`uno`, `uno328pb`) the emission and its
  `last_emit_ms` state compile out. This is structural, not a choice.
  `rurp_set_programmer_mode()` tears the UART down for the whole programmer-mode window. The Uno's
  `rurp_log_id` override then defers frames into a 4-slot buffer. An overflow of that buffer
  silently drops the next frame. That would starve a following `MSG_ERR_MAX_PULSES` frame of its
  slot and turn a program failure into a host transport timeout.

  This is established as a source contract only, in
  `tests/test_progress_emission_is_leonardo_only.py`. Nothing attests it behaviourally.
  `src/boards/uno_rurp_shield.cpp` compiles in no native environment, and the native capture stub
  carries no `com_mode` gate.

Per-row differences:

| | `0x07` | `0x08` | `0x0B` |
|---|---|---|---|
| Pins | 28 | 32 | 24 |
| `vpp_path` | drop resistor | `VPP_PATH_DROP_RESISTOR` | `VPP_PATH_DIRECT_VPE` |
| Modal pulse width | 100µs (113 of 170 chips) | 100µs (104 of 127 chips) | 500µs (21 of 32 chips) |
| Fallback width | 1000µs | 100µs | 500µs |
| `verify_mode` | `VERIFY_PER_PULSE_PLUS_FINAL` | `VERIFY_PER_PULSE_PLUS_FINAL` | `VERIFY_PER_PULSE` |
| `max_pulses` | 25 | 25 | 255 |
| `energy_cap_us` | 0 (uncapped) | 0 (uncapped) | 50000 (50 ms) |
| `--vpe-as-vpp` | overrides toward direct VPE | overrides toward direct VPE | no-op, already direct |

`VERIFY_PER_PULSE_PLUS_FINAL` runs one additional full-array verify pass after the loop converges.
A mismatch in that pass emits `MSG_ERR_VERIFY` (`0xAF`), carrying the same 5-byte payload
`memory_verify_execute` uses. `VERIFY_PER_PULSE` verifies per pulse and runs no final pass.

**Which error IDs are reachable, per row.** `MSG_ERR_MAX_PULSES` (`0xBD`) is reachable on all three.
`MSG_ERR_ENERGY_CAP` (`0xBE`) and the pre-flight `MSG_ERR_PULSE_TOO_WIDE` (`0xAE`) refusal need
`energy_cap_us > 0`, so they are reachable on `0x0B` only. On `0x07` and `0x08` they are
structurally unreachable.

**The `0x0B` energy cap, exactly.** The 50000µs cap divides evenly by every shipped width. 200µs,
500µs and 1000µs give exactly 250, 100 and 50 pulses, with the accumulated total landing on exactly
50000. "Capped at 50 ms" is therefore exact for shipped data. An arbitrary `--pulse-us` value
bounds the accumulated-at-failure total at less than `energy_cap_us + w`. Evaluating that bound at
`w = energy_cap_us` naively suggests 99999µs. That is wrong: at `w == energy_cap_us` only one pulse
can occur, because a second pulse needs `(i-1)*w < energy_cap_us`. The real worst case is two pulses
at `w = 49999`, which gives 99998µs.

**The `0x08` drop bit.** The drop bit is a VPP *level* selector. It is not a routing control. On
Rev 2-class hardware (`REVISION_2_0`, `_2_1`, `_2_2`, `_2_3`) it survives every `set_address()` of
the block. On Rev 0 and Rev 1 it is still stripped after the first `set_address()`, deliberately,
because the two logical bits map onto one physical line there.
`mem_util_calculate_top_address_register`'s preserve mask in `memory.cpp` is gated on hardware
revision alone, inside `#ifdef HARDWARE_REVISION`. `eprom.cpp` carries no `handle->pins >= 32` clear.

Routing VPP to socket pin 1 on a 32-pin part is a separate, **physical** decision, made with a
jumper. This project documents that jumper two contradictory ways, so this file names no designator
and asserts no net. **Boundary:** the `0x08` routing is attested only in the emitted
control-register stream, never on a part. It is not a claim that `0x08` VPP is correct on silicon,
and it changes no `support_status`.

**Program-VCC ceiling. Accepted debt.** All four vendor algorithms assume a raised program-VCC for
threshold margin. That ceiling is about 6.25 V. This shield has no VCC-raise path, so the ceiling is
unreachable. The per-byte loop buys timing fidelity, pulse-count fidelity and verify fidelity on all
three 27C rows. It does **not** buy silicon-margin fidelity. The limit is hardware-bound. It is
recorded here, not attempted. The `verify_mode` header comment at `include/eprom_params.h:31-33`
names the ceiling.

### Protocol `0x0D` notes (AT28C and 28C-family EEPROM)

`configure_eeprom28c()` in `eeprom_28c.cpp` exposes a **standalone chip erase**. A `CMD_ERASE` arm
dispatches to `eeprom28c_erase_execute`. That function emits the **software** six-byte chip-erase
sequence from Atmel application note "Software Chip Erase" (Rev. 0544B-10/98):

```
5555<-AA, 2AAA<-55, 5555<-80, 5555<-AA, 2AAA<-55, 5555<-10
```

It uses the same timed emitter the SDP sequences use. An SDP-disable prefix precedes it. An
unconditional `delay(AT28C_TEC_MAX_MS)` follows it. That is the 20 ms `tEC`, timed internally, with
no poll. The arm costs **0 B RAM**, because the six writes are inline rather than a `.data` table.
No sector erase exists.

**The datasheet's *hardware* Chip Erase mode is deliberately NOT implemented.** It needs **12 V on
OE, pin 22** of `DIP28_28C256`, which damages a 5 V part. `scripts/check_erase_no_vpp.py` enforces
its absence. That script runs a brace-matched negative scan of `eeprom28c_erase_execute`'s body and
asserts zero control-register high-voltage writes. Do not re-derive that 12 V path from the
datasheet and splice it into this handler. Algorithm 5 keeps its `FLAG_CAN_ERASE` exclusion
permanently, for the same reason: no firmware routine implements that 12 V path, so setting the flag
would claim a capability that does not exist.

`write` performs **no blank check at all** on this protocol. Each page write auto-erases internally,
so the pre-write check was a false precondition rather than a safety net. `blank` remains available
as its own step.

**All of this ships software-proven and unvalidated on silicon.** `0x0D` stays `UNVERIFIED`. None of
it claims the write path works on a part. The auto SDP-disable sequence reports its own emission and
measured duration. The SDP protection state itself is not readable, so a successful emission proves
only that the sequence was sent. It proves nothing about the part's protection state before or
after. The `Programming Protocols` wiki page §1.6 carries the full model.

### JSON Wire Protocol

The firmware receives JSON commands over serial at 250000 baud. It parses the `algorithm` field, an
integer, into `handle->protocol`. That is the primary dispatch key.

Key fields:

- `algorithm` — integer protocol ID, stored in `handle->protocol`.
- `vpp_mv` — VPP voltage in millivolts, used by the ADC validation step.
- `memory-size` — chip size in bytes.
- `pulse-delay` — write pulse width in µs. `0` means use the handler default.
- `chip-id` — expected manufacturer and device ID. `0` skips the ID check.

The firmware no longer parses the legacy `type` key. `json_parser.c` skips unknown JSON fields
silently, so a stray `type` from an older host is ignored safely. See `## Breaking Changes (v1.20)`
in `README.md`.

### Operation-Setup Ack (`MSG_OK_READY`)

`init_programmer_framed` in `src/firestarter.cpp` sends this ack once it has parsed a command. It is
one length-discriminated byte blob. Three separate additions extended it in place rather than
emitting a new message per capability:

```
[buffer_size u16 BE][hw_revision u8][ver_len u8][ver bytes][write_budget_s u16 BE]
```

- **`buffer_size`** — the data-buffer size, `DATA_BUFFER_SIZE`, 2 bytes. It has been present since
  the ack existed.
- **`hw_revision` and `ver`** — the hardware-revision byte, then a variable-length firmware-version
  string. **Read `ver` at a computed offset, never a fixed index.** The string length varies by
  board name. A firmware build that omits this field cannot connect to the host at all:
  `_probe_port` raises `FirmwareOutdatedError` when no firmware identity is reported, and
  `tests/test_fwguard.py`'s `test_absent_identity_refuses` asserts that refusal on purpose.
- **`write_budget_s`** — the per-block worst-case write-time budget, a `uint16_t` of **seconds**.
  The firmware already pads it, so the host applies no multiplier of its own. `eprom_block_budget_s()`
  computes it. See `include/eprom_budget.h` for the padding rule and the pulse-count arithmetic.
  Write it at the offset immediately after the variable-length `ver` tail, computed from `ver_len`,
  never a literal. The firmware emits it for **every** command, not only `CMD_WRITE`, because the
  ack's shape must not vary by command. A non-EPROM protocol makes `eprom_block_budget_s()` return
  `0`, as does any command for which `configure_memory` never ran. The host reads `0` as "not
  advertised" and leaves the attribute `None`, because of its own `[1, 14400]` plausibility clamp.
  It never reads `0` as "no time needed".

`MSG_OK_READY`'s catalog entry is a variable-length byte blob, `param_bytes = -1`. All three
extensions therefore needed zero catalog edits and zero codegen runs. `include/messages.h` is
untouched by any of them.

**`--pulse-us` interaction.** The host can override the per-run pulse width with
`firestarter write --pulse-us N`. Its Click parser bounds the value at `1..65535`. That bound is
minipro parity, because minipro's `-o pulse=N` is a `uint16`. **It is not a wire-type limit.**
`extract_long` in `json_parser.c` parses `pulse-delay` into an **unclamped** `uint32_t`, so a value
above 65535 is reachable on the wire independently of the host flag. That gap is recorded, not
clamped. The firmware-side backstop that does enforce a ceiling is `configure_eprom`'s pre-flight
refusal, `MSG_ERR_PULSE_TOO_WIDE` (`0xAE`), keyed on `energy_cap_us`. It fires before any high
voltage is enabled. Only the `0x0B` row makes it reachable, which is why the host mirrors no table
value to pre-empt it.

### Key Files

- `src/json_parser.c` — parses a JSON command into `firestarter_handle_t`. It skips unknown fields
  silently.
- `src/proms/memory.cpp` — top-level dispatch, `configure_memory()`.
- `include/firestarter.h` — the `firestarter_handle_t` struct definition.
- `include/rurp_pinout.h` — control-register bit definitions.
- `include/messages.h` — **generated. Do not edit it by hand.** It carries message IDs only. The
  source of truth is `messages.toml` in the meta repository. Codegen runs there and nowhere else.
  This repository consumes the synced artifact. To change a message, edit `messages.toml`, run the
  codegen in the meta repository, and sync the result here.

### Constants

Control register bits, from `rurp_pinout.h`:

- `CTRL_VPP_REGULATOR_ENABLE (0x80)` — enable the VPP boost regulator.
- `CTRL_VPP_VPE_DROP_ENABLE (0x01 legacy / 0x100 rev2)` — drop VPE through a resistor to VPP level.
- `CTRL_VPP_P1_ENABLE (0x08)` — route VPP to socket pin 1.
- `CTRL_VPP_A9_ENABLE (0x02)` — route VPP to A9, for the EPROM chip-ID read.
- `CTRL_VPE_ENABLE (0x04)` — apply VPE directly to the PGM pin.

Firmware flags, from `firestarter.h`:

- `FLAG_FORCE (0x01)` — treat an ID mismatch as a warning, not an error.
- `FLAG_CAN_ERASE (0x02)` — the chip supports erase before write.
- `FLAG_SKIP_ERASE (0x04)` — skip the auto-erase in write init.
- `FLAG_SKIP_BLANK_CHECK (0x08)` — skip the blank check.
- `FLAG_VPE_AS_VPP (0x10)` — legacy. Use the direct VPE path.

### Hardware Revision Documentation

The `Shield Revisions` wiki page is the operator-facing canonical RURP shield revision reference. It
is a subset clone of a meta-repository investigation document. It carries four sections: the
inventory, the per-revision capability matrix, the silkscreen-to-code alias table, and the
per-revision ADC band table. **If any of those four sections changes in the meta repository, update
the wiki page in the same change.** Nothing enforces this mechanically.

The `ADC_BAND_R41_*` defines in `rurp_pinout.h` are the firmware-side source of truth for the
band-lookup math. The ADC Band Table in the `Shield Revisions` wiki page mirrors those values
verbatim. Drift between the two is a bug. If the values change in `rurp_pinout.h`, update the wiki
page and the meta record in the same commit pair.

`pinMode(PIN_HW_REVISION_DETECT_ADC)` uses `INPUT`, high-Z, not `INPUT_PULLUP`. The MCU internal
pull-up is therefore off and the R41 detect divider's R_top is not active. The existing ADC band
thresholds (200, 220, 600) characterize **A3-net composition**, not R41 value:

- R41 only, to ground → low band.
- External pull-up active → mid band.
- Floating → high band.

A future Rev 2.4 PCB could add an external R_top to restore the original schematic-divider
semantics.

### PY32F071 Flash-Path and PCB Documentation

`platform/py32f071/FLASH-PATH-AND-PCB.md` is a subset clone of a meta-repository decision record. It
carries five shared sections, each named by a marker so a reader can find the contract from either
copy:

- `[SHARED:S1]` — the three-tier flash path.
- `[SHARED:S2]` — the PCB checklist.
- `[SHARED:S3]` — the flash budget.
- `[SHARED:S4]` — the USB vendor and product identity.
- `[SHARED:S5]` — the socket-empty instruction.

**If any of those sections changes in the meta repository, update the sub-repo copy in the same
change.** `tests/test_flash_path_record_sync.py` enforces this mechanically, so a divergence is a
test failure rather than a latent inconsistency. **Stated honestly:** `pytest tests/ -v` does run
that module in CI, but its cross-repo legs skip there. CI checks out this repository alone, so the
meta copy is absent and the `requires_meta` marker skips at collection time. Comparing the two
records is therefore a local-run obligation for anyone editing either copy. Never imply that CI
compared them.

`FIRESTARTER_META_ROOT` overrides the resolved meta-repository **root only**, never the marker name.
It sits alongside `FIRESTARTER_FW_ROOT` and `FIRESTARTER_SIZE_BASELINE`. It binds at import, so set
it in a child process rather than monkeypatching it. This repository has no central
environment-variable inventory, so this sentence is the only place a reader outside
`tests/meta_presence.py` can discover it.

## Native (Host) Test Environment

Unity tests exercise the dispatch logic in `configure_memory` on the host, through PlatformIO's
`platform = native`. No AVR board is needed.

### Invocation

```bash
pio test -e native                          # run every native suite
pio test -e native -f "*test_dispatch*"     # run only configure_memory dispatch tests
```

### Layout

```
firestarter_fw/
├── platformio.ini                          # [env:native]: platform=native, test_framework=unity,
│                                           # src_filter = +<proms/>, test_build_src = yes,
│                                           # -D RURP_BOARD_NAME=\"native\"
└── test/
    └── native/
        └── avr/
            └── test_dispatch/
                ├── test_configure_memory.cpp   # one Unity RUN_TEST case per KNOWN_PROTOCOLS entry
                ├── host_stubs.cpp              # no-op rurp_* symbols + LOG_*_MSG PROGMEM strings
                └── avr/
                    └── pgmspace.h              # host shim for AVR PROGMEM macros, incl. PGM_P
```

### Why a host stub translation unit?

`[env:native]` cross-compiles `src/proms/*.cpp`, the dispatch and handler translation units, against
host libc and ArduinoFake. `src_filter = +<proms/>` excludes the AVR-only units: `src/boards/*.cpp`,
`src/dev_tools.cpp`, `src/eprom_operations.cpp` and `src/logging.c`.

The handlers still reference `rurp_*` hardware symbols and the eight `LOG_*_MSG` PROGMEM strings.
`host_stubs.cpp` supplies minimal no-op implementations, which satisfy the linker without touching
real hardware. The dispatch tests assert on `handle->firestarter_operation_main` and
`handle->response_code` only. They never assert on register side effects, so the no-op stubs are
functionally complete for this test class.

The host shim at `test/native/avr/test_dispatch/avr/pgmspace.h` defines `PROGMEM`, `PSTR`, `PGM_P`
and `pgm_read_*` as host-memory equivalents, so headers that include `<avr/pgmspace.h>` compile on a
non-Harvard host.

### Adding a native test suite

Drop `test_*.cpp` files under `test/native/avr/<dirname>/`. Extend `host_stubs.cpp` only if the new
test references additional `rurp_*` symbols.

**`[env:native]` needs changes too. It does not pick a new suite up on its own.** The environment
uses a **positive** `test_filter` allowlist in `platformio.ini`. A suite directory is invisible to
`pio test` until its path appears in `test_filter`. Its headers are unreachable until a matching
`-I test/native/avr/<dirname>` entry appears in `build_flags`. Update both lists.

A second native environment, `[env:native_nodevtools]`, also exists. A new suite must appear in
**both** environments' `test_filter` and `-I` lists, which is four new lines, so that it runs both
with and without `-D DEV_TOOLS`.

**Exception: `native_params_v131`, `native_loop_v131` and `native_trace_v131`.** The instruction
directly above does not apply to these three. Each names only its own suite in its own
`test_filter`. None is folded into a pinned environment's `test_filter`. None is in `default_envs`.
**None runs in any CI leg of either repository.**

`native_loop_v131` exists because the frozen `native_trace_v131` fixture goes red by design once the
per-byte program loop is rewritten, so it cannot verify that rewrite. `native_loop_v131` is its own
oracle and carries the same four constraints.

`[env:native_loop_v131]` runs two suites: `test_loop_eprom_v131` at 47 cases and
`test_vpp_eprom_v131` at 32 cases, for **79 cases total**. Because this environment runs in no CI
leg, both counts are a local run-by-name obligation. Never imply a CI measurement.
