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
