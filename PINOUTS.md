# Pinouts and shield wiring — firmware reference

Implementation reference: the socket pin map for every chip family the firmware
drives, the DIP24-to-DIP32 adapter mapping, and the RURP shield's own pin and
control-register assignments.

The user-facing page — which chips need an adapter, how to wire one, and the
safety guarantees — lives on the wiki:
https://github.com/henols/firestarter/wiki/Pin-Maps

The per-family tables below are generated from
`firestarter_app/firestarter/data/pinouts.json`, which is what the firmware is
actually configured from. Regenerate rather than hand-edit them.

---

## Chip socket pin maps

Pin 1 is the end marked by the notch or the dot on the package.

The 24-pin UV families run 25 V on VPP during programming. A family with no VPP
pin never sees a boosted rail.

### `DIP24_2532` — TI 2532 (4KB) — non-JEDEC 24-pin UV-EPROM

**Programming voltage: 25.0 V.** Read voltage 5.0 V.

VPP pin: yes — pin 21

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | A7 | 13 | D3 |
| 2 | A6 | 14 | D4 |
| 3 | A5 | 15 | D5 |
| 4 | A4 | 16 | D6 |
| 5 | A3 | 17 | D7 |
| 6 | A2 | 18 | A11 |
| 7 | A1 | 19 | A10 |
| 8 | A0 | 20 | /CE |
| 9 | D0 | 21 | VPP |
| 10 | D1 | 22 | A9 |
| 11 | D2 | 23 | A8 |
| 12 | GND | 24 | tied high |

### `DIP24_2716` — JEDEC 2716 (2KB)

**Programming voltage: 25.0 V.** Read voltage 5.0 V.

VPP pin: yes — pin 21

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | A7 | 13 | D3 |
| 2 | A6 | 14 | D4 |
| 3 | A5 | 15 | D5 |
| 4 | A4 | 16 | D6 |
| 5 | A3 | 17 | D7 |
| 6 | A2 | 18 | /CE |
| 7 | A1 | 19 | A10 |
| 8 | A0 | 20 | /OE |
| 9 | D0 | 21 | VPP |
| 10 | D1 | 22 | A9 |
| 11 | D2 | 23 | A8 |
| 12 | GND | 24 | tied high |

### `DIP24_2732` — JEDEC 2732 (4KB)

VPP pin: yes — pin 20

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | A7 | 13 | D3 |
| 2 | A6 | 14 | D4 |
| 3 | A5 | 15 | D5 |
| 4 | A4 | 16 | D6 |
| 5 | A3 | 17 | D7 |
| 6 | A2 | 18 | /CE |
| 7 | A1 | 19 | A10 |
| 8 | A0 | 20 | /OE |
| 9 | D0 | 21 | A11 |
| 10 | D1 | 22 | A9 |
| 11 | D2 | 23 | A8 |
| 12 | GND | 24 | tied high |

### `DIP24_2816` — JEDEC 24-pin 5V parallel EEPROM (AT28C16/AT28C04 family)

VPP pin: none (5 V only)

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | A7 | 13 | D3 |
| 2 | A6 | 14 | D4 |
| 3 | A5 | 15 | D5 |
| 4 | A4 | 16 | D6 |
| 5 | A3 | 17 | D7 |
| 6 | A2 | 18 | /CE |
| 7 | A1 | 19 | A10 |
| 8 | A0 | 20 | /OE |
| 9 | D0 | 21 | /WE |
| 10 | D1 | 22 | A9 |
| 11 | D2 | 23 | A8 |
| 12 | GND | 24 | VCC |

### `DIP24_6116` — JEDEC 24-pin 5V SRAM (6116/6264-style)

VPP pin: none (5 V only)

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | A7 | 13 | D3 |
| 2 | A6 | 14 | D4 |
| 3 | A5 | 15 | D5 |
| 4 | A4 | 16 | D6 |
| 5 | A3 | 17 | D7 |
| 6 | A2 | 18 | /CE |
| 7 | A1 | 19 | A10 |
| 8 | A0 | 20 | /OE |
| 9 | D0 | 21 | /WE |
| 10 | D1 | 22 | A9 |
| 11 | D2 | 23 | A8 |
| 12 | GND | 24 | VCC |

### `DIP28_27256` — JEDEC 27256

VPP pin: yes — pin 1

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | VPP | 15 | D3 |
| 2 | A12 | 16 | D4 |
| 3 | A7 | 17 | D5 |
| 4 | A6 | 18 | D6 |
| 5 | A5 | 19 | D7 |
| 6 | A4 | 20 | /CE |
| 7 | A3 | 21 | A10 |
| 8 | A2 | 22 | /OE |
| 9 | A1 | 23 | A11 |
| 10 | A0 | 24 | A9 |
| 11 | D0 | 25 | A8 |
| 12 | D1 | 26 | A13 |
| 13 | D2 | 27 | A14 |
| 14 | GND | 28 | VCC |

