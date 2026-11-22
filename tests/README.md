# Firmware behavior tests

`test/native/avr/` contains PlatformIO Unity tests that execute the compiled firmware with
host-side hardware fakes. Run both `pio test -e native` and `pio test -e native_nodevtools`.

`tests/` contains Python tests that compile and execute production firmware modules or
preprocess actual safety guards; run `python3 -m pytest tests/`.

Tests must assert **observable behavior**: outputs, state transitions, error/refusal paths,
hardware-control calls and safe compile-time decisions. Avoid tests that only check comments,
exact source text, line numbers, source Git SHA values, historical branch ancestry, design
records or test-fixture metadata. For source changes that affect behavior, add executable
regressions rather than replacing code review with a text-matching test.

### Actual ARM build configurations

For the command-gate, GPIO-isolation and USB RX regressions, set both
`FIRESTARTER_PY32_COMPILE_COMMANDS` to the CMake-generated compile database and
`FIRESTARTER_PY32_BOARD` to the intended target (`py32f071` or `micropetal_uno`).
The tests use production definitions, remove `NDEBUG` so assertions execute, and
fail if the expected target/provisional flags are missing. Without an ARM database
these tests skip; the ARM CI matrix always supplies both variables. The USB test
queues a full worst-case encoded block before draining it and repeats across ring
wraparound. Hardware USB enumeration and throughput still require a real board.

The PY32 USB transport integration regression compiles the production USB backend
and shared frame encoder/decoder with host fakes and the **actual ARM CMake flags**.
It checks complete 4096-byte COBS/CRC frames, invalid-frame resynchronization,
and outgoing MSG_DATA_CHUNK framing during normal and stalled USB IN transfers.
