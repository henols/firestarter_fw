<p align="left"><img src="https://raw.githubusercontent.com/henols/firestarter/main/images/branding/firestarter_logo_horizontal.png" alt="Firestarter EPROM Programmer" width="400"></p>

# PY32F071 design record

This record holds the design facts for the PY32F071 port: the flash geometry, the flash map, the
configuration storage, the flash path, the USB identity and the PCB requirements. The
[README](README.md) gives the status, the build and the install steps.

No PY32F071 board exists. Nothing in this record describes behaviour that anybody saw on the
silicon. Each fact comes from a Puya document, from the pinned SDK, or from the source in this
repository.

## Flash geometry

The source is the Puya PY32F07X Reference Manual V0.2: §4.1 and §4.2.1 (p. 34), and Table 4-1
(p. 34).

| Property | Value | Reference |
|---|---|---|
| Page size | 256 bytes | RM V0.2 §4.1, §4.2.1 |
| Sector size | 8192 bytes | RM V0.2 §4.1, §4.2.1, §4.2.3.5 |
| Main flash | `0x08000000` to `0x0801FFFF`, 128 KiB, 16 sectors, 512 pages | RM V0.2 Table 4-1 |
| Sector 15 | Pages 480 to 511, `0x0801E000` to `0x0801FFFF` | RM V0.2 Table 4-1 |
| Program unit | One full page, as 64 words of 32 bits | RM V0.2 §4.2.3.2 |
| Erase units | Page (`PER`), sector (`SER`) and mass (`MER`) | RM V0.2 §4.2.3.3 to §4.2.3.5 |

The pinned SDK agrees. `platform/py32f071/CMakeLists.txt` pins `OpenPuya/PY32F071_Firmware` at
commit `0ed2f4b4d3391eccfd4491006a30295fd78e32c2`. Its header `py32f071xB.h` defines
`FLASH_PAGE_SIZE` as `0x100` and `FLASH_SECTOR_SIZE` as `0x2000`.

Do not use the "128-byte page, 4 KiB sector" figures. They are correct for the PY32F030 and
PY32F003, not for this part. The comment "Program 128bytes" in `py32f071_hal_flash.h` comes from
the smaller part. The code under it writes 64 words, which is 256 bytes.

## Flash map

`linker/PY32F071xB_FLASH.ld` reserves this map:

| Region or symbol | Origin | Length | Note |
|---|---|---|---|
| `BOOTLOADER (rx)` | `0x08000000` | `0` | Named seam only |
| `FLASH (rx)` | `0x08000000` | `120K` | The application, `0x08000000` to `0x0801DFFF`, sectors 0 to 14 |
| `CONFIG (r)` | `0x0801E000` | `8K` | Sector 15, pages 480 to 511 |
| `RAM (xrw)` | `0x20000000` | `16K` | |
| `__config_page_size` | | `256` | |
| `__config_slot_a_start` | `0x0801E000` | 256 B | Page 480 |
| `__config_slot_b_start` | `0x0801E100` | 256 B | Page 481, a different page-erase unit from slot A |
| `__config_region_end` | `0x08020000` | | |

`CONFIG` is one full sector at the top of the flash. A sector erase of the application region
therefore cannot touch it. The two slots are on different pages, so an erase of one slot cannot
change the other. 7680 B of the 8192 B sector are free. More slots can use that space, and no
address has to move.

The `BOOTLOADER` region has length 0. A non-zero length moves the application's `ORIGIN`. That is
a flash-map migration, not a resize. Each board that has firmware then needs a full re-flash over
DFU or SWD. The vector table itself can move with no cost. The CMSIS device header declares
`__VTOR_PRESENT`, and the compiled `SystemInit` writes `SCB->VTOR` at each boot.

## Configuration storage

AVR boards keep the configuration in EEPROM. The PY32F071 keeps it in two flash slots. Each slot
holds one record, defined in `src/config_storage_dualslot.h`:

```cpp
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    rurp_configuration_t configuration;
    uint32_t sequence;
    uint32_t crc32;
} StoredConfiguration;
```

- **Load.** The firmware reads both slots and keeps the valid record with the higher `sequence`.
  A failed or interrupted save leaves the previous record usable.
- **`version`.** The firmware writes the value 1. No code reads it yet. It is not
  `CONFIG_VERSION`, the `"VER06"` string inside `rurp_configuration_t`.
