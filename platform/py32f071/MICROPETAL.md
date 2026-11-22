# MicroPetal Uno R1 (experimental bring-up)

This target reuses the native PY32F071 backend, not an Arduino core. It targets the
**non-E PY32F071C1BU6**, with 128 KiB flash and 16 KiB SRAM. It does not target F072.
It uses the existing 48 MHz USB clock, USB CDC on PA11/PA12, flash configuration
storage and ADC backend. Firmware identity and output names are `micropetal_uno`.

Hardware source: [MicroPetal](https://github.com/AndersBNielsen/MicroPetal),
`MicroPetal_Uno.kicad_sch` and matching PCB at commit
`0c21572f2c54b36d6858e2456d5876b644cc742c`. This is a source-level mapping, not a
record of a tested assembly. No firmware was flashed during development.

## RURP shield mapping

| Shield signal | Uno header | MicroPetal MCU |
|---|---|---|
| Data D0–D7 | D0–D7 | PB0–PB7 |
| Low address latch | D8 | PB8 |
| High address latch | D9 | PB9 |
| /OE | D10 | PB12 |
| Control latch | D11 | PB15 |
| User button | D12 | PB14, active low |
| /CE | D13 | PB13 |
| VPP divider | A2 | PA2 / ADC channel 2 |
| Shield revision detect | A3 | PA3 / ADC channel 3 |

The onboard LED is active low on PB13. BTN is on PB14. Leave JP4 and JP5 open:
they otherwise join A4/A5 to D12/D13. USB is native, not a UART bridge; do not
use the board's non-Uno-compatible UART pin pair for host communication.

## Why socket operations remain disabled

The owner confirms that **U4 SN65HVD230 is DNP on the F071 assembly**. Its
schematic connections to PB8/PB9 do not create a conflict when the part is not
mounted: D8/D9 are available for the RURP address latches. CAN is an F072 option,
not a populated peripheral on this F071 target.

The non-E PY32F071 datasheet specifies **1.7–5.5 V for VCC and VCCA**, with VCCA
required to match VCC (supply architecture and Table 5-4). The F071/DNP assembly
does not need conversion to 3.3 V for an absent CAN transceiver. Default JP3 1–2
(5 V VCC) and JP7 1–2 (VCCA follows VCC) are not, by themselves, a 5 V shield
compatibility blocker. The earlier populated-CAN and mandatory-3.3-V claims were
incorrect for this assembly. A board with U4 fitted is a different configuration
and must respect that part's supply and signal requirements.

The MicroPetal target now enables the existing shared revision-specific
control-register mapping and revision reporting/override. It samples A3 at 12 bits,
averages eight readings and scales to the existing 10-bit detection bands; ADC
errors report UNKNOWN rather than selecting a route. A2 supplies the legacy
Rev 0/1 discriminator. The generic placeholder target remains unchanged.

This is software preparation, not validated hardware detection. With no shield,
floating ADC inputs can report a meaningless revision. Stock Rev 2.0–2.3 shields
all detect as the Rev 2.0 class; confirm the physical revision and use the existing
configuration override when needed. UNKNOWN and invalid revisions map to zero.
On Rev 2.x, logical A18 aliases the physical pin-1 VPP switch, A16 moves to bit 5,
and the drop bit maps to bit 0; Rev 0/1 keep their original mapping. These routes
are covered by host regressions, but physical backend/timing validation is pending.

This target retains `RURP_PINMAP_PROVISIONAL`. In addition it suppresses **all
socket GPIO output writes**, including startup, voltage-monitor setup and command
cleanup. D0–D7 stay inputs; control pins stay at their reset input state. PA2 is
analog input and PB14 is the button input. USB/configuration are available for
bring-up; voltage monitoring is passive only and cannot enable the regulator.
An attached shield can retain old latch state: do not attach it for this phase.
No read, program, erase or chip-ID capability is claimed.

Supply reference: [Puya PY32F071 datasheet V1.4](https://www.puyasemi.com/download_path/%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C/MCU%20%E5%BE%AE%E5%A4%84%E7%90%86%E5%99%A8/PY32F071%E7%B3%BB%E5%88%97%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.4.pdf).

## Build and install

From the firmware repository root:

```sh
cmake -S platform/py32f071 -B build/micropetal_uno -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DRURP_BOARD=micropetal_uno
cmake --build build/micropetal_uno
```

Outputs: `firestarter_micropetal_uno.elf`, `.bin`, `.hex`, `.map`.
The default `RURP_BOARD=py32f071` retains the original placeholder target.
CI builds both targets and attaches HEX/BIN files to each run. This change does
not publish releases or add a CLI auto-installer/board choice.

The ARM transfer buffer is **4096 bytes**, the host's current advertised-buffer
limit. The USB RX ring holds a complete worst-case COBS block including CRC and
framing overhead. AVR buffer sizes are unchanged.

Use the chip's factory bootloader for now; a custom CDC self-flash bootloader is
deferred. The application still starts at `0x08000000` (no flash-map migration).
Use the factory **USB-HID ISP** path on this board, not the generic target's DFU
instructions. A possible external uploader is
[puyaisp](https://github.com/wagiminator/MCU-Flash-Tools/blob/main/puyaisp.py).
Installation on an actual R1 assembly remains unverified.

1. Keep the shield disconnected and the PROM socket empty.
2. With power off, shunt J3 pins **3–4** (PF8/BOOT0 to VCC), then connect USB or reset.
   BTN is not BOOT. Never shunt J3 5–6: that shorts +5 V to GND.
3. With a compatible `puyaisp`, use `puyaisp -t hid -f firestarter_micropetal_uno.bin`.
4. Power off, remove the BOOT shunt, then reconnect/reset.

SWD is available on J1: 1 GND, 2 PA14/SWCLK, 3 PA13/SWDIO, 4 VCC/reference.
Use a PY32F071-aware tool and do not feed debugger power into a USB-powered board.

## Acceptance before enabling the socket

Bench-validate the ARM shield-revision detection and physical control-register
mapping. Confirm the assembled F071 board has U4 DNP and VCCA follows VCC; verify
the shield divider voltage against VCCA. Then measure startup/reset states, every data value,
bus direction changes, latch/CE/OE sequencing, ADC calibration, USB transport and
PROM timing on an empty socket. Only after this evidence should a separate change
remove the provisional and GPIO-output blocks. Recheck every shield revision's
physical high-voltage routes; a successful compile is not a hardware test.

Use the staged [board-arrival checklist](MICROPETAL_BRINGUP.md) to record evidence.
