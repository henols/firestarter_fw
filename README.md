<p align="left"><img src="https://raw.githubusercontent.com/henols/firestarter/main/images/branding/firestarter_logo_horizontal.png" alt="Firestarter EPROM Programmer" width="400"></p>

# Firestarter Firmware

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Buy me a coffee](https://img.shields.io/badge/Ko--fi-Buy%20me%20a%20coffee-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/E1E21I2WWW)

This is the firmware for the Firestarter EPROM programmer. It runs on an Arduino with the RURP
shield and drives the chip in the socket. The `firestarter` command-line tool sends it one
command at a time over USB.

For an introduction to Firestarter, installation and your first chip read, start at the
[Firestarter hub](https://github.com/henols/firestarter#readme). You do not have to build this
firmware yourself. The CLI installs the correct build for your board.

## Supported boards

| Board | PlatformIO env | MCU | Notes |
|---|---|---|---|
| `uno` | `uno` | ATmega328P | Arduino Uno R3 with the RURP shield |
| `uno328pb` | `uno328pb` | ATmega328PB | Uno-shaped board with an ATmega328PB, pin-compatible with `uno` |
| `leonardo` | `leonardo` | ATmega32U4 | Arduino Leonardo with the RURP shield, 1024-byte data buffer |
| `py32f071` | CMake, not PlatformIO | PY32F071 | Beta only. No PCB exists. The firmware refuses every command that drives the chip socket on this target. See [platform/py32f071/README.md](platform/py32f071/README.md). |

The firmware reports its board name in the handshake. The CLI uses that name to find the
matching `firestarter_<board>.hex` file in a release.

## Build

The default `pio run` builds `uno`, `uno328pb` and `leonardo`.

```bash
pio run -e uno                 # build one board
pio test -e native             # run the unit tests on the host
pio run -t upload -e uno       # flash a connected board
pio run -t monitor -e uno      # serial monitor, 250000 baud
```

Replace `uno` with `uno328pb` or `leonardo` for the other boards.

## Install

Use the CLI:

```bash
firestarter fw -i -b <board>          # latest stable release
firestarter fw -i --pre -b <board>    # latest pre-release
```

You can also download `firestarter_<board>.hex` from
[Releases](https://github.com/henols/firestarter_fw/releases) and flash it with `avrdude`.

A pre-release has a version such as `X.Y.ZbN` or `X.Y.ZrcN`. Its GitHub label is "Pre-release",
never "Latest", so a CLI on the stable channel does not install it.

> [!WARNING]
> A beta build has no stability guarantee. It can contain bugs, and it can change or disappear
> without notice. Use a stable release for work that matters.

## Developer documents

- [PROTOCOLS.md](PROTOCOLS.md): how the firmware writes, erases and identifies each chip family.
- [PINOUTS.md](PINOUTS.md): how the Arduino pins connect to the shield, and the control-register
  bits.

User documentation, the supported chips and the socket pin maps are on the
[Firestarter wiki](https://github.com/henols/firestarter/wiki).

## Contributing

Report problems in the [central issue tracker](https://github.com/henols/firestarter/issues). The
[Contributing](https://github.com/henols/firestarter/wiki/Contributing) wiki page tells you where
to open a pull request.

## Changes

The only change log is [CHANGELOG.md](https://github.com/henols/firestarter/blob/main/CHANGELOG.md)
in the meta repository.

## License

[MIT](LICENSE)
