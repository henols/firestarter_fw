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

Set `FIRESTARTER_PY32_COMPILE_COMMANDS` to a CMake-generated compile database.
Tests derive the board identity from its definitions, remove `NDEBUG` so assertions
execute, and fail when expected target/provisional flags are absent. Without a
database ARM tests skip; the ARM CI supplies it. Board-specific GPIO tests run in
both matrix legs; transport and command-gate tests run once on the generic target.

The USB integration test compiles the production backend and shared encoder/decoder.
It covers the production block size plus a test-only 4 KiB stress configuration,
CRC/COBS errors and resynchronization, ring wraparound, OUT backpressure, stalled IN
transfers, DTR recovery and session changes mid-frame. It reuses the native suite's
reference encoders. Hardware enumeration, throughput and timing remain unverified.
