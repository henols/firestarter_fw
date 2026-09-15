"""PlatformIO post-build hook: refuse a firmware image that would overwrite the bootloader.

Each AVR environment sets ``board_upload.maximum_size = 32768`` to expose the full
device flash, which removes the linker's protection of the bootloader region. This
hook restores that protection as an explicit, named build failure.

The reported ceiling and the safe ceiling are different numbers. A build at 75% of
the reported ceiling can already be past the safe one, and without this hook nothing
says so: the image links, flashes, and destroys the board's bootloader entry.
"""

Import("env")

BOOTLOADER_BYTES = {
    "uno": 512,
    "uno328pb": 384,
    "leonardo": 4096,
}

DEVICE_FLASH_BYTES = 32768


def _image_size(target):
    sections = (".text", ".data")
    size_tool = env.subst("$SIZETOOL")
    if not size_tool:
        return None
    import subprocess

    try:
        out = subprocess.run(
            [size_tool, "-A", str(target)],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    total = 0
    seen = False
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] in sections:
            try:
                total += int(parts[1])
                seen = True
            except ValueError:
                pass
    return total if seen else None


def check_bootloader_margin(source, target, env):
    env_name = env["PIOENV"]
    reserved = BOOTLOADER_BYTES.get(env_name)
    if reserved is None:
        return

    used = _image_size(target[0])
    if used is None:
        print(
            f"bootloader-guard: WARNING -- could not measure the {env_name} image; "
            "the bootloader margin was NOT checked."
        )
        return

    safe_ceiling = DEVICE_FLASH_BYTES - reserved
    margin = safe_ceiling - used
    percent = 100.0 * used / safe_ceiling

    if used > safe_ceiling:
        raise SystemExit(
            f"\nbootloader-guard: BUILD REFUSED for {env_name}.\n"
            f"  image        {used} B\n"
            f"  safe ceiling {safe_ceiling} B "
            f"({DEVICE_FLASH_BYTES} device flash - {reserved} bootloader)\n"
            f"  over by      {used - safe_ceiling} B\n\n"
            f"Flashing this image would overwrite the bootloader and the board would\n"
            f"lose USB/serial bootloader entry. The {DEVICE_FLASH_BYTES} B figure the\n"
            f"size report shows is the raw device flash, not a safe target.\n"
        )

    print(
        f"bootloader-guard: {env_name} {used}/{safe_ceiling} B "
        f"({percent:.1f}% of the safe ceiling, {margin} B margin, "
        f"{reserved} B bootloader reserved)"
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_bootloader_margin)
