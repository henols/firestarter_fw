# `tests/fixtures/` — naming discipline

This directory holds committed evidence used by Phase 123's baseline JSON and its downstream
checkers/tests (123-02 … 123-06). Every fixture in this phase — in this directory and in the
sibling `firestarter_app/tests/fixtures/` and `.planning/phases/123-…/fixtures/` directories —
follows the same three-prefix naming discipline:

- **`captured_*`** — verbatim tool output, committed **unedited**. No trimming beyond an explicitly
  recorded contiguous window, no reflow, no ANSI stripping, no hand-edited number. If a capture is
  truncated (for example, a `pio test` tail block rather than the full run), the truncation point is
  a stated fact in the producing plan's SUMMARY, never an invisible edit.
- **`planted_*`** — a deliberate violation, each derived from a named `captured_` (or otherwise real)
  file by a single stated edit, used to prove a checker fails closed rather than passing vacuously.
- **`clean_*`** — a control that must pass a checker cleanly, proving the checker does not fire on
  legitimate input.

## Why `firestarter/tests/` cannot reach a real build

`firestarter/tests/` is invisible to PlatformIO. PlatformIO's unit-test discovery globs `test/`
(the real native suites live at `test/native/avr/…`), and the default `[env]` source filter for the
two native environments is explicitly
`+<proms/> +<boards/rurp_serial_utils.cpp> +<json_parser.c> +<operation_utils.cpp>` — no path under
`tests/` is named anywhere in `platformio.ini`. A `.cpp` fixture placed here therefore cannot reach
any build, `pio run` or `pio test` alike, and `platformio.ini` stays completely untouched by this
phase.

## Verifying fixture presence

Fixture presence in this directory is verified with `git ls-files`, **never** with `git add`'s exit
code. Git refuses to stage any path containing a `.git` path component (file or directory) and does
so **silently at exit 0** — `git add` reports success while staging nothing. `git ls-files` is the
only check that reflects what is actually tracked in the index. Every capture in this directory is
independently checked with `git ls-files tests/fixtures/` rather than trusted from a shell's own exit
code.

## Per-file inventory is mechanical, not prose

The per-file inventory (which `captured_/planted_/clean_` file exists, and which checker/pytest each
one backs) is enforced mechanically by `tests/test_checker_convention.py` (Phase 123 Plan 06), not
duplicated here as a hand-maintained list. Prose here would drift the moment a new checker or fixture
is added; the convention test cannot.

## Files in this directory (as of Plan 01)

| File | Prefix | Backs |
|------|--------|-------|
| `captured_build_uno.log` | `captured_` | BASE-01 comparator (`check_size_baseline.py`), BASE-06 warning gate (AVR zero rule) |
| `captured_build_uno328pb.log` | `captured_` | same |
| `captured_build_leonardo.log` | `captured_` | same |
| `captured_test_native_summary.log` | `captured_` | BASE-01 comparator native-env parsing |
| `captured_test_native_nodevtools_summary.log` | `captured_` | same, `native_nodevtools` env |
| `captured_native_warnings_excerpt.log` | `captured_` | BASE-06 warning gate — proves the parser survives real `pio test` framing around a genuine macro-redefinition diagnostic, not just a bare compiler invocation |
| `merge05_base01_anchor_uno.log` | (none) | `--policy merge05` band comparator ONLY |
| `merge05_base01_anchor_uno328pb.log` | (none) | same |
| `merge05_base01_anchor_leonardo.log` | (none) | same |

The `merge05_base01_anchor_*.log` trio was split out of the `captured_build_*.log` trio by
debug session `w27c512-program-fail-byte0`, and is frozen at BASE-01's own anchor figures
(uno 24824, uno328pb 24874, leonardo 26906). The two roles the `captured_` trio used to serve
at once became contradictory when that session's fix added 96 B of flash to every AVR target:
`captured_build_*.log` must track the LIVE tree (five default-mode legs feed them straight to
`scripts/baseline/size_baseline.json`), while the band comparator's PASS leg must be fed
inputs that sit at the band's anchor. Feeding the live logs to both would have silently turned
the band leg into a false claim that the current tree is inside MERGE-05's original band — it
is not, and the live tree's +96 B is instead admitted explicitly, under a named
defect-fix exemption, by `test_policy_merge05_admits_the_documented_defect_fix` (the v1.31
Phase 145 adjudication; it replaced `test_policy_merge05_fires_on_the_current_tree`, which
asserted the un-adjudicated breach). The split still stands: this trio holds BASE-01's anchor
figures, so a future re-anchor or exemption change cannot silently move the PASS leg's
inputs underneath it. These three
carry no `captured_`/`planted_`/`clean_` prefix on purpose: they are neither a raw capture of
the current tree nor a deliberate violation, but a capture frozen at a past anchor point, and
`tests/test_checker_convention.py` does not require a prefix of a file it does not enumerate.

