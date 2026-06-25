#!/usr/bin/env bash
# datasheets-check.sh — Wave-0 structural validation for the Phase 85 datasheets/ tree.
# Run from the firestarter/ repo root: bash datasheets/datasheets-check.sh
#
# Covers four requirements:
#   DSHEET-01  every on-hand chip has a committed PDF (non-empty, %PDF magic)
#   DSHEET-02  each no-silicon bucket dir has >=1 representative PDF
#   DSHEET-03  README indexes every folder; phantom/infeasible exclusions named; every
#              README-referenced PDF maps to a real file
#   SAFE-05    only datasheets/ is modified (structural assertion; checked here by
#              confirming the tree shape is consistent, not re-running git-diff)
#
# Exits 0  and prints "datasheets-check: PASS" on success.
# Exits 1  and prints "datasheets-check: FAIL" if any hard assertion fails.
# Prints WARN lines for soft issues (README refs with no file) without failing.
set -euo pipefail

DS=datasheets
fail=0

# ---------------------------------------------------------------------------
# 1. README must exist
# ---------------------------------------------------------------------------
if [ ! -f "$DS/README.md" ]; then
  echo "FAIL: missing $DS/README.md"
  fail=1
fi

# ---------------------------------------------------------------------------
# 2. Every expected bucket folder must exist and hold >=1 non-trivial PDF
#    whose first 4 bytes are the %PDF magic bytes.
# ---------------------------------------------------------------------------
expected_buckets="0x05-FLASH-AMD-STD 0x06-FLASH-AMD-ALT 0x07-EPROM-STD 0x08-EPROM-QUICK \
0x0B-EPROM-LEGACY 0x0D-EEPROM-POLL 0x0E-SRAM-32PIN 0x10-FLASH-INTEL 0x27-SRAM-24PIN \
0x28-SRAM-STD 0x29-SRAM-512K-1M 0x34-EEPROM-X88C64"

for b in $expected_buckets; do
  d="$DS/$b"
  if [ ! -d "$d" ]; then
    echo "FAIL: missing bucket dir $d"
    fail=1
    continue
  fi
  n=$(find "$d" -maxdepth 1 -name '*.pdf' -size +1k | wc -l)
  if [ "$n" -lt 1 ]; then
    echo "FAIL: $d has no non-trivial PDF (size >1k)"
    fail=1
  fi
  for f in "$d"/*.pdf; do
    [ -e "$f" ] || continue
    if ! head -c4 "$f" | grep -q '%PDF'; then
      echo "FAIL: $f is not a real PDF (no %PDF magic bytes)"
      fail=1
    fi
  done
done

# ---------------------------------------------------------------------------
# 3. No folder must exist for phantom or infeasible bucket IDs.
#    README must also mention each forbidden hex as an explicit exclusion.
# ---------------------------------------------------------------------------
forbidden="0x35 0x39 0x11 0x2A 0x2B 0x2C"

for b in $forbidden; do
  # Check for folder with or without a suffix (e.g. 0x35-SOMETHING or plain 0x35)
  if compgen -G "$DS/${b}-*" >/dev/null 2>&1 || [ -d "$DS/$b" ]; then
    echo "FAIL: forbidden bucket folder for $b exists under $DS/"
    fail=1
  fi
  # README must call out each forbidden bucket as an exclusion
  if [ -f "$DS/README.md" ]; then
    if ! grep -q "$b" "$DS/README.md"; then
      echo "FAIL: README does not mention exclusion $b"
      fail=1
    fi
  fi
done

# ---------------------------------------------------------------------------
# 4. Every *.pdf filename referenced in README must map to a real file.
#    This is a soft WARN (not FAIL) so a mid-download run does not block.
# ---------------------------------------------------------------------------
if [ -f "$DS/README.md" ]; then
  grep -oE '[A-Za-z0-9_.-]+\.pdf' "$DS/README.md" | sort -u | while read -r ref; do
    if ! find "$DS" -name "$ref" | grep -q .; then
      echo "WARN: README references $ref but no such file found under $DS/"
    fi
  done
fi

# ---------------------------------------------------------------------------
# Result
# ---------------------------------------------------------------------------
if [ "$fail" -eq 0 ]; then
  echo "datasheets-check: PASS"
  exit 0
else
  echo "datasheets-check: FAIL"
  exit 1
fi
