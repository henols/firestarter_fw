<p align="left"><img src="https://raw.githubusercontent.com/henols/firestarter/main/images/branding/firestarter_logo_horizontal.png" alt="Firestarter EPROM Programmer" width="400"></p>

# Shield pin assignments

This page shows how the firmware drives the RURP shield. The socket pin maps for each chip family
and the DIP24 adapter wiring are on the [Pin Maps](https://github.com/henols/firestarter/wiki/Pin-Maps)
wiki page.

## Arduino pins

| Arduino pin | Shield signal | Firmware name |
|---|---|---|
| D0 to D7 | Data bus D0 to D7 | `rurp_write_data_buffer()`, `rurp_read_data_buffer()` |
| D8 | Low address byte latch | `LEAST_SIGNIFICANT_BYTE` (`0x01`) |
| D9 | High address byte latch | `MOST_SIGNIFICANT_BYTE` (`0x02`) |
| D10 | Chip OE, active low | `OUTPUT_ENABLE` (`0x04`) |
| D11 | Control-register latch | `CONTROL_REGISTER` (`0x08`) |
| D12 | User button, Uno only | `USER_BUTTON` (`0x10`) |
| D13 | Chip CE, active low | `CHIP_ENABLE` (`0x20`) |
| A2 | VPP rail, through the voltage divider | `PIN_VPP_VOLTAGE_ADC` |
| A3 | Shield revision, through the R41 detect divider | `PIN_HW_REVISION_DETECT_ADC` |

The hex values in the third column are the bit masks that `rurp_set_control_pin()` and
`rurp_write_to_register()` use. The definitions are in `include/rurp_shield.h` and
`include/rurp_pinout.h`.

On `uno` and `uno328pb`, the data bus is port D and the control pins are port B. D0 and D1 are
also the serial lines. These builds define `SERIAL_ON_IO`. The firmware stops the UART while it
drives the data bus and starts it again after the operation step.

On `leonardo`, the USB serial port is separate from the data bus. The data bits are on PD2, PD3,
PD1, PD0, PD4, PC6, PD7 and PE6, in the order D0 to D7. The control bits are on PB4 to PB7, PD6
and PC7. The Leonardo build has no user button. The mapping is in
`src/boards/leonardo_rurp_shield.cpp`.

The firmware reads A3 only in a build that defines `HARDWARE_REVISION`. All AVR environments in
`platformio.ini` define it.

## Control register

The control register is an 8-bit latch. The firmware writes it through D11. Bits 0 to 7 of the
latch drive these signals in a build without `HARDWARE_REVISION`:

| Bit | Mask | Name | Signal |
|---:|---|---|---|
| 0 | `0x01` | `CTRL_VPP_VPE_DROP_ENABLE`, `CTRL_ADDRESS_LINE_16` | Drop resistor in the VPE path, and address line 16 |
| 1 | `0x02` | `CTRL_VPP_A9_ENABLE` | VPP onto A9, for the chip-ID read |
| 2 | `0x04` | `CTRL_VPE_ENABLE` | VPE onto the OE/VPP line |
| 3 | `0x08` | `CTRL_VPP_P1_ENABLE` | VPP onto socket pin 1 |
| 4 | `0x10` | `CTRL_ADDRESS_LINE_17` | Address line 17 |
| 5 | `0x20` | `CTRL_ADDRESS_LINE_18` | Address line 18 |
| 6 | `0x40` | `CTRL_READ_WRITE` | Read or write direction |
| 7 | `0x80` | `CTRL_VPP_REGULATOR_ENABLE` | VPP regulator |

A build with `HARDWARE_REVISION` uses a 9-bit logical value. Address line 16 is `0x01` and the drop
resistor is `0x100`. All other names keep the values above. At each register write,
`rurp_map_ctrl_reg_for_hardware_revision()` in `include/rurp_hw_rev_utils.h` converts the logical
value to the physical byte for the detected revision:

| Physical bit | Rev 0 and Rev 1 | Rev 2.0 to Rev 2.3 |
|---:|---|---|
| 0 (`0x01`) | Drop resistor and address line 16 | Drop resistor |
| 1 (`0x02`) | VPP onto A9 | VPP onto A9 |
| 2 (`0x04`) | VPE onto OE/VPP | VPE onto OE/VPP |
| 3 (`0x08`) | VPP onto pin 1 | VPP onto pin 1 and address line 18 |
| 4 (`0x10`) | Address line 17 | Address line 17 |
| 5 (`0x20`) | Address line 18 | Address line 16 |
| 6 (`0x40`) | Read or write | Read or write |
| 7 (`0x80`) | VPP regulator | VPP regulator |

Two signals share one bit on each revision group. On Rev 0 and Rev 1, address line 16 and the drop
resistor share bit 0. On Rev 2.x, address line 18 and VPP-onto-pin-1 share bit 3. The firmware
selects the table from the revision that it reads on A3. The `hardware_revision` value in the
stored configuration overrides that reading. For an unknown revision, the firmware writes 0 to the
register, so it switches on no voltage.

> [!CAUTION]
> A wrong revision gives a wrong bit map. The chip then gets wrong addresses. An address bit can
> also set a voltage signal. On a Rev 1 board that the firmware reads as Rev 2, address line 18
> sets bit 3, which is VPP onto pin 1 on Rev 1. Make sure that the revision is correct before you
> program a chip.

The Rev 2 latch schematic is in [document/rurp_ctrl_reg_rev2.png](document/rurp_ctrl_reg_rev2.png).
It shows the 74HC573 outputs Q0 to Q7 and the net names. The schematic names the bit 7 net
`REG_DISABLE`.