- **`CONFIG_MAGIC`.** The value is `0x52555250`, the ASCII characters `RURP`. The value is a
  firmware-local choice. It is not `0xFFFFFFFF`, the value of erased flash, and it is not
  `0x00000000`.

**Save.** `py32f071_hal_flash.h` accepts one program type only, `FLASH_TYPEPROGRAM_PAGE`, in
`IS_FLASH_TYPEPROGRAM`. `FLASH_Program_Page` always writes 64 words. A write that is not 32 bits
wide causes a hard fault (RM V0.2 §4.2.3.2). The firmware therefore cannot write a header or CRC
word last as a separate step. It saves in this sequence:

1. Erase the inactive slot.
2. Build the full 256-byte record, with the header and the CRC, in a 4-byte-aligned buffer.
3. Program the page in one operation. The end of that operation is the commit.

The active slot does not change during a save. An interrupted page fails its CRC check at the next
load.

**Validation order.** The firmware treats each byte from a slot as untrusted input. It checks
`magic`, then `length`, then `crc32`. Each check must pass before the next one runs. A `length`
larger than the caller's buffer fails before any copy. The CRC cannot replace this check, because
anybody who can write the flash can also write a matching CRC. The Cortex-M0+ has 16 KiB of SRAM
and the firmware configures no MPU, so no hardware stops an overflow.

**CRC32 is not a security primitive.** It finds accidental corruption only. It gives no
protection against a deliberate change.

**Erase before program.** RM V0.2 §4.2.3.2, step 2, says to read out the 64 words of a page that
holds data before you program it. `FLASH_Program_Page` does not do this. It is correct only on an
erased page, so the firmware always erases the slot first.

**Write protection.** If the write-protection option byte covers the configuration pages, the
hardware skips the page erase and sets `WRPERR` (RM V0.2 §4.5.3 and §4.2.3.3). Each HAL result
other than `HAL_OK` makes the save return `false`.

**Reset and interrupts.** RM V0.2 §4.2.3 (p. 35) says: "If a reset occurs during Flash program and
erase operations, the contents of the Flash memory are not protected." A read of the flash stalls
the bus until the operation ends. The HAL also masks interrupts during the 64-word program. No
interrupt runs during an erase or a program, USB CDC included. The program and erase operations
need HSI on. `src/main.cpp` starts HSI and uses it as the PLL source. If a later clock change stops
HSI, the configuration save stops working. The first boot of an empty board saves a record, which
stalls the CPU for one erase and one program. Nobody measured that time.

**Host side.** The DFU client in `firestarter_app` (`py32_dfu.py`) accepts an image only from
`0x08000000` to `0x0801E000`, which is `ORIGIN(CONFIG)`. Its DfuSe erase covers only the pages
under the image. An install therefore does not erase the configuration. This is the intended
behaviour. Nobody saw it on a board. When the device gives no DfuSe memory layout, the client
uses an erase size of 2048 bytes. That size matches neither the page nor the sector.

## Flash path

This design has three ways to write the flash, in order of priority.

1. **Self-flash bootloader over USB CDC and COBS: the intended primary path.** The host sends the
   image over the serial port that it already uses, with the same COBS framing. A small bootloader
   in the reserved region writes the image to the application region. The host needs only
   `pyserial`, which the CLI already has. This bootloader does not exist yet. When it exists, it
   must never be in its own update path, it must check the application CRC before the jump, and
   an interrupted transfer must leave the board in the bootloader.
2. **Factory USB DFU: the maintainer/manufacturing recovery path.** The factory bootloader in
   system memory uses the same two USB pins as the application. BOOT0 selects it. The CLI has a
   Python DFU client, `py32_dfu.py`, so no external program is necessary. It needs `pyusb`, a
   libusb backend and, on Windows, a WinUSB driver.
3. **SWD on PA13, PA14 and nRST: the last resort.** It needs a probe and access to the board. It
   is the only path that can recover from a bad option-byte state.

The factory USB DFU path does not retire the self-flash bootloader. The DFU path is the recovery
path that the PCB checklist below expects.

An external tool is acceptable on the recovery paths, because only the maintainer and the factory
use them. It is not acceptable on the user path. The design rejects these install routes:

- Puya `PY32DfuTool`: it runs on Windows x64 only.
- `dfu-util`: it is an external program, the same problem as `avrdude`.
- `puyaisp` over the UART bootloader: it needs a second USB-to-serial adapter and access to the
  strap pins.

## Bootloader budget

The Puya factory bootloader uses 12,032 bytes of system memory for USART, I2C and USB DFU (Puya
UM1503/UM1504, §1.1, Table 1-1). In a local build of this tree, the objects that a USB CDC
bootloader needs add up to about 14.6 KiB. These are the clock, flash, CherryUSB, COBS and CRC,
CDC, GPIO, NVIC, `SystemInit`, timing and startup objects. The bootloader logic itself is not in
that number. The estimate for a full bootloader is 17 to 20 KiB.

| Reservation | Result |
|---|---|
| 1 sector (8 KiB) | Too small. The USB, flash and clock objects alone are larger. |
| 2 sectors (16 KiB) | Possible only with direct register access and no HAL. Do not plan with it. |
| 3 sectors (24 KiB) | The recommended size. Reserving it moves the application `ORIGIN`, a flash-map migration. Each flashed board then needs a full re-flash over DFU or SWD. |

The `BOOTLOADER` region keeps `LENGTH = 0` until a bootloader exists. The 24 KiB figure is a plan.
It carries the migration cost above.

## USB identity

`src/usb_cdc.c` sets `FIRESTARTER_USB_VID` to `0x1209` and `FIRESTARTER_USB_PID` to `0x0001`.
`0x1209` is the pid.codes vendor ID for open-source hardware. `1209:0001` is the pid.codes PID for
private testing. It is not an allocated PID. The pid.codes terms ask that source code with this
PID warns that the PID is not unique. `usb_cdc.c` has that warning.

The previous pair was `0x36B7`/`0xFFFF`. `0x36B7` belongs to Puya Semiconductor. The pair came
from the SDK USB CDC example, `usbd_cdc_if.c`, and its Windows driver file, `pycdc.inf`, at SDK
commit `0ed2f4b4d3391eccfd4491006a30295fd78e32c2`. The board then showed the vendor ID of another
company.

**Ship gate: no PY32F071 board ships, and no release advertises a USB identity, until a PID allocated under VID 0x1209 exists.**

To get a PID, open a pull request on the pid.codes registry. The registry asks for public PCB
design files. The request can therefore have to wait for a schematic. The project owner files the
request.

The host DFU client finds a device by the interface class `0xFE/0x01` (`py32_dfu.py`), not by the
vendor and product ID. The value `0x0448` is a device ID in the Puya bootloader parameter table
(UM1504, Table 1-1). It is not a USB product ID.

## PCB checklist

Decide each item before the first schematic. After layout, a change is expensive or impossible.
You fix R3 already when you select the part.

- [ ] **R1 — BOOT0 / nBOOT1 strap reachable.**
  - *Why:* `PF8-BOOT0` is an input with a pull-down after reset. The strap must pull PF8 high through a jumper, button or test point to VDD. Without it, the factory DFU recovery path cannot start. An external pull-down is optional. The self-flash path does not exist yet, so the strap must stay available. `PF8` is not bonded on `QFN56`, which is one more reason to reject that package in R3.
  - *Breaks if omitted:* the recovery path that needs no probe is gone. After the first bad image, only SWD can recover the board.

- [ ] **R2 — SWD pads exposed, including nRST.**
  - *Why:* `PA13` and `PA14` are `SWDIO` and `SWCLK` after reset, but both have other functions. A design that uses them for a different signal loses the last-resort path. `nRST` must be on the same header, because the option bit `nBOOT1` = 0 makes `BOOT0` = 1 select SRAM, and only SWD recovers from that state.
  - *Breaks if omitted:* one bad option-byte write makes the board permanently unusable. The header needs SWDIO, SWCLK, GND, a VDD sense and `nRST`.

- [ ] **R3 — Contiguous PB0–PB7 data bus, and a package that can carry it.**
  - *Why:* the single `IDR` read and the single `BSRR` write need all eight data lines on one port, in one sequence. `PB2` and `PB3` are not bonded on `QFN56`, and `PB2` to `PB7` are not bonded on `QFN32`. The packages that work are `LQFP64`, `CSP64`, `QFN64`, `LQFP48` and `QFN48`. Do not use `QFN56` or `QFN32`.
  - *Breaks if omitted:* the bus needs two ports or a shift and mask, so a read or write is no longer one operation. No layout change can correct this after the part selection.