### `DIP28_27512` — JEDEC 27512

VPP pin: yes — pin 22

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | A15 | 15 | D3 |
| 2 | A12 | 16 | D4 |
| 3 | A7 | 17 | D5 |
| 4 | A6 | 18 | D6 |
| 5 | A5 | 19 | D7 |
| 6 | A4 | 20 | /CE |
| 7 | A3 | 21 | A10 |
| 8 | A2 | 22 | /OE |
| 9 | A1 | 23 | A11 |
| 10 | A0 | 24 | A9 |
| 11 | D0 | 25 | A8 |
| 12 | D1 | 26 | A13 |
| 13 | D2 | 27 | A14 |
| 14 | GND | 28 | VCC |

### `DIP28_2764` — JEDEC 2764/128

VPP pin: yes — pin 1

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | VPP | 15 | D3 |
| 2 | A12 | 16 | D4 |
| 3 | A7 | 17 | D5 |
| 4 | A6 | 18 | D6 |
| 5 | A5 | 19 | D7 |
| 6 | A4 | 20 | /CE |
| 7 | A3 | 21 | A10 |
| 8 | A2 | 22 | /OE |
| 9 | A1 | 23 | A11 |
| 10 | A0 | 24 | A9 |
| 11 | D0 | 25 | A8 |
| 12 | D1 | 26 | A13 |
| 13 | D2 | 27 | /PGM |
| 14 | GND | 28 | VCC |

### `DIP28_28C256` — JEDEC 28-pin 5V parallel EEPROM (28C256 family)

VPP pin: none (5 V only)

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | A14 | 15 | D3 |
| 2 | A12 | 16 | D4 |
| 3 | A7 | 17 | D5 |
| 4 | A6 | 18 | D6 |
| 5 | A5 | 19 | D7 |
| 6 | A4 | 20 | /CE |
| 7 | A3 | 21 | A10 |
| 8 | A2 | 22 | /OE |
| 9 | A1 | 23 | A11 |
| 10 | A0 | 24 | A9 |
| 11 | D0 | 25 | A8 |
| 12 | D1 | 26 | A13 |
| 13 | D2 | 27 | /WE |
| 14 | GND | 28 | VCC |

### `DIP28_28C64` — JEDEC 28-pin 5V parallel EEPROM 8K (28C64 family)

VPP pin: none (5 V only)

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | NC | 15 | D3 |
| 2 | A12 | 16 | D4 |
| 3 | A7 | 17 | D5 |
| 4 | A6 | 18 | D6 |
| 5 | A5 | 19 | D7 |
| 6 | A4 | 20 | /CE |
| 7 | A3 | 21 | A10 |
| 8 | A2 | 22 | /OE |
| 9 | A1 | 23 | A11 |
| 10 | A0 | 24 | A9 |
| 11 | D0 | 25 | A8 |
| 12 | D1 | 26 | NC |
| 13 | D2 | 27 | /WE |
| 14 | GND | 28 | VCC |

### `DIP28_JEDEC_SRAM_8K` — JEDEC 28-pin SRAM/FRAM 8K (6264/FM1608 family)

VPP pin: none (5 V only)

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | NC | 15 | D3 |
| 2 | A12 | 16 | D4 |
| 3 | A7 | 17 | D5 |
| 4 | A6 | 18 | D6 |
| 5 | A5 | 19 | D7 |
| 6 | A4 | 20 | /CE |
| 7 | A3 | 21 | A10 |
| 8 | A2 | 22 | /OE |
| 9 | A1 | 23 | A11 |
| 10 | A0 | 24 | A9 |
| 11 | D0 | 25 | A8 |
| 12 | D1 | 26 | NC |
| 13 | D2 | 27 | /WE |
| 14 | GND | 28 | VCC |

### `DIP32_27C020` — JEDEC 32-pin UV-EPROM ≤256K (27C010/27C020 family — PGM on pin 31)

VPP pin: yes — pin 1

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | VPP | 17 | D3 |
| 2 | A16 | 18 | D4 |
| 3 | A15 | 19 | D5 |
| 4 | A12 | 20 | D6 |
| 5 | A7 | 21 | D7 |
| 6 | A6 | 22 | /CE |
| 7 | A5 | 23 | A10 |
| 8 | A4 | 24 | /OE |
| 9 | A3 | 25 | A11 |
| 10 | A2 | 26 | A9 |
| 11 | A1 | 27 | A8 |
| 12 | A0 | 28 | A13 |
| 13 | D0 | 29 | A14 |
| 14 | D1 | 30 | A17 |
| 15 | D2 | 31 | /WE |
| 16 | GND | 32 | VCC |