## `_fullflash` fixture families (quick task 260820-a7w)

Quick task 260820-a7w made both flash-limit guards report the AVR MCUs' real 32768 B
flash size (uno 32256->32768, uno328pb 32384->32768, leonardo 28672->32768), moving
`flash_total` in BOTH recorded baselines (`scripts/baseline/size_baseline.json` AND,
by operator ruling, `scripts/baseline/size_baseline_base01.json`). Every AVR fixture
log embeds its ceiling verbatim in its `Flash:` report line, so this stranded nine
`tests/test_check_size_baseline.py` legs across both modes (default and
`--policy merge05`) whose fixtures still carried the old totals. Followed this
directory's established remedy -- sever onto new fixture families -- rather than
editing shared fixtures in place:

- **`captured_build_fullflash_{uno,uno328pb,leonardo}.log`** (`captured_`) --
  byte-for-byte copies of the cold-rebuild logs committed at
  `.planning/quick/260820-a7w-make-the-flash-limit-guards-to-be-the-ac/
  260820-a7w-cold-{uno,uno328pb,leonardo}.log`. Retires
  `captured_build_v132_{uno,uno328pb,leonardo}.log` for the default-mode legs.
- **`planted_size_baseline_flash_regression_fullflash.log`** (`planted_`) -- the
  usual +512 B Leonardo `used`-figure plant, derived from
  `captured_build_fullflash_leonardo.log`. Retires
  `planted_size_baseline_flash_regression_v132.log`.
- **`merge05_base01_anchor_fullflash_{uno,uno328pb,leonardo}.log`** -- BASE-01's own
  anchor figures (24824/24874/26906), with ONLY the `Flash:` line's total changed to
  32768 (percentage recomputed for readability; `used` and the `RAM:` line
  untouched). Retires `merge05_base01_anchor_{uno,uno328pb,leonardo}.log`.
- **`merge05_defect_fix_fullflash_{uno,uno328pb,leonardo}.log`** -- derived from
  `captured_build_{uno,uno328pb,leonardo}.log` (used 24920/24970/27002) the same way,
  total-only. Given a PURPOSE name rather than inheriting `captured_build_*`: after
  this severance the family is read by exactly one leg (the merge05 defect-fix
  admission arm), and the old name meant "a captured default-mode log", which this
  family no longer is.
- **`planted_size_baseline_policy_{uno_over_band,leonardo_growth,ram_moved}_
  fullflash.log`** -- the three `--policy merge05` negative-control plants, each with
  ONLY the total changed to 32768; every planted `used`/RAM figure and its
  one-byte-past-the-allowance role are unchanged, because BASE-01's growth anchors
  never moved.

**Retired, read by no leg after this severance -- KEPT, not deleted.** Decision:
leave `captured_build_v132_{uno,uno328pb,leonardo}.log` and its planted sibling
`planted_size_baseline_flash_regression_v132.log`, the pre-149
`captured_build_{uno,uno328pb,leonardo}.log` trio, `merge05_base01_anchor_{uno,
uno328pb,leonardo}.log`, and the three pre-`_fullflash` `planted_size_baseline_
policy_*.log` fixtures in this directory rather than deleting them. Reason: each is
a byte-for-byte, previously-committed measurement record (Phase 144/145/149's cold
builds and the corresponding hand-derived plants); this directory's own convention
already excludes any file the mechanical inventory (`tests/test_checker_convention.py`
where applicable, or a plain `grep` here) does not name from being treated as live,
so keeping them costs nothing and preserves a legible history of the pre-260820-a7w
ceilings without needing a git-history dig. A future severance should apply the same
reasoning rather than re-litigating it.

**Not this task's doing.** `planted_size_baseline_flash_regression.log` (the pre-v132
sibling of the fixture retired above) was ALREADY orphaned before quick task
260820-a7w -- no leg referenced it even at the commit immediately preceding this
task. Recorded here so it is not mistaken for a casualty of this severance.

## `_v151` fixture family (Plan 151-10, LOCK-02)