- [ ] **R4 — HSE crystal footprint laid out but not fitted, and HSE pins kept free.**
  - *Why:* the port runs with `HSE_OFF` and makes the 48 MHz USB clock from HSI and the PLL. The part can trim HSI against `USBD_SOF`, but the SDK example uses the LSE reference. Nobody has shown that untrimmed HSI meets the USB full-speed tolerance.
  - *Breaks if omitted:* a board with no crystal footprint needs a new PCB to add one. An empty footprint costs only two parts that you do not fit. If the HSE pins carry other signals, this option is gone.

- [ ] **R5 — VPP sense on PA4 / ADC channel 4, with its divider a board decision.**
  - *Why:* the provisional map puts the VPP measurement on `PA4` because the Puya `ADC` example uses it. Each board must declare its VPP control mode, and all boards use manual adjustment now. This sense path is the only VPP feedback. No schematic confirms `PA4` yet.
  - *Breaks if omitted:* the firmware cannot read the VPP voltage, and the divider cannot be added after layout without a cut on the board.

- [ ] **R6 — Test points on the data bus and the control strobes.**
  - *Why:* the bus-trace tests run on the `native` environment, not on ARM. Nothing detects a difference between the ARM and the AVR bus sequences. On a first board, probes are the only check.
  - *Breaks if omitted:* at first power-up nobody can tell a firmware fault from a wiring fault. Nobody checked the pin map against hardware.

- [ ] **R7 — USB connector on PA11/PA12, and the D+ pull-up decided before layout.**
  - *Why:* `PA11` and `PA12` are the USB pins for the application and for the factory bootloader. They cannot move. The PHY can have an internal D+ pull-up, or it can need a `1.5 kΩ` resistor to 3V3. The datasheet, the CMSIS header and the CherryUSB port do not say which.
  - *Breaks if omitted:* a board that needs the resistor and does not have it does not enumerate. A board with a second pull-up can enumerate incorrectly or not at all. Read the USB chapter of the reference manual before the first schematic.

### Deliberately undecided

- **Socket.** A ZIF socket or a plain DIP socket. This depends on the number of insertions and on
  a cost limit. Neither exists yet.
- **Connector.** The type of the USB connector and of the SWD header. This depends on the
  enclosure and the board size. Neither exists.
- **Power budget.** This includes whether the board makes VPP or gets it from outside. It depends
  on the VPP circuit, the R5 divider and a cost limit.

## Socket empty before a firmware install

**Before any PY32F071 firmware install — DFU, SWD or otherwise — the PROM socket must be empty.**

This rule is stricter than on the AVR boards, for these reasons:

1. **The pin map is provisional.** `include/boards/py32f071_rurp_shield.h` defines
   `RURP_PY32F071_PINMAP_PROVISIONAL`. All pins except the ADC input are placeholders.
2. **A pin can have the wrong direction.** If the firmware drives a pin as an output and the real
   board connects that pin to a PROM output, the two outputs fight. That can damage the chip on
   the first power-up. The AVR pin map has measurements from three board revisions. This map has
   none.
3. **Nobody measured the startup levels.** The code sets `/CE` and `/OE` high at startup, but
   no measurement confirms it.
4. **A DFU install is the worst case.** The board starts with `BOOT0` high. The application GPIO
   setup does not run. The factory bootloader sets the pins, and no Firestarter code controls
   them.

## Open questions

1. **The factory value of `nBOOT1`.** The datasheet (§2.3) and UM1504 do not give it. The option
   byte chapter of the reference manual can answer it.
2. **The D+ pull-up.** The USB chapter of the reference manual can answer it. Do not guess.
3. **Bootloader entry.** A protocol command or the strap only. With the strap only, R1 must stay
   on every board. A command can make R1 a factory test point, but only after the command exists
   and works.
4. **A pid.codes request before a schematic.** The registry instructions and its FAQ do not agree.
   The project owner can ask the registry.
5. **What the factory bootloader enumerates as.** One USB listing on real silicon can answer it.
6. **The release asset for self-flash.** The host looks for `firestarter_<board>.hex`. A
   self-flash protocol can need a raw binary file.
