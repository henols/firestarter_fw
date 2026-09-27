<p align="left"><img src="https://raw.githubusercontent.com/henols/firestarter/main/images/branding/firestarter_logo_horizontal.png" alt="Firestarter EPROM Programmer" width="400"></p>

# Development

This document tells you how to build and test the Firestarter firmware. You do not need it to use
Firestarter: the CLI installs the firmware. Refer to the
[Firestarter wiki](https://github.com/henols/firestarter/wiki).

## Boards

| Board | PlatformIO env | MCU | Notes |
|---|---|---|---|
| `uno` | `uno` | ATmega328P | Arduino Uno R3 with the RURP shield |
| `uno328pb` | `uno328pb` | ATmega328PB | Uno-shaped board with an ATmega328PB, pin-compatible with `uno` |
| `leonardo` | `leonardo` | ATmega32U4 | Arduino Leonardo with the RURP shield, 1024-byte data buffer |
| `py32f071` | CMake | PY32F071 | Beta only. Refer to [platform/py32f071/README.md](platform/py32f071/README.md) |

The firmware sends its board name when the CLI connects. The CLI uses that name to find the file
`firestarter_<board>.hex` in a release.

## Build, test and flash

The PlatformIO default builds `uno`, `uno328pb` and `leonardo`.

```bash
pio run                        # build all three AVR boards
pio run -e uno                 # build one board
pio test -e native             # unit tests on the host
pio test -e native_nodevtools  # unit tests without the dev commands
pio run -t upload -e uno       # flash a connected board
pio run -t monitor -e uno      # serial monitor, 250000 baud
```

Replace `uno` with `uno328pb` or `leonardo` for the other boards. The Python checks are in
`tests/`:

```bash
python3 -m pytest tests/
```

Some of these tests need a clean working tree. Commit your change before you run them.

## Releases

A push to `beta` publishes a pre-release with one `.hex` file for each board. A push to `main`
publishes a stable release when the version in `include/version.h` has no tag yet. The
[release runbook](https://github.com/henols/firestarter/blob/main/RELEASING.md) has the details.

## Pull requests

Send pull requests to this repository. Report problems in the
[project issue tracker](https://github.com/henols/firestarter/issues). The wiki
[Contributing](https://github.com/henols/firestarter/wiki/Contributing) page has the rules.