Plan 151-10 measured Phase 151's own firmware growth cold: `dev lock-status`
(Plan 151-08's firmware read) cost +288 B of flash, uniform on all three AVR targets,
and +0 B of RAM, against the pre-151 live baseline. `scripts/baseline/size_baseline.json`
was re-recorded to the new cold figures (uno 25418/1575, uno328pb 25468/1581, leonardo
27500/2016), which the `_fullflash` family (still carrying the pre-151 figures) no
longer matches -- eight `tests/test_check_size_baseline.py` legs would have gone RED or
falsely green if left pointed at it. Followed the same established remedy as quick task
260820-a7w -- sever onto a new fixture family rather than editing shared fixtures in
place:

- **`captured_build_v151_{uno,uno328pb,leonardo}.log`** (`captured_`) -- byte-for-byte
  cold `rm -rf .pio/build/<env>` + single `pio run -e <env>` captures per env
  (`.planning/phases/151-protection-readability-lock-status/151-SIZE-TRANSCRIPTS.md`).
  Retires `captured_build_fullflash_{uno,uno328pb,leonardo}.log` for the default-mode
  legs.
- **`merge05_base01_anchor_v151_{uno,uno328pb,leonardo}.log`** -- BASE-01's own anchor
  figures (24824/24874/26906, RAM 1573/1579/2014), everything else left as captured.
  Retires `merge05_base01_anchor_fullflash_{uno,uno328pb,leonardo}.log`.
- **`merge05_lock_status_v151_{uno,uno328pb,leonardo}.log`** -- the new exemption's own
  admission proof: BASE-01 + 96 + 210 + 288 = the cold post-151 tree exactly, so
  numerically identical to `captured_build_v151_*.log` but read against BASE-01 under
  `--policy merge05`, at zero headroom on leonardo. New purpose-named family; no prior
  fixture retired by this one.
- **`planted_size_baseline_policy_leonardo_growth_v151.log`** -- leonardo's `used`
  raised to 27501 (+595 B, one byte past the new 594 B allowance). Retires
  `planted_size_baseline_policy_leonardo_growth_fullflash.log`.
- **`planted_size_baseline_policy_uno_over_band_v151.log`** -- uno's `used` raised to
  25483 (+659 B, one byte past the new 658 B allowance). Retires
  `planted_size_baseline_policy_uno_over_band_fullflash.log`.
- **`planted_size_baseline_policy_ram_moved_v151.log`** -- uno's RAM `used` raised to
  1576 (+3 B, one byte past the unmoved 2 B RAM tolerance -- Plan 151-10 added NO
  second RAM exemption, so this figure is unchanged from its `_fullflash` predecessor;
  moved purely for family-consistency). Retires
  `planted_size_baseline_policy_ram_moved_fullflash.log`.
- **`planted_size_baseline_flash_regression_v151.log`** -- the usual +512 B Leonardo
  `used`-figure plant, derived from `captured_build_v151_leonardo.log` (27500 + 512 =
  28012). Retires `planted_size_baseline_flash_regression_fullflash.log`.

`captured_test_native_summary.log` and `captured_test_native_nodevtools_summary.log`
were updated IN PLACE, 151 -> 163 cases/succeeded (suites unchanged at 17) -- no
severance needed, following the same in-place precedent Phase 149 Plan 07 used, since
`test_clean_native_both_envs_pass` is the only leg reading either fixture at test time.

**Retired, read by no leg after this severance -- KEPT, not deleted.** Decision:
leave `captured_build_fullflash_{uno,uno328pb,leonardo}.log` and its planted sibling
`planted_size_baseline_flash_regression_fullflash.log`, `merge05_base01_anchor_
fullflash_{uno,uno328pb,leonardo}.log`, `merge05_defect_fix_fullflash_{uno,uno328pb,
leonardo}.log` (still read by Arm 1 of `test_policy_merge05_admits_the_documented_
defect_fix` -- NOT retired), and `planted_size_baseline_policy_{uno_over_band,
leonardo_growth,ram_moved}_fullflash.log` in this directory rather than deleting them.
Same reasoning as the a7w severance above: each is a byte-for-byte, previously-committed
measurement record; this directory's own convention already excludes any file the
mechanical inventory does not name from being treated as live, so keeping them costs
nothing and preserves a legible history of the pre-151 ceilings without needing a
git-history dig. Every family the a7w severance itself retired is unaffected by this
plan and remains exactly as it was left.

## Release-asset fixture trees (Phase 128 Plan 01)

Three new `pio_build/`-rooted directory-tree fixtures back
`check_release_assets.py` (REL-03/REL-02, D-11/D-12): `clean_release_assets_all_three/`
(`clean_`, the control), `planted_release_assets_missing_uno328pb/` and
`planted_release_assets_zero_byte_leonardo/` (`planted_`). Their build root is
named `pio_build/`, not `.pio/`, because `.gitignore` line 1 is the bare
pattern `.pio`, which matches at any depth and would make `git add` silently
stage nothing for a dotted directory name — the checker reaches these trees
through the `FIRESTARTER_PIO_BUILD_ROOT` seam instead.

**Known, recorded gap (D-14):** `pio test` output never contains a literal `Compiling .pio/build/...`
progress line — that framing is specific to `pio run` (see `captured_build_uno.log` for an example).
`captured_native_warnings_excerpt.log` instead carries `pio test`'s own real framing for that command
(`Processing <suite> in native environment`, `Building...`, and the compiler's own `In file included
from ...` chain) immediately surrounding the warning — this is pio's genuine wrapping for this
command, verified across default, `-v` and `-vvv`-on-a-clean-rebuild invocations, none of which ever
emit the word `Compiling` during `pio test`. Recorded here, and in `123-01-SUMMARY.md`, so a later
reader does not mistake the absence for an incomplete capture.
