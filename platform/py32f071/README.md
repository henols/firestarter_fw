# Firestarter on PY32F071

This target builds the existing Firestarter PROM algorithms and command protocol with a native PY32F071 hardware backend.

## Implemented

- native CMake/Ninja GNU Arm build
- pinned official Puya PY32F071 SDK
- PY32F071 CMSIS startup and interrupt vector
- C and C++ runtime initialization
- 48 MHz clock configuration required by native USB
- CherryUSB CDC transport without `SERIAL_ON_IO`
- SysTick millisecond timing
- TIM3 microsecond delays
- safe active-low `/CE` and `/OE` startup levels
- logical-control-to-physical-GPIO translation
- contiguous eight-bit GPIO data bus
- one-snapshot data reads through `IDR`
- atomic data writes through `BSRR`
- 12-bit ADC voltage measurement using VREFINT compensation
- optional active-low user button support
- unchanged shared PROM algorithms and packet framing

## Provisional example pin map

The implementation guide marks its GPIO values as examples, and no final PY32F071 Firestarter schematic or pin assignment is present in the repository or supplied documents. To allow the target to compile and evolve before the PCB mapping is finalized, the board header now contains this explicitly provisional example:

| Signal | Example PY32F071 pin |
|---|---|
| PROM D0-D7 | PB0-PB7 |
| LSB address-latch strobe | PA0 |
| MSB address-latch strobe | PA1 |
| `/OE` | PA2 |
| Control-register latch strobe | PA3 |
| VPP measurement | PA4 / ADC channel 4 |
| `/CE` | PA5 |
| User button | Not fitted |

PA4 / ADC channel 4 follows the official Puya PY32F071 ADC example. All other assignments are placeholders selected for a simple contiguous bus and must not be treated as verified PCB wiring.

The single physical mapping point is:

```text
include/boards/py32f071_rurp_shield.h
```

The header defines:

```cpp
#define RURP_PY32F071_PINMAP_PROVISIONAL 1
```

Replace the mapping and remove or clear that marker when the final schematic is available.

## Build

```sh
cmake -S platform/py32f071 -B build/py32f071 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/py32f071
```

Generated outputs are ELF, BIN, HEX, linker map and size report, all under
`build/py32f071/`. Only `firestarter_py32f071.hex` is ever published — the rest
are dev-environment artefacts, reproducible with the two commands above, and CI
does not upload them.

The underscore in `firestarter_py32f071.*` is load-bearing. The host installer
resolves release assets by the name `firestarter_<board>.hex`, where `<board>` is
the `RURP_BOARD_NAME` the firmware reports over the wire (`py32f071`), so the
CMake `TARGET_NAME` matches the convention `name_firmware.py` applies to the AVR
targets.

## Release integration

`py32f071.yml` builds and validates this target on pull requests and on every
`beta` push, but it does **not** cut releases, for one hard reason:
`beta-build.yml` runs `.github/scripts/update_version.py`, which rewrites
`include/version.h` and auto-commits it, *before* building. An image built in
any other job compiles a stale `VERSION` string, and the host's whole update
decision is a comparison of that string against the release tag. So the
PY32F071 image has to be built in the same job as the AVR images, after the
version bump — and it is reached through a **composite action** at
`.github/actions/build-py32f071/`, called by both workflows, precisely because
a composite action's steps run *in* the calling job. A reusable `workflow_call`
workflow would instead run as a separate job with its own checkout, and would
break that same-job ordering outright.

The `Release` step's `files:` block, as shipped, has two entries:

```yaml
          files: |
            .pio/build/**/firestarter_*.hex
            build/py32f071/firestarter_*.hex
```

Two entries because PlatformIO writes its AVR `.hex` outputs under
`.pio/build/`, while this CMake build writes its image under `build/py32f071/`
— one glob cannot cover both trees. The second entry is a glob, not a literal
filename.

Why an absent py32 image does not fail the release: `softprops/action-gh-release`
globs every `files:` entry — literal or glob alike — via `glob.sync()`, and
decides severity purely from its `fail_on_unmatched_files` input, whose
documented default is `false`. This step deliberately never sets that input, so
an unmatched glob only warns; it never fails the run. The glob form above is
still preferred over a literal path, but that preference is about
rename-resilience and reading consistently with the AVR entry — a style
argument, not a failure-mode argument.

The ARM build itself has two roles, by design. `py32f071.yml` is the **LOUD**
gate: it carries no `continue-on-error` and stays red when the ARM build
breaks. `beta-build.yml`'s call site to the same composite action carries
`continue-on-error: true` and exists only to produce the release asset —
it can never block the three AVR assets. That containment is defensible only
because `py32f071.yml` exists alongside it and does not hide the same failure.
**Removal trigger:** the `continue-on-error` flag comes off once this target
has been validated on real silicon. No PY32F071 PCB exists, so that trigger is
unreachable this milestone and the flag stays, deliberately.

Immediately before the `Release` step, `scripts/check_release_assets.py` runs
unconditionally and fails the build if any AVR `.hex` named by
`scripts/baseline/size_baseline.json`'s `avr_targets` keys is missing or empty.
It never requires the py32f071 image.

`beta-build.yml` also carries a permanent `rehearsal` boolean dispatch input.
When set, it publishes a **draft** release under a `rehearsal-<run_id>` tag
instead of a real pre-release, so a rehearsal run leaves no public footprint. A
rehearsal dispatch must always supply `beta_version` explicitly — leaving it
blank makes `update_version.py` take the stable-release path instead of the
beta path.

`build.yml` (the stable / `main` release workflow) is deliberately untouched.
`py32f071` is in the host's `BETA_ONLY_BOARDS`, so a stable release carrying a
py32f071 asset would advertise an image the stable CLI exits 2 on. **Graduation
trigger:** fold the ARM build into `build.yml` when `py32f071` leaves
`BETA_ONLY_BOARDS`.

Everything above describes **publication**: nothing here says the published
image runs, boots or installs, because no PY32F071 PCB exists.

## Hardware validation still required

Before connecting a PROM or applying programming voltage, validate the startup levels, complete `0x00`-`0xFF` data mapping, bus direction changes, every logical control signal, USB framing, voltage readings, and all PROM timing with appropriate test equipment. The provisional pin map is for compilation and early bring-up only.

### Socket empty before any firmware install

**Before any PY32F071 firmware install — DFU, SWD or otherwise — the PROM socket must be empty.** The pin map above is provisional, so a signal may be assigned the wrong direction, and a DFU install runs the factory bootloader, during which no Firestarter GPIO initialisation executes at all. See `FLASH-PATH-AND-PCB.md` §"Socket empty before any PY32F071 firmware install", in the same directory, for the full reasoning.
