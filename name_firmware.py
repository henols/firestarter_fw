# name_firmware.py — PlatformIO pre-build SCons hook (extra_scripts = pre:)
#
# This script is wired at [env] scope via `extra_scripts = pre:name_firmware.py`
# at firestarter/platformio.ini line 29, so it runs once per env (including
# [env:native]). Every env MUST declare `-D RURP_BOARD_NAME=\"X\"` in its
# build_flags; the script extracts X and sets PROGNAME = "firestarter_X".
#
# PROGNAME decouples from PIO's `board` setting and derives from the
# RURP_BOARD_NAME build_flag instead, so the board-id triple (build_flag value =
# artifact filename = handshake `<board>` slot) has a single source of truth.

import re

Import("env")


def _extract_board_name():
    """Parse -D RURP_BOARD_NAME=\\"X\\" from this env's build_flags.

    Returns the bare board-name string. Exits with a clear UserError if the
    flag is missing or malformed.
    """
    flags = env.GetProjectOption("build_flags")
    if not flags:
        print("ERROR: name_firmware.py — env '%s' has no build_flags; "
              "-D RURP_BOARD_NAME=\\\"X\\\" is required" % env["PIOENV"])
        env.Exit(1)

    parsed = env.ParseFlags(flags)
    for define in parsed.get("CPPDEFINES", []):
        if isinstance(define, (list, tuple)) and len(define) == 2:
            name, value = define
            if name == "RURP_BOARD_NAME":
                # PIO surfaces -D NAME=\"value\" with the literal escaped quotes
                # preserved in the value string. Strip them; allow either escaped
                # or plain quote conventions for robustness (Pitfall 2 — order
                # matters: try escaped first, then plain).
                v = str(value).strip()
                for quote in ('\\"', '"'):
                    if v.startswith(quote) and v.endswith(quote) and len(v) >= 2 * len(quote):
                        v = v[len(quote):-len(quote)]
                        break
                # Defensive validation per RESEARCH Security Domain V5: PROGNAME
                # flows into a filename, so the value must be a safe identifier.
                if not re.match(r"^[a-zA-Z0-9_-]+$", v):
                    print("ERROR: name_firmware.py — RURP_BOARD_NAME value '%s' "
                          "is not a valid identifier (must match [a-zA-Z0-9_-]+)" % v)
                    env.Exit(1)
                return v

    print("ERROR: name_firmware.py — no -D RURP_BOARD_NAME=\\\"X\\\" found in "
          "build_flags for env '%s'" % env["PIOENV"])
    env.Exit(1)


board_name = _extract_board_name()
env.Replace(PROGNAME="firestarter_%s" % board_name)
