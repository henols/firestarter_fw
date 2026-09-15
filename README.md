<p align="left"><img src="https://raw.githubusercontent.com/henols/firestarter_app/refs/heads/main/images/firestarter_logo.png" alt="Firestarter EPROM Programmer" width="200"></p>

# Firestarter Firmware

The AVR firmware that runs on the Arduino and drives the chip in the socket.

**New here?** Start at [firestarter](https://github.com/henols/firestarter) — what
Firestarter is, how to install it, and how to read your first chip. You do not need to build
this firmware by hand; the `firestarter` CLI installs the matching build for you.

This README covers building and flashing the firmware itself.

## Supported boards

Three build targets are emitted per release:

| Board | PlatformIO env | MCU | Bootloader | Notes |
|---|---|---|---|---|
| `uno` | `[env:uno]` | ATmega328P | optiboot (stk500v1 `arduino`) | Arduino Uno R3 + RURP shield |
| `uno328pb` | `[env:uno328pb]` | ATmega328PB | Urclock (MiniCore default) | Uno R3 carrier re-MCU'd with ATmega328PB; pin-compatible with `uno` |
| `leonardo` | `[env:leonardo]` | ATmega32U4 | Caterina (avr109) | Arduino Leonardo + RURP shield; 1024-byte data buffer |

The firmware reports its board name in the handshake response, and `firestarter fw -i` uses that
string to resolve the matching `.hex` from the GitHub release.

## Building

```bash
pio run -e uno                 # build
pio test -e native             # unit tests
pio run -t upload -e uno       # flash a connected board
pio run -t monitor -e uno      # serial monitor, 250000 baud
```

Swap `uno` for `leonardo` or `uno328pb`.

## Installing a build

Normally, through the CLI:

```bash
firestarter fw -i              # stable
firestarter fw -i --pre        # pre-release
```

Or download `firestarter_{board}.hex` from
[Releases](https://github.com/henols/firestarter_fw/releases) and flash it with `avrdude`.

Pre-release builds are tagged `X.Y.ZbN` or `X.Y.ZrcN` and marked "Pre-release", never "Latest",
so a stable-installed CLI never pulls beta firmware by accident.

> **Beta builds carry no stability guarantee.** They may contain bugs, change without notice, or
> be withdrawn. Use a stable release for bench work that matters.

## Protocol reference

- **[PROTOCOLS.md](PROTOCOLS.md)** — per-protocol write algorithms, pulse widths, voltage routing,
  register constants and datasheet citations
- **[PINOUTS.md](PINOUTS.md)** — socket pin maps for every chip family, shield pin and
  control-register assignments, the DIP24 adapter mapping

`PROTOCOLS.md` carries a machine-read claims region checked against the host tool and this
firmware. Keep its table shape intact when editing.

### One protocol note worth stating here

Protocol `0x0D` (5 V parallel EEPROM) exposes a standalone chip erase using the **software**
six-byte sequence from Atmel's "Software Chip Erase" application note (Rev. 0544B-10/98). The
datasheet's *hardware* erase mode, which requires **12 V on OE (pin 22)**, is deliberately **not
implemented** — that is a hardware-damage hazard on a 5 V part.
`scripts/check_erase_no_vpp.py` is the gate that keeps it out.

## Reporting a problem

See the [Contributing](https://github.com/henols/firestarter/wiki/Contributing) wiki page for where to report a problem and where to open a pull request.

## Documentation

Everything else — supported chips, shield revisions, protocols, how to test a chip — is on the
**[Firestarter wiki](https://github.com/henols/firestarter/wiki)**.
Version history is in [Breaking Changes](https://github.com/henols/firestarter/wiki/Breaking-Changes).

## License

[MIT](LICENSE)
