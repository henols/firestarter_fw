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
| Shield revision detect | A3 | PA3 (not implemented by the ARM backend) |

The onboard LED is active low on PB13. BTN is on PB14. Leave JP4 and JP5 open:
they otherwise join A4/A5 to D12/D13. USB is native, not a UART bridge; do not
use the board's non-Uno-compatible UART pin pair for host communication.

## Why socket operations remain disabled

A header match is not electrical compatibility. In particular:

- U4 SN65HVD230 RXD/TXD are connected to PB8/PB9. RXD is an output and conflicts
  with a latch strobe. Isolate the transceiver signals before using these pins.
  F071 has no CAN controller; the populated transceiver is for the F072 option.
- JP3 defaults to 5 V VCC, but SN65HVD230 is specified for 3.0–3.6 V operation.
  With power disconnected, open JP3 1–2 and bridge 2–3 for 3.3 V. Verify the
  modification and supply voltage before USB connection; JP7 1–2 makes VCCA
  follow VCC. Do not treat the assembled board as a 5 V Uno replacement.
- A stock RURP shield is a 5 V design. Establish data/control level compatibility
  and protect PA2 from the shield's VPP-divider maximum voltage at 3.3 V VCCA.
  The existing divider constants do not prove that its output is safe.
- The ARM backend does not yet detect the shield revision on A3. The revision
  controls physical high-voltage routing. It must be resolved before socket use.

This target retains `RURP_PINMAP_PROVISIONAL`. In addition it suppresses **all
socket GPIO output writes**, including startup, voltage-monitor setup and command
cleanup. D0–D7 stay inputs; control pins stay at their reset input state. PA2 is
analog input and PB14 is the button input. USB/configuration are available for
bring-up; voltage monitoring is passive only and cannot enable the regulator.
An attached shield can retain old latch state: do not attach it for this phase.
No read, program, erase or chip-ID capability is claimed.

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

Resolve transceiver isolation, shield levels, ADC range and revision mapping in a
reviewed adapter/wiring plan. Then measure startup/reset states, every data value,
bus direction changes, latch/CE/OE sequencing, ADC calibration, USB transport and
PROM timing on an empty socket. Only after this evidence should a separate change
remove the provisional and GPIO-output blocks. Recheck every shield revision's
physical high-voltage routes; a successful compile is not a hardware test.
