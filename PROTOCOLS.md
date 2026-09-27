<p align="left"><img src="https://raw.githubusercontent.com/henols/firestarter/main/images/branding/firestarter_logo_horizontal.png" alt="Firestarter EPROM Programmer" width="400"></p>

# Programming protocols

This is the developer reference for the protocol handlers in `src/proms/`. The user description of
each protocol is on the [Programming Protocols](https://github.com/henols/firestarter/wiki/Programming-Protocols)
wiki page.

## Purpose

The host sends the chip's `algorithm` value in each command. That value is the minipro
`protocol_id` of the chip. The firmware stores it in `handle->protocol` and dispatches on it only.
`configure_memory()` in `src/proms/memory.cpp` selects the handler. The protocol constants are in
`include/proto_constants.h`. An unknown value goes to `configure_not_implemented()`, which returns
`MSG_ERR_PROTOCOL_NOT_IMPLEMENTED` (`0xBB`) and changes no hardware state.

Since release 3.1.0 the firmware has no verify command and no blank-check command. The host reads
the chip back and compares the data. Some write paths also read back each byte or page. The
sections below say where.

## Overview

The chip counts come from `chip_database.json` in `firestarter_app`. The database has 746 chips.
The count is the number of rows with that `programming.algorithm` value.

| Protocol | Name | Handler (`src/proms/`) | Chips | VPP and VCC |
|---|---|---|---:|---|
| `0x05` | `PROTO_FLASH_5V_PAGE` | `configure_flash_5v_page()`, `flash_5v_page.cpp` | 27 | 5 V only. No control-register voltage bit. |
| `0x06` | `PROTO_FLASH_NOR_UNLOCK` | `configure_flash_nor_unlock()`, `flash_nor_unlock.cpp` | 190 | 5 V only. No control-register voltage bit. |
| `0x07` | `PROTO_EPROM_28PIN` | `configure_eprom()`, `eprom.cpp` | 170 | VPP through the drop resistor. |
| `0x08` | `PROTO_EPROM_32PIN` | `configure_eprom()`, `eprom.cpp` | 127 | VPP through the drop resistor. |
| `0x0B` | `PROTO_EPROM_24PIN` | `configure_eprom()`, `eprom.cpp` | 32 | Direct VPE rail. |
| `0x0D` | `PROTO_EEPROM_PARALLEL` | `configure_eeprom28c()`, `eeprom_28c.cpp` | 84 | 5 V write. The chip-ID read puts VPP on A9. |
| `0x0E` | `PROTO_SRAM_32PIN` | `configure_sram()`, `sram.cpp` | 20 | 5 V only. |
| `0x10` | `PROTO_FLASH_INTEL` | `configure_flash_intel()`, `flash_intel.cpp` | 39 | VPP regulator on socket pin 1. |
| `0x27` | `PROTO_SRAM_24PIN` | `configure_sram()`, `sram.cpp` | 2 | 5 V only. |
| `0x28` | `PROTO_SRAM_28PIN` | `configure_sram()`, `sram.cpp` | 34 | 5 V only. |
| `0x29` | `PROTO_SRAM_32PIN_NVRAM` | `configure_sram()`, `sram.cpp` | 20 | 5 V only. |
| `0x34` | `PROTO_EEPROM_8051BUS` | `configure_not_implemented()` | 1 | Not implemented. |

The control-register bits named below are in `include/rurp_pinout.h`. [PINOUTS.md](PINOUTS.md)
shows the bit layout.

## `0x05`: 5 V page-write flash

**Write.** The host sends the page size in the `page-size` field. The value must be a power of
two and 512 or less. Other values fail with `MSG_ERR_FL4_PAGE_SIZE` (`0xBF`). The start address and
the length must align to the page. Other values fail with `MSG_ERR_FL4_PAGE_ALIGN` (`0xC0`). At the
start of each page the firmware sends the three-byte unlock `5555<-AA, 2AAA<-55, 5555<-A0`. It then
loads the bytes of the page.

**Completion.** After the last byte of a page, the firmware reads that byte until it equals the
data. It reads at most 1024 times, with 10 µs between reads. A timeout sends
`MSG_ERR_FL4_VERIFY_TIMEOUT` (`0xB3`).

**Erase.** The handler has no erase. The chip erases each page when it writes the page. A
standalone erase command gets `MSG_ERR_NOT_SUPPORTED` (`0xA5`).

**Chip ID and lock status.** The firmware enters ID mode with `AA, 55, 90` and reads addresses
`0x0000` and `0x0001`. It leaves ID mode with `AA, 55, F0`. The lock-status command reads the
boot-block status at `0x0002` in ID mode (Winbond W29C020C datasheet, p. 9). `0xFF` means unlocked
and `0xFE` means locked.

## `0x06`: AMD and SST unlock-sequence flash

**Write.** For each byte the firmware sends `5555<-AA, 2AAA<-55, 5555<-A0` and then writes the
byte. It then polls DQ7 for at most 150 ms. It accepts DQ7 when two reads in sequence match the
data. A timeout sends `MSG_ERR_OP_TIMEOUT` (`0xB7`).

**Erase.** An erase at address 0 sends the six-byte chip erase that ends with `5555<-10`. An erase
at a different address sends the sector erase that ends with `<address><-30`. Before a write, the
firmware erases the chip if the host sets `FLAG_CAN_ERASE` and does not set `FLAG_SKIP_ERASE`. It
then waits 105 ms.

**Chip ID and lock status.** The chip ID uses the same ID mode as `0x05`. The lock-status command
reads the sector-protection byte at `0x0002` in ID mode (AMD Am29F040B datasheet, Rev. F, p. 11).
`0x00` means unprotected and `0x01` means protected. The byte shows the lowest sector only.

## `0x07`, `0x08` and `0x0B`: UV-EPROM and electrically erasable EPROM

The three protocols share one handler, `configure_eprom()`. The table
`src/proms/eprom_params.cpp` holds the values that differ between them.

| | `0x07` | `0x08` | `0x0B` |
|---|---|---|---|
| Socket pins of the chip | 28 | 32 | 24 |
| Pulse width when `pulse-delay` is 0 | 1000 µs | 100 µs | 500 µs |
| `max_pulses` | 25 | 25 | 255 |
| `energy_cap_us` | 0 (no cap) | 0 (no cap) | 50000 |
| `verify_mode` | `VERIFY_PER_PULSE_PLUS_FINAL` | `VERIFY_PER_PULSE_PLUS_FINAL` | `VERIFY_PER_PULSE` |
| `vpp_path` | `VPP_PATH_DROP_RESISTOR` | `VPP_PATH_DROP_RESISTOR` | `VPP_PATH_DIRECT_VPE` |

**VPP route.** `eprom_hv_route_mask()` selects the route in this order:

1. `FLAG_VPE_AS_VPP` (host option `--vpe-as-vpp`) selects the direct VPE rail,
   `CTRL_VPP_REGULATOR_ENABLE`.
2. `VPP_PATH_DIRECT_VPE` selects the direct VPE rail.
3. A requested VPP above 17380 mV selects the direct VPE rail. The drop resistor cannot supply
   more.
4. All other cases use `CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE`. The drop resistor
   lowers VPE to the VPP level.

`CTRL_VPE_ENABLE` switches the rail onto the OE/VPP line of the chip. The firmware uses
`CTRL_VPP_P1_ENABLE` in its place when the host puts VPP on socket pin 1. That is pin 1 of a
28-pin or 32-pin chip, or pin 21 of a 24-pin chip. A jumper on the shield connects that switch to
the correct socket pin. The
[Shield Revisions](https://github.com/henols/firestarter/wiki/Shield-Revisions) wiki page gives the
jumper settings.

**VPP check.** Before a read, write, erase or chip-ID command, the firmware switches the route on, waits
100 ms and reads the rail on A2. It does not connect the rail to the socket during this check. A
value more than 500 mV above the requested VPP sends `MSG_ERR_VPP_HIGH` (`0xB8`). With `--force`
it sends a warning. A value below 95 % of the requested VPP sends `MSG_WARN_VPP_LOW` (`0x81`). On
Rev 0 hardware the firmware skips the check and sends a warning.

**Write.** The firmware writes one data block at a time:

1. If the regulator is off, it switches the route on and waits 500 ms. A successful block leaves
   the route on for the next block.
2. It reads each byte of the block that is not `0xFF` and marks each byte that does not match.
3. It switches `CTRL_VPE_ENABLE` on and waits 1000 µs. It pulses CE low on each marked byte for
   the pulse width. It waits 100 µs and switches `CTRL_VPE_ENABLE` off.
4. It reads the marked bytes again. Step 3 and step 4 repeat until all bytes match.

The pulse width does not change between pulses. The route must be off for each read, because
Program Verify needs OE/VPP low (Winbond W27C512 datasheet, operating-mode table). After
`max_pulses` pulses the write fails with `MSG_ERR_MAX_PULSES` (`0xBD`). On `0x0B`, a pulse total
that reaches `energy_cap_us` fails with `MSG_ERR_ENERGY_CAP` (`0xBE`). No row applies an
overprogram pulse. The limit of 25 pulses comes from the Winbond W27C512 "Smart Programming
Algorithm 2" flowchart and ST M27C512 Rev. 3, Fig. 4. The 50 ms cap on `0x0B` is the
per-location pulse `t_w(PR)` in the TI TMS 2516 datasheet (programming timing table).

**Verify.** With `VERIFY_PER_PULSE_PLUS_FINAL` the firmware reads the full block one more time
after the loop. A mismatch sends `MSG_ERR_VERIFY` (`0xAF`). `0x0B` does no final pass.

**Erase.** The firmware erases only when the host sets `FLAG_CAN_ERASE`. A UV-EPROM has no
electrical erase. For an electrically erasable part, the firmware puts VPE on A9 and on OE/VPP and
holds CE low for 100 ms. That is the typical `T_PWE` of 95 to 105 ms (Winbond W27C512 datasheet).
A write erases the chip first, unless the host sets `FLAG_SKIP_ERASE`.

**Chip ID.** The firmware switches the regulator on without the drop resistor, puts VPP on A9 and
reads addresses `0x0000` and `0x0001`.

## `0x0D`: 5 V parallel EEPROM

**Write init.** If the database gives a chip ID, the firmware checks it first. It puts VPP on A9
and reads the ID at `memory-size − 64`. The firmware then sends the six-byte SDP disable
`AA, 55, 80, AA, 55, 20` to `5555`/`2AAA`. The host option `--skip-sdp-unlock` omits it. The
firmware reports the time that the sequence took. It sends a warning when the time is more than
100 µs per byte (`t_BLC`). It then waits 10 ms, the `t_WC` of the Microchip AT28C256 datasheet (p. 10), and polls the
DQ6 toggle bit at `0x5555`. The chip does not report its SDP state. A sent sequence
therefore does not prove the state before or after it.

**Write.** The page size comes from `page-size`. The firmware uses 64 when the value is 0, is not
a power of two, or is more than 512. After the last byte of a page, the firmware polls DQ7 of that
byte for at most 2000 reads. A timeout sends `MSG_ERR_EEPROM_TIMEOUT` (`0xB2`). The firmware then
reads each byte of the page back. A mismatch sends `MSG_ERR_VERIFY` (`0xAF`). The firmware does no
blank check before a write, because the chip erases each page when it writes it.

**Erase.** The firmware sends the SDP disable and then the six-byte software chip erase
`AA, 55, 80, AA, 55, 10`. It waits 20 ms (`t_EC`) with no poll (Atmel application note
"Software Chip Erase", Rev. 0544B-10/98). The erase is always for the full chip.

> [!WARNING]
> Do not add the datasheet hardware chip-erase mode. It puts 12 V on OE, pin 22 of a 28-pin part,
> and that voltage can damage a 5 V EEPROM. `scripts/check_erase_no_vpp.py` fails when the erase
> function writes a high-voltage control-register bit.

**SDP commands.** `CMD_SDP_UNLOCK` sends the SDP disable. `CMD_SDP_LOCK` sends
`5555<-AA, 2AAA<-55, 5555<-A0` and waits 10 ms.

## `0x10`: Intel 28F command-register flash

**VPP.** The firmware switches on `CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE` and waits
500 ms. It checks the rail with the same limits as the EPROM protocols. If the check fails, it
switches VPP off before it returns.

**Write.** For each byte the firmware writes `0x40` and then the data byte. It then reads the
status register until bit 7 is 1, for at most 150 ms. Status bit 4 sends `MSG_ERR_INTEL_VPP`
(`0xB4`). Status bit 3 sends `MSG_ERR_INTEL_PROGRAM` (`0xB5`). A timeout sends
`MSG_ERR_INTEL_SR_TIMEOUT` (`0xB6`).

**Erase.** The firmware writes `0x20` and then `0xD0` to address 0. It polls the status register
for at most 15 s. A write erases the chip first when the host sets `FLAG_CAN_ERASE` and does not
set `FLAG_SKIP_ERASE`.

**End and chip ID.** At the end of the operation the firmware writes `0xFF` (read-array mode) and
switches VPP off. The chip ID writes `0x90`, reads `0x0000` and `0x0001` and writes `0xFF`.

## `0x0E`, `0x27`, `0x28` and `0x29`: SRAM, NVRAM and FRAM

`configure_sram()` adds nothing to the common memory functions in `memory.cpp`. The handler never
switches on the VPP regulator. Read and write are the only commands.

- **Write.** The host bus configuration holds WE low. The firmware sets the address and the data
  and pulses CE low.
- **Read.** The firmware holds WE high, sets OE and CE low and reads the data bus.

There is no erase.

## Pulse-width override and program-VCC limit

The host option `firestarter write --pulse-us N` accepts 1 to 65535 µs. That range copies the
minipro `-o pulse=N` option, which is a `uint16`. It is not a wire limit. `json_parser.c` reads
`pulse-delay` into a `uint32_t` with no clamp. `configure_eprom()` refuses a pulse wider than
`energy_cap_us` with `MSG_ERR_PULSE_TOO_WIDE` (`0xAE`), before any high voltage is on. Only `0x0B`
has a cap, so only `0x0B` can refuse a pulse width.

The vendor EPROM algorithms program and verify at a raised VCC to check the cell margin. The shield
cannot raise VCC. The firmware therefore verifies at the normal VCC. It gives the datasheet timing
and pulse count, but not the margin check.

## Protocol IDs that the firmware does not implement

- **`0x34`** has one database chip, the X88C64. It has no handler. The final fail-closed call in
  `configure_memory()` refuses it.
- **`0x11`, `0x2A`, `0x2B` and `0x2C`** are FWH and GAL/PLD protocols. The RURP bus cannot drive
  them. `configure_memory()` names them and sends them to `configure_not_implemented()`.
- **`0x35` and `0x39`** go to the `0x05` handler. No database chip uses them.
- **All other values**, `0` included, go to `configure_not_implemented()`.
