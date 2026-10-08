# zero_bootloader_reserve.py — PlatformIO pre-build SCons hook (extra_scripts = pre:)
#
# Makes the flash-limit guards report the ATmega328PB's real 32768 B flash size on
# `uno328pb`, in lockstep with the plain `board_upload.maximum_size = 32768` override
# on `uno` and `leonardo`.
#
# WHY THIS SCRIPT EXISTS (a plain INI override is NOT enough on this one env).
# `boards/ATmega328PB.json` sets `build.core = "MiniCore"`. The atmelavr platform's
# Arduino framework builder has a MiniCore-only code path that runs AFTER
# build_flags/board overrides are applied:
#
#     upload_section["maximum_size"] -= board.get("bootloader.size", get_bootloader_size())
#
# `ATmega328PB.json` has a `bootloader` section but no `bootloader.size` key, so
# `get_bootloader_size()` runs: for `upload_protocol == "urclock"` with `max_size >= 4096`,
# it returns 384. Hence 32768 - 384 = 32384, and a bare `board_upload.maximum_size = 32768`
# in platformio.ini changes nothing, because this subtraction runs on top of it. `uno` and
# `leonardo` use `build.core = "arduino"`, which is NOT in the MiniCore code path, so no
# subtraction happens there and the INI override reaches the reported ceiling directly.
#
# `board_bootloader.size = 0` in platformio.ini does not work. PlatformIO's manifest
# override loop only int-coerces an override when the target key ALREADY EXISTS in the
# board manifest. `bootloader.size` is absent from `ATmega328PB.json`, so the override
# value is left as the Python string "0", and the subtraction above then raises
# `TypeError: unsupported operand type(s) for -=: 'int' and 'str'`. A `pre:` extra_script
# is the only mechanism that lands an actual `int`.
#
# MECHANISM. This script runs as a `pre:` SCons hook, which executes before the
# Arduino framework builder performs the subtraction above. Calling
# `env.BoardConfig().update("bootloader.size", 0)` here inserts a real `int` 0 into the
# board manifest's `bootloader` section (not a string), so `board.get("bootloader.size",
# ...)` returns 0 and the subtraction becomes a no-op: 32768 - 0 = 32768.
#
# The 384 B urclock bootloader region is no longer protected by the linker. The
# post-build hook bootloader_guard.py checks the image against that region instead.
#
# SCOPE. Wired at [env] scope (`extra_scripts`) so it runs once per env, including
# [env:native], which has no BOARD at all. The body below is therefore guarded by an
# `if` block (not `Return()`) so the guard is visible as a provable no-op on every
# non-uno328pb env rather than an early-exit that must be trusted by inspection.

Import("env")

if env["PIOENV"] == "uno328pb":
    env.BoardConfig().update("bootloader.size", 0)
