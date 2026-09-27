<p align="left"><img src="https://raw.githubusercontent.com/henols/firestarter/main/images/branding/firestarter_logo_horizontal.png" alt="Firestarter EPROM Programmer" width="400"></p>

# Firestarter on PY32F071

This target builds the shared Firestarter PROM handlers and the command protocol for the Puya
PY32F071 microcontroller. It replaces the Arduino with a native hardware backend. The design
record for the flash map, the configuration storage, the flash path and the PCB is in
[DESIGN.md](DESIGN.md).

## Status

- The target is beta only. A stable CLI does not offer the `py32f071` board.
- CI builds the image. `py32f071.yml` builds it on each branch push except `main`, and on pull
  requests that change ARM paths. `beta-build.yml` attaches `firestarter_py32f071.hex` to each
  pre-release. An ARM build failure in `beta-build.yml` cannot stop the AVR assets.
- No PCB exists. Nobody has run this image on a PY32F071 chip.
- The pin map is provisional. The firmware therefore refuses every command that drives the chip
  socket: read, write, erase, chip ID, the SDP commands and lock status. It sends
  `MSG_ERR_NOT_SUPPORTED` for each of them. The version, configuration and voltage-read commands
  stay available.

## What the port contains

- A CMake and Ninja build with the GNU Arm toolchain, and a pinned Puya PY32F071 SDK.
- The PY32F071 CMSIS startup code and vector table.
- A 48 MHz system clock from the internal HSI oscillator and the PLL. USB needs this clock.
- USB CDC serial through CherryUSB. The target does not define `SERIAL_ON_IO`.
- Millisecond timing from SysTick, and microsecond delays from TIM3.
- Inactive (high) `/CE` and `/OE` levels at startup.
- An eight-bit data bus on one GPIO port. A read takes one `IDR` snapshot. A write is one `BSRR`
  store.
- A 12-bit ADC voltage reading, corrected with the internal VREFINT reference.
- Configuration storage in two flash slots. [DESIGN.md](DESIGN.md) describes it.

## Pin map (provisional)

| Signal | PY32F071 pin |
|---|---|
| PROM D0 to D7 | PB0 to PB7 |
| Low address byte latch | PA0 |
| High address byte latch | PA1 |
| `/OE` | PA2 |
| Control-register latch | PA3 |
| VPP measurement | PA4, ADC channel 4 |
| `/CE` | PA5 |
| User button | Not fitted |

PA4 with ADC channel 4 follows the Puya ADC example. The other pins are placeholders that give a
simple bus. Do not use them as PCB wiring.

The only place that sets the pins is `include/boards/py32f071_rurp_shield.h`. That header defines
`RURP_PY32F071_PINMAP_PROVISIONAL 1`. The flag makes `configure_memory()` refuse the socket
commands. When a final schematic exists, change the pin definitions and clear the flag.

## Build

Run from the repository root:

```sh
cmake -S platform/py32f071 -B build/py32f071 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/py32f071
```

The build writes an ELF, BIN, HEX, linker map and size report to `build/py32f071/`. Only
`firestarter_py32f071.hex` goes into a release. The host installer finds a release asset by the
name `firestarter_<board>.hex`. `<board>` is the board name that the firmware reports, `py32f071`.

## Install over USB DFU

> [!WARNING]
> **Before any PY32F071 firmware install — DFU, SWD or otherwise — the PROM socket must be empty.**
> The pin map is provisional, and during a DFU install the factory bootloader controls the pins,
> not Firestarter. [DESIGN.md](DESIGN.md) gives the full reason.

1. Install a pre-release of the CLI with the `py32` extra: `pip install --pre "firestarter[py32]"`.
   The extra adds `pyusb`. You also need a libusb backend. On Windows, you need a WinUSB driver
   for the DFU device.
2. Remove the chip from the socket.
3. Set BOOT0 high and power-cycle the board. The factory bootloader starts only when the option
   bit nBOOT1 is 1.
4. Run `firestarter fw -i --pre -b py32f071`.

The CLI has its own DFU client, so you do not need `dfu-util` or a vendor tool. It refuses an image
that reaches past the application region.

## Flash path

The design has three ways to write the flash. [DESIGN.md](DESIGN.md) has the detail.

1. A self-flash bootloader over the USB CDC link. This is the intended main path. It does not
   exist yet.
2. The factory USB DFU bootloader. This is the recovery path, and the only path that the CLI
   supports now.
3. SWD, as the last resort.

## Before you connect a chip

Before you connect a PROM or apply a programming voltage, measure these items with test equipment:

- The startup pin levels.
- All data values from `0x00` to `0xFF` on the data bus.
- Each change of bus direction and each control signal.
- The USB framing and the voltage readings.
- All PROM timing.
