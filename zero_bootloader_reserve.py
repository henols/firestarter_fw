# zero_bootloader_reserve.py — PlatformIO pre-build SCons hook (extra_scripts = pre:)
#
# Quick task 260820-a7w: make the flash-limit guards report the ATmega328PB's real
# 32768 B flash size on `uno328pb` too, in lockstep with the plain
# `board_upload.maximum_size = 32768` override on `uno` and `leonardo`.
#
# WHY THIS SCRIPT EXISTS (a plain INI override is NOT enough on this one env).
# `boards/ATmega328PB.json` sets `build.core = "MiniCore"`. The atmelavr platform's
# builder (`~/.platformio/platforms/atmelavr/builder/frameworks/arduino.py:148`) has a
# MiniCore-only code path that runs AFTER build_flags/board overrides are applied:
#
#     upload_section["maximum_size"] -= board.get("bootloader.size", get_bootloader_size())
#
# `ATmega328PB.json` has a `bootloader` section but no `bootloader.size` key, so
# `get_bootloader_size()` runs: for `upload_protocol == "urclock"` with `max_size >= 4096`,
# it returns 384. Hence 32768 - 384 = 32384, the exact figure this project recorded before
# this quick task -- and a bare `board_upload.maximum_size = 32768` in platformio.ini
# changes nothing, because this subtraction runs on top of it. `uno` and `leonardo` use
# `build.core = "arduino"`, which is NOT in the MiniCore code path, so no subtraction
# happens there and the INI override reaches the reported ceiling directly.
#
# REJECTED ROUTE (do not re-attempt): `board_bootloader.size = 0` in platformio.ini.
# PlatformIO's manifest-override loop (`pioplatform.py:103-114`) only int-coerces an
# override when the target key ALREADY EXISTS in the board manifest. `bootloader.size` is
# absent from `ATmega328PB.json`, so the override value is left as the Python string "0",
# and `arduino.py:148`'s subtraction then raises
# `TypeError: unsupported operand type(s) for -=: 'int' and 'str'` -- observed directly
# during planning. A `pre:` extra_script is the only mechanism that lands an actual `int`.
#
# MECHANISM. This script runs as a `pre:` SCons hook, which executes before
# `BuildFrameworks` loads `arduino.py` and performs the subtraction above. Calling
# `env.BoardConfig().update("bootloader.size", 0)` here inserts a real `int` 0 into the
# board manifest's `bootloader` section (not a string), so `board.get("bootloader.size",
# ...)` returns 0 and the subtraction becomes a no-op: 32768 - 0 = 32768.
#
# Forfeits: 384 B (the urclock bootloader this MiniCore build.core assumes is present).
# The linker no longer protects that region -- operator-accepted, see platformio.ini's
# own override-site comment on [env:uno328pb] and scripts/baseline/size_baseline.json's
# meta note for the full trade record.
#
# SCOPE. Wired at [env] scope (`extra_scripts`) so it runs once per env, including
# [env:native], which has no BOARD at all. The body below is therefore guarded by an
# `if` block (not `Return()`) so the guard is visible as a provable no-op on every
# non-uno328pb env rather than an early-exit that must be trusted by inspection.

Import("env")

if env["PIOENV"] == "uno328pb":
    env.BoardConfig().update("bootloader.size", 0)