### `DIP32_28C512_EEPROM` — JEDEC 32-pin 5V parallel EEPROM 64K (28C512 family)

VPP pin: none (5 V only)

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | — | 17 | D3 |
| 2 | — | 18 | D4 |
| 3 | A15 | 19 | D5 |
| 4 | A12 | 20 | D6 |
| 5 | A7 | 21 | D7 |
| 6 | A6 | 22 | /CE |
| 7 | A5 | 23 | A10 |
| 8 | A4 | 24 | /OE |
| 9 | A3 | 25 | A11 |
| 10 | A2 | 26 | A9 |
| 11 | A1 | 27 | A8 |
| 12 | A0 | 28 | A13 |
| 13 | D0 | 29 | A14 |
| 14 | D1 | 30 | /WE |
| 15 | D2 | 31 | — |
| 16 | GND | 32 | VCC |

### `DIP32_SST39SF040` — JEDEC 32-pin 5V Flash (SST39SF040/AM29F040 family)

VPP pin: none (5 V only)

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | A18 | 17 | D3 |
| 2 | A16 | 18 | D4 |
| 3 | A15 | 19 | D5 |
| 4 | A12 | 20 | D6 |
| 5 | A7 | 21 | D7 |
| 6 | A6 | 22 | /CE |
| 7 | A5 | 23 | A10 |
| 8 | A4 | 24 | /OE |
| 9 | A3 | 25 | A11 |
| 10 | A2 | 26 | A9 |
| 11 | A1 | 27 | A8 |
| 12 | A0 | 28 | A13 |
| 13 | D0 | 29 | A14 |
| 14 | D1 | 30 | A17 |
| 15 | D2 | 31 | /WE |
| 16 | GND | 32 | VCC |

### `DIP32_STD` — JEDEC 32-Pin Standard

VPP pin: yes — pin 1

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | VPP | 17 | D3 |
| 2 | A16 | 18 | D4 |
| 3 | A15 | 19 | D5 |
| 4 | A12 | 20 | D6 |
| 5 | A7 | 21 | D7 |
| 6 | A6 | 22 | /CE |
| 7 | A5 | 23 | A10 |
| 8 | A4 | 24 | /OE |
| 9 | A3 | 25 | A11 |
| 10 | A2 | 26 | A9 |
| 11 | A1 | 27 | A8 |
| 12 | A0 | 28 | A13 |
| 13 | D0 | 29 | A14 |
| 14 | D1 | 30 | A17 |
| 15 | D2 | 31 | A18 |
| 16 | GND | 32 | VCC |

---

## RURP shield pin assignments

Two Arduino analog inputs are read directly:

| Arduino pin | Reads |
|---|---|
| A2 | VPP rail voltage, through the divider |
| A3 | shield revision, through the R41 detect divider |

The bus is driven through an 8-bit control register latch:

| Bit | Mask | Signal |
|---:|---|---|
| 0 | `0x01` | drop resistor in the VPP/VPE path |
| 1 | `0x02` | VPP onto A9 (chip identification) |
| 2 | `0x04` | VPE rail enable |
| 3 | `0x08` | VPP onto pin 1 |
| 4 | `0x10` | address line 17 |
| 5 | `0x20` | address line 18 |
| 6 | `0x40` | read / write direction |
| 7 | `0x80` | VPP regulator enable |

Two bits moved between shield generations. On Rev 1, address line 16 shares
bit 0 with the drop resistor and address line 18 is bit 5. On Rev 2, address
line 16 is bit 5 and address line 18 shares bit 3 with VPP-on-pin-1. The
firmware selects the mapping from the revision detected on A3, so a wrong
revision presents as addressing faults rather than an error.

Definitions: `include/rurp_pinout.h`.

---

## DIP24-to-DIP32 adapter

The AT28C04 and AT28C16 families are 24-pin 5 V EEPROMs that fit the 32-pin
socket physically but land write-enable in the wrong place. Chip pin 21 is
write enable; the socket carries write enable on pin 30. Dropped straight in,
chip pin 21 meets the socket's D7 line — electrically harmless, but write
enable can never be asserted, so the chip reads and never writes.

All other pins map straight through, except chip pins 19, 22, 23 and 24, which
shift to socket pins 23, 26, 27 and 32.

Neither layout carries a VPP pin, so a mis-wired adapter cannot put a high
voltage on the chip.

**Pin 21 is not VPP on every 24-pin part.** On the UV families it is; on the
5 V EEPROM families it is write enable. The two layouts are otherwise
physically identical, which is why the distinction matters.
