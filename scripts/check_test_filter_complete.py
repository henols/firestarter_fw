#!/usr/bin/env python3
"""scripts/check_test_filter_complete.py -- native suite registration gate.

Every directory `test/native/avr/test_<name>/` that holds a `.cpp` file must be
listed in the `test_filter` of `platformio.ini`, and every listed entry must
name a directory that exists. PlatformIO runs only the filtered suites, so a
suite that is missing from the filter is never built or run, and nothing says so.

Usage:
    python3 scripts/check_test_filter_complete.py [--root DIR]

`--root` defaults to the repository root. It exists so the paired pytest can
point the checker at a fixture tree.

Exit status: 0 when the set of suite directories equals the set of filter
entries. 1 when they differ; the output names each unregistered suite and each
dangling entry. 2 when `platformio.ini` or the suite directory is missing, or
when the filter has no entries.
"""

import argparse
import re
import sys
from pathlib import Path

SUITE_PARENT = Path("test") / "native" / "avr"
FILTER_PREFIX = "native/avr/"


def read_filter_entries(ini_text):
    """Return the set of `test_filter` entries, one per continuation line."""
    entries = set()
    in_filter = False
    for line in ini_text.splitlines():
        if re.match(r"^\s*test_filter\s*=", line):
            in_filter = True
            rest = line.split("=", 1)[1].strip()
            if rest:
                entries.add(rest)
            continue
        if in_filter:
            if line[:1] in (" ", "\t") and line.strip() and not line.strip().startswith(";"):
                entries.add(line.strip())
            elif line.strip() == "" or line.strip().startswith(";"):
                continue
            else:
                in_filter = False
    return entries


def read_suite_dirs(root):
    base = root / SUITE_PARENT
    suites = set()
    for child in sorted(base.iterdir()):
        if child.is_dir() and child.name.startswith("test_") and any(child.glob("*.cpp")):
            suites.add(FILTER_PREFIX + child.name)
    return suites


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    args = parser.parse_args(argv)
    root = Path(args.root)

    ini = root / "platformio.ini"
    if not ini.is_file() or not (root / SUITE_PARENT).is_dir():
        print(f"ERROR: {ini} or {root / SUITE_PARENT} not found", file=sys.stderr)
        return 2
    entries = read_filter_entries(ini.read_text())
    if not entries:
        print("ERROR: platformio.ini has no test_filter entries", file=sys.stderr)
        return 2
    suites = read_suite_dirs(root)

    unregistered = sorted(suites - entries)
    dangling = sorted(entries - suites)
    for name in unregistered:
        print(f"FAIL: {name} is not in test_filter, so PlatformIO never runs it")
    for name in dangling:
        print(f"FAIL: test_filter lists {name}, which has no suite directory")
    if unregistered or dangling:
        return 1
    print(f"PASS: {len(suites)} native suites, all listed in test_filter")
    return 0


if __name__ == "__main__":
    sys.exit(main())
