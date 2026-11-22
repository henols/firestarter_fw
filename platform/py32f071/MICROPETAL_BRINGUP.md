# MicroPetal board-arrival checklist

Status: **software-tested only; no board flashed or measured**. The default
`micropetal_uno` image intentionally refuses all socket operations and suppresses
socket GPIO writes. Do not remove either guard just to try a read.

## 1. Identify and retain the exact build

Download the `firestarter_micropetal_uno` artifact from the green PY32F071 CI run
for the current PR head. Keep `BUILD_COMMIT.txt`, `BUILD_BOARD.txt`, `SHA256SUMS`,
BIN, HEX, ELF and MAP together. Check hashes before flashing:

```sh
sha256sum -c SHA256SUMS
```

On macOS without `sha256sum`, use `shasum -a 256 -c SHA256SUMS`.
Do not use the generic `firestarter_py32f071` image. The ELF/MAP are for SWD
inspection and RAM/stack investigation, not different firmware variants.

To reproduce locally, follow [MICROPETAL.md](MICROPETAL.md), adding
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` to configure, then run:

```sh
FIRESTARTER_PY32_COMPILE_COMMANDS="$PWD/build/micropetal_uno/compile_commands.json" \
FIRESTARTER_PY32_BOARD=micropetal_uno \
python3 -m pytest tests/test_py32_command_gate.py tests/test_micropetal_gpio.py \
  tests/test_py32_usb_rx.py tests/test_py32_usb_transport.py -v
```

Use a complete GNU Arm toolchain including newlib (the Homebrew compiler-only
package may lack standard headers). CI uses Ubuntu's gcc-arm-none-eabi package.

## 2. Board alone — shield disconnected

- Confirm PY32F071C1BU6, U4 CAN transceiver DNP, JP4/JP5 open, JP3 1–2 and
  JP7 1–2 as described in the pin-map document. Record assembly/photos.
- Inspect for shorts. Measure VCC and VCCA and verify they match.
- With the shield disconnected and socket empty, follow the factory USB-HID ISP
  sequence in MICROPETAL.md. J3 **3–4** is BOOT; **5–6 shorts the supply**.
  Remove BOOT with power off before application boot. Keep a known-good image
  and factory-ISP/SWD recovery available.
- Record USB enumeration, OS/device path, firmware build and board identity.
  Use a current protocol-compatible Firestarter host, not an older installed
  host just because it finds a serial port. No CLI auto-installer is provided.
- On the selected serial port, run the read-only commands:

  ```sh
  firestarter --port /dev/ACTUAL_PORT fw
  firestarter --port /dev/ACTUAL_PORT config
  firestarter --port /dev/ACTUAL_PORT hw
  ```

  Identity must be `micropetal_uno`, buffer 4096. An ADC-derived revision with
  **no shield attached is meaningless**, not a passed detection test.
- Repeat connect/disconnect, reset and USB reconfiguration. Check configuration
  survives a power cycle. Check button input with a debugger/diagnostic harness;
  no extra GPIO-driving diagnostic firmware is included.
- Inspect reset/startup pin states with a scope/logic analyzer. D0–D7 and socket
  control outputs must remain high impedance; PB13's LED must not be mistaken
  for an independently controllable status LED (it shares /CE).

## 3. Empty-socket instrumentation — separate validation build required

Do not attach a powered shield to the present high-impedance bring-up image:
shield latches can retain an old state. Prepare a reviewed, bounded test build
that establishes CE/OE inactive and clears latches before energizing routes.
The normal image's output blocks remain until this stage has passed.

| Check | Required evidence (all currently not run) |
|---|---|
| Revision detect | A3 raw voltage/count versus VCCA; physical PCB revision; detected/effective override. Stock 2.0–2.3 share the 2.0 detect class. Test ADC failure/UNKNOWN. |
| Data and latches | Every data value, input/output turnaround, inactive CE/OE during latch changes, latch setup/hold at 48 MHz. |
| Physical routes | Compare logical register, mapped byte, and actual socket pins per revision. Rev 2 A18 aliases pin-1 VPP; do not assume it is a harmless address line. |
| Supply/ADC | Meter VCC/VCCA and divider input; validate ADC scaling/reference and compare passive rail readings. Generic host `cal` assumes AVR's nominal bandgap; do not treat it as verified ARM calibration. |
| Reset/interruptions | Power-on, reset, host disconnect and interrupted operations: no unintended VPP/VPE enable. ARM watchdog is still unimplemented. |
| Transport | Real 4096-byte COBS/CRC round trips, sustained transfers, delayed/stopped IN reads, USB reconnect after latched TX failure. No truncated frame accepted. |
| RAM/timing | Measure peak stack/RAM headroom and USB ISR interaction; static linker headroom is not a peak measurement. |

Record meter readings at socket pin 1, A9 and relevant VPP/VPE contacts, jumper
positions, waveform captures and exact build hashes. Do not count simulation as
physical validation. A fresh empty-socket confirmation is required before any
flash or route probe; no chip may be present for this stage.

## 4. First chip — only after empty-socket acceptance

Make a separate reviewed change removing the guards only after measured route,
voltage and timing evidence is recorded for the intended shield revision.
Start with a known expendable read-only part, confirm its read-supply conditions,
and compare two complete dumps by exact length and SHA-256 against a known image.
Validate programming/erase separately; a repeatable read proves neither.
