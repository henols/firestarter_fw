"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 123 Plan 02 — the BASE-08 anti-hollow pairing for scripts/check_size_baseline.py.

Requirements: BASE-01, BASE-08
Decisions covered: D-01, D-02, D-03, D-04, D-13

This is the MANDATORY anti-hollow pairing for the BASE-01 comparator: a checker with
no negative-fixture test is exactly this project's v1.12 hollow-GATE-03 failure mode --
a gate that could never fail because nothing concrete was asserted against it. Every
planted-violation test below invokes scripts/check_size_baseline.py as a real subprocess
(list-form argv, never shell=True, never an in-process import) against a committed
fixture under tests/fixtures/, so a passing suite here proves the checker itself -- not
this test module -- fails the build on a real violation.

No `pio` invocation happens anywhere in this module: every log parsed is a committed
fixture (captured_* for the clean-control arms, planted_size_baseline_* for one
deliberate violation per exit-taxonomy arm). Paying a cold-toolchain `pio` cost inside
pytest would make this suite non-hermetic and slow.

Coverage:
  1. Clean AVR control — each of the three captured_build_v132_*.log files exits 0
     against the LIVE default baseline and its PASS: line names the env. SEVERED from
     captured_build_*.log by Phase 149 Plan 07 (orchestrator-directed) -- see that
     test's own docstring for the full reasoning.
  2. Clean native control — both captured_test_native*.log files exit 0 with 151 and 17
     in the PASS: line (Phase 149 Plan 07 updated these two fixtures IN PLACE, 141 -> 151,
     since nothing else in this module depends on them staying frozen).
  3. Planted flash regression exits non-zero, prints FAIL:, and the output names both
     the baseline figure (27212, the page-size-seam figure D-14 re-anchored the live
     default to) and the observed figure (27724). SEVERED onto
     planted_size_baseline_flash_regression_v132.log by Phase 149 Plan 07
     (orchestrator-directed) -- see that test's own docstring for the full reasoning.
  4. Planted unparseable log exits exactly 2 (the literal return code, not just
     non-zero) and does NOT print PASS:.
  5. Planted errored-suites log exits non-zero naming ERRORED — proving the gate
     asserts per-suite statuses, not merely the suite count (A-4's failure mode).
  6. Never-vacuous: invoking the checker with no logs and no --rebuild exits non-zero,
     prints the never-vacuous message, and prints no PASS:.
  7. Baseline-seam precedence: pointing FIRESTARTER_SIZE_BASELINE at a temp JSON whose
     Leonardo flash figure differs makes the previously-clean captured_build_leonardo.log
     FAIL — proving the checker genuinely reads the seam rather than embedding numbers.
  8. --policy merge05 permits the re-anchored figures (EXACT ZERO delta on all three
     targets, read from the frozen merge05_base01_anchor_*.log trio) against
     BASE-01 — Phase 144 Plan 05 (D-11) re-anchored BASE-01 in place to the same v1.31
     tip, so a PASS here means the anchor moved, not that growth stayed inside v1.24's
     original band (D-14).
  8b. --policy merge05 ADMITS the current tree's +96 B against BASE-01 under the named
      defect-fix exemption (MERGE05_DEFECT_FIX_EXEMPTION_BYTES) with the +96 still
      visible in the PASS text, AND still FAILS one byte past it (planted +97 B on
      leonardo) — the adjudication and its negative control in one leg. Replaces
      test_policy_merge05_fires_on_the_current_tree, which asserted the un-adjudicated
      breach and which the adjudication was designed to turn RED.
  8c. Phase 151 (LOCK-02) extends the same leg with a THIRD arm: the fully-landed
      +594 B tree (all three flash exemptions stacked: 96 + 210 + 288) PASSES
      against BASE-01 at EXACTLY the new leonardo ceiling (zero headroom), with the
      four-term decomposition (`band0+exempt96+seam210+lock288`) visible in the
      PASS text on all three targets — the new exemption's own admission proof,
      read from merge05_lock_status_v151_*.log.
  9. --policy merge05 fires on a planted +161 B Uno-class flash growth (one byte outside
     the EFFECTIVE 160 B allowance = unchanged 64 B band + 96 B exemption), naming the
     computed delta, the allowance, and the allowance's decomposition.
  10. --policy merge05 fires on a planted +97 B Leonardo flash growth (leonardo's base
      band stays 0 B, so its effective allowance is exactly the 96 B exemption),
      naming the env and the computed delta.
  11. --policy merge05 fires on a planted +1 B RAM move (RAM equality holds under the
      band mode too), naming ram_used.
  12. The default (no --policy) mode is unchanged by the new flag: all three
      captured_build_v132_*.log logs (SEVERED from captured_build_*.log, Phase 149
      Plan 07, orchestrator-directed) still exit 0 and the output never contains the
      band-mode `<=64` substring.

Derivation of each planted fixture from its named captured_ source (single stated edit,
diffable against the source so a reviewer can see exactly what was planted):

  planted_size_baseline_flash_regression.log
    = captured_build_leonardo.log with the Flash: line's `used` figure raised from
      27002 to 27514 (+512 B, the same offset every prior version of this fixture has
      used since Phase 123, now applied to the v1.31-tip figure Phase 144 Plan 05
      re-captured -- see below). The percentage/bar-graph columns are left exactly as
      captured (now inconsistent with the new `used` figure) -- a free proof that the
      parser anchors on the `(used N bytes from M bytes)` tail and never reads the bar.

  Phase 124 Plan 10 (W-1 half (b)) re-captured captured_build_{uno,uno328pb,leonardo}.log
  and captured_test_native{,_nodevtools}_summary.log from the post-landing tree (all five
  code-bearing plans 124-01..09 applied): uno 23954/1573, uno328pb 24004/1579, leonardo
  26016/2014, both native envs still 141 cases/17 suites. planted_size_baseline_flash_
  regression.log above was re-derived from the new leonardo capture in the same commit,
  keeping the same +512 B offset. The three `policy_*` planted fixtures below were NOT
  re-derived -- they are asserted exclusively against the FROZEN
  scripts/baseline/size_baseline_base01.json (pre-landing figures), which Plan 124-10
  never modifies, so their pre-landing numbers (23932/23997, 26072/26073) remain correct
  and unchanged. The pre-landing `captured_*` fixtures this plan superseded are preserved
  in git history; their numbers are also preserved permanently in
  scripts/baseline/size_baseline_base01.json.

  Phase 144 Plan 05 (D-10, D-11, D-13) re-captured captured_build_{uno,uno328pb,leonardo}
  .log AGAIN, from the v1.31 tip (uno 24824/1573, uno328pb 24874/1579, leonardo
  26906/2014, both native envs still 141 cases/17 suites) -- three more phases (140-143)
  had landed src/ changes since Phase 124. Unlike Phase 124 Plan 10, this time the three
  `policy_*` planted fixtures below WERE re-derived, because D-11 re-anchored
  scripts/baseline/size_baseline_base01.json itself to the same v1.31 tip -- it no longer
  holds the v1.24 figures (23932/23976/26072) the paragraph above describes. Each was
  re-derived preserving its single cause and its asserted delta (+65 B / +1 B / +1 B),
  never its absolute figure, per D-18: a re-derived plant is a NEW plant and needs its
  own proof that it still fires. The pre-re-anchor `policy_*` fixtures and BASE-01's
  v1.24 content are preserved in git history, never kept in-tree (D-12).

  Phase 149 Plan 07 (D-14, orchestrator-directed severance, NOT a re-capture of
  captured_build_*.log) added a FOURTH fixture family,
  captured_build_v132_{uno,uno328pb,leonardo}.log, transcribed from the committed
  cold post-change logs (uno 25130/1575, uno328pb 25180/1581, leonardo 27212/2016,
  both native envs 151/17): D-14 re-anchored scripts/baseline/size_baseline.json's
  live default avr_targets/native_envs to these figures, which the pre-149
  captured_build_*.log trio no longer matches. Unlike every prior re-capture above,
  this one does NOT touch captured_build_{uno,uno328pb,leonardo}.log in place --
  test_baseline_seam_precedence_flips_clean_log_to_fail and
  test_policy_merge05_admits_the_documented_defect_fix's Arm 1 both still need that
  trio frozen at the pre-149 figures, so a fourth SEVERED family was added instead,
  following the exact precedent test_policy_merge05_permits_the_measured_landing_
  deltas set below when the Phase 145 debug session severed it onto
  merge05_base01_anchor_*.log for the same reason (a leg needing frozen inputs while
  the live tree keeps moving). planted_size_baseline_flash_regression_v132.log was
  derived from captured_build_v132_leonardo.log the same way its non-v132 sibling
  above was, keeping the same +512 B offset (27212 -> 27724).
  captured_test_native_summary.log and captured_test_native_nodevtools_summary.log
  were updated IN PLACE, 141 -> 151 cases/succeeded (suites unchanged at 17) --
  no severance needed there, since test_clean_native_both_envs_pass is the ONLY leg
  in this module reading either native summary fixture at test time.

  planted_size_baseline_unparseable.log
    = captured_build_uno.log with BOTH the `RAM:` and `Flash:` report lines deleted
      (lines 85-86 of the source) and everything else intact.

  planted_size_baseline_suites_errored.log
    = captured_test_native_summary.log with every per-suite status word changed from
      PASSED to ERRORED (all 17 rows, `s/PASSED/ERRORED/g`) and the
      `141 test cases: 141 succeeded` line's succeeded count changed to 0, leaving the
      total (141) and all 17 rows present -- A-4's exact failure mode: the suite count
      still reads 17, so a gate asserting only the count would incorrectly pass.

  planted_size_baseline_policy_uno_over_band.log
    = captured_build_uno.log AS IT READ AT BASE-01'S ANCHOR with the Flash: line's
      `used` figure raised from 24824 to 25195 (+371 B — one byte outside the
      EFFECTIVE uno-class allowance of 370 B: the unchanged 64 B band plus the 96 B
      defect-fix exemption plus the 210 B Phase 149 page-size-seam exemption). This
      and the two other policy-mode planted logs are compared against BASE-01, which
      no adjudication to date has moved, so they stay at the anchor figures while the
      captured_build_*.log trio sits +96 B ahead with the tree as it read before Phase
      149. Everything else, including the now-stale percentage/bar columns, is left
      exactly as captured.
      Re-derived THREE times now, each time preserving the single cause and the
      one-byte-past-the-ceiling role, never the absolute figure (D-18): by Phase 144
      Plan 05 from 23932/23997 when the anchor moved, by the v1.31 Phase 145
      adjudication from 24889 (+65 B) when the enforced ceiling moved from 64 B to
      160 B, and by Phase 149 (D-12) from 24985 (+161 B) when the ceiling moved again
      from 160 B to 370 B on landing the page-size-seam exemption. Without this third
      re-derivation this plant would sit INSIDE the new allowance and its leg would
      have gone falsely green while still claiming to prove a firing.

  planted_size_baseline_policy_leonardo_growth.log
    = captured_build_leonardo.log with the Flash: line's `used` figure raised from
      26906 to 27213 (+307 B — leonardo's base band stays 0 B must-not-grow, so its
      effective allowance is exactly the 96 B defect-fix exemption plus the 210 B
      Phase 149 page-size-seam exemption, and this is one byte past it). Left at the
      BASE-01 anchor for the reason given under the uno-class entry above. Re-derived
      by Phase 144 Plan 05 from 26072/26073, by the v1.31 Phase 145 adjudication from
      26907 (+1 B), and now by Phase 149 (D-12) from 27003 (+97 B) — same D-18
      reasoning as the uno-class entry: a +97 B plant now sits inside the new
      allowance. This single fixture backs two legs (Coverage 10 and the
      negative-control arm of test_policy_merge05_admits_the_documented_defect_fix),
      deliberately shared rather than committed twice byte-identically.

  planted_size_baseline_policy_ram_moved.log
    = captured_build_uno.log with the RAM: line's `used` figure raised from 1573 to
      1576 (+3 B — one byte past the Phase 149 page-size-seam RAM exemption of 2 B;
      before Phase 149, RAM equality was enforced exactly under the band mode too, on
      all three envs, with zero tolerance). Re-derived by Phase 144 Plan 05 from the
      same 1573/1574 pair (D-18) when only the source capture's Flash: line moved
      underneath it, and now by Phase 149 (D-12) from 1574 (+1 B) to 1576 (+3 B) --
      the RAM clause gained its own named exemption for the first time (the single
      `uint16_t page_size` handle field, measured +2 B on all three targets), so a
      +1 B plant now sits inside the new tolerance and would have gone falsely green.

Quick task 260820-a7w (make the flash-limit guards report the AVR MCUs' real 32768 B
flash size) -- TWO NEW fixture families, because BOTH recorded baselines
(scripts/baseline/size_baseline.json AND size_baseline_base01.json) moved their
flash_total on all three targets, stranding nine legs across both modes whose fixtures
still carried the old, bootloader-reduced totals:

  Family A -- default mode, REAL logs. captured_build_fullflash_{uno,uno328pb,
  leonardo}.log are byte-for-byte copies of the cold-rebuild logs this quick task
  committed at .planning/quick/260820-a7w-make-the-flash-limit-guards-to-be-the-ac/
  260820-a7w-cold-{uno,uno328pb,leonardo}.log (uno 25130/32768/1575, uno328pb
  25180/32768/1581, leonardo 27212/32768/2016 -- flash_used/ram_used unmoved from the
  v132 family they retire; only flash_total moved, for real, because the ceiling
  itself moved). planted_size_baseline_flash_regression_fullflash.log is derived from
  captured_build_fullflash_leonardo.log with the same +512 B offset every prior
  generation of this fixture has used since Phase 123 (27212 + 512 = 27724), so it
  fails on flash_used alone.

  Family B -- --policy merge05, SYNTHETIC (derived, never captured, exactly as every
  prior generation of these fixtures was). Each existing merge05 fixture had ONLY its
  Flash: line's total changed to 32768 -- `used` and the RAM: line are byte-identical
  to the source, and the percentage column was recomputed purely for readability
  (SIZE_RE never captures the percentage or the bar-graph column, so a stale
  percentage would be cosmetic, not load-bearing -- moot here since every percentage
  below WAS recomputed):
    merge05_base01_anchor_fullflash_{uno,uno328pb,leonardo}.log
      <- merge05_base01_anchor_{uno,uno328pb,leonardo}.log (used 24824/24874/26906,
         BASE-01's own anchor figures, unchanged)
    merge05_defect_fix_fullflash_{uno,uno328pb,leonardo}.log
      <- captured_build_{uno,uno328pb,leonardo}.log (used 24920/24970/27002). This
         family gets a PURPOSE name rather than inheriting `captured_build_*`: the old
         name meant "a captured default-mode log", and after this severance the
         family is read by exactly one leg, the merge05 defect-fix admission arm.
    planted_size_baseline_policy_uno_over_band_fullflash.log
      <- planted_size_baseline_policy_uno_over_band.log (used 25195, +371 vs
         BASE-01's 24824, one byte past the 370 B allowance)
    planted_size_baseline_policy_leonardo_growth_fullflash.log
      <- planted_size_baseline_policy_leonardo_growth.log (used 27213, +307 vs
         BASE-01's 26906, one byte past the 306 B allowance; shared by two legs, as
         before)
    planted_size_baseline_policy_ram_moved_fullflash.log
      <- planted_size_baseline_policy_ram_moved.log (flash used 24824 unchanged, RAM
         used 1576, +3 vs BASE-01's 1573, one byte past the 2 B RAM tolerance)

  Because BASE-01's flash_used/ram_used anchors never moved, EVERY existing delta
  assertion in these legs still holds unchanged: 25195-24824=+371 (370 B allowance),
  27213-26906=+307 (306 B allowance), 1576-1573=+3 (2 B allowance), and the
  defect-fix family is +96 on all three targets -- nothing here was re-derived, only
  re-frozen at the new flash_total.

  Nine legs repointed: test_clean_avr_all_three_envs_pass and
  test_default_mode_is_unchanged_by_the_new_flag -> the three
  captured_build_fullflash_*.log; test_planted_flash_regression_flips_checker_to_
  failure -> planted_size_baseline_flash_regression_fullflash.log;
  test_baseline_seam_precedence_flips_clean_log_to_fail -> captured_build_fullflash_
  leonardo.log (a REPAIR: this leg's prior fixture, captured_build_leonardo.log, had
  been failing against the untampered live baseline on flash_used AND ram_used since
  Phase 149 severed four legs away from it and left it stale -- the leg's tampered
  flash_used=1 plant was proving nothing about the env seam it was written to test;
  this quick task did not introduce that staleness, only repaired it);
  test_policy_merge05_permits_the_measured_landing_deltas -> the three
  merge05_base01_anchor_fullflash_*.log; test_policy_merge05_admits_the_documented_
  defect_fix -> Arm 1 to the three merge05_defect_fix_fullflash_*.log, Arm 2 to
  planted_size_baseline_policy_leonardo_growth_fullflash.log;
  test_policy_merge05_fires_on_uno_class_over_band,
  test_policy_merge05_fires_on_leonardo_growth and test_policy_merge05_fires_on_
  ram_move -> their respective `_fullflash` plants (the leonardo-growth fixture is
  shared with the defect-fix leg's Arm 2, as before). Each had begun firing, or would
  have begun firing, on `flash_total ... (board or framework moved)` in ADDITION to
  the one reason it names -- a leg that fires for two reasons no longer proves the
  one it was written for.

  test_base01_is_not_re_anchored_by_the_new_exemption was STRENGTHENED, not
  repointed (it reads BASE-01 directly, never a fixture): its docstring's claim that
  "BASE-01's avr_targets are byte-unchanged" is corrected to the two-axis split the
  operator ruled on (board identity moved, growth did not), and new assertions pin
  flash_total == 32768 on all three targets alongside the pre-existing frozen
  flash_used/ram_used pins.

  RETIRED, read by no leg after this severance (disposition: KEPT in git history,
  NOT deleted from tests/fixtures/ -- deleting them would erase a still-legible
  measurement record of the pre-260820-a7w ceilings without shrinking the test
  matrix, since `git ls-files` and this project's fixture-inventory convention
  already exclude anything a checker or test does not name; keeping them costs
  nothing and preserves the exact byte-for-byte record Phase 144/145/149 measured):
  captured_build_v132_{uno,uno328pb,leonardo}.log and its planted sibling
  planted_size_baseline_flash_regression_v132.log; the pre-149
  captured_build_{uno,uno328pb,leonardo}.log trio; merge05_base01_anchor_{uno,
  uno328pb,leonardo}.log; and the three pre-fullflash planted_size_baseline_policy_*
  fixtures. Separately, and NOT this task's doing:
  planted_size_baseline_flash_regression.log (the pre-v132 sibling of the fixture
  above) was ALREADY orphaned before this quick task -- no leg referenced it even at
  the previous commit; recorded here so it is not mistaken for a casualty of this
  severance.

Plan 151-10 (LOCK-02, D-01/D-02) -- a THIRD flash exemption, no new RAM exemption, EIGHT
legs severed onto a NEW `*_v151*` fixture family. `dev lock-status` (Phase 151's firmware
read, commits 32c32e7, f66d817, 8db7e55, 0444b1c, 3ff9f34) measured at a uniform +288 B on
all three AVR targets against the pre-151 live baseline (151-SIZE-TRANSCRIPTS.md), funded
as MERGE05_LOCK_STATUS_READ_EXEMPTION_BYTES = 288 in scripts/check_size_baseline.py. RAM
moved by exactly +0 B this phase (the pre-existing +2 B against BASE-01 is Phase 149's,
unmoved), so NO second RAM exemption was authored -- test_policy_merge05_fires_on_ram_move
is repointed onto the new family purely for family-consistency, its asserted values
unchanged.

`scripts/baseline/size_baseline.json`'s live default avr_targets/native_envs moved to the
cold post-151 figures (uno 25418/1575, uno328pb 25468/1581, leonardo 27500/2016; native and
native_nodevtools both 163 cases/17 suites, up from 151 -- Plan 151-08's five new legs in
each of test_val_nor_unlock.cpp/test_val_5v_page.cpp plus Plan 151-03's native-mirror-suite
growth), which the pre-151 `*_fullflash*` family no longer matches on flash_used/ram_used
(default mode requires EXACT identity). `*_fullflash*` itself is NOT touched -- it is
retired, not repointed, joining the a7w-retired families in keep-not-delete disposition
(tests/fixtures/README.md's `_fullflash fixture families` section and this module's own
disposition table above record the same convention). This severance instead adds a new
family, `*_v151*`, thirteen files: `captured_build_v151_{uno,uno328pb,leonardo}.log`
(byte-for-byte cold `rm -rf .pio/build/<env>` + single `pio run -e <env>` captures, the
same recipe every prior generation of this family has used); `merge05_base01_anchor_v151_
{uno,uno328pb,leonardo}.log` (SYNTHETIC, as every prior generation of this fixture is --
the `used` figures set to BASE-01's own anchor, 24824/24874/26906, RAM 1573/1579/2014,
everything else left as captured); `merge05_lock_status_v151_{uno,uno328pb,leonardo}.log`
(the new exemption's own admission proof -- BASE-01 + 96 + 210 + 288 = the cold post-151
tree exactly, so numerically identical to captured_build_v151_*.log but read against
BASE-01 under --policy merge05, at zero headroom on leonardo); and four SYNTHETIC plants,
each re-derived as allowance+1 from the merge05_base01_anchor_v151_* anchor (or from
captured_build_v151_leonardo.log for the default-mode plant), preserving each plant's
single cause and its one-byte-past-the-ceiling role, never its absolute figure (D-18):
`planted_size_baseline_policy_leonardo_growth_v151.log` (27501, +595 vs the new 594 B
leonardo allowance), `planted_size_baseline_policy_uno_over_band_v151.log` (25483, +659 vs
the new 658 B uno-class allowance), `planted_size_baseline_policy_ram_moved_v151.log`
(1576, +3 vs the unmoved 2 B RAM tolerance -- re-derived onto this family though its value
did not need to change), and `planted_size_baseline_flash_regression_v151.log` (28012, the
same +512 B offset every prior generation of this fixture has used since Phase 123, now
applied to the post-151 leonardo figure 27500).

Eight legs repointed: test_clean_avr_all_three_envs_pass and
test_default_mode_is_unchanged_by_the_new_flag -> the three captured_build_v151_*.log;
test_planted_flash_regression_flips_checker_to_failure ->
planted_size_baseline_flash_regression_v151.log; test_baseline_seam_precedence_flips_
clean_log_to_fail -> captured_build_v151_leonardo.log (repairing the same kind of drift
the a7w severance repaired: the live baseline moved out from under a `_fullflash` control
that other legs still need frozen); test_policy_merge05_fires_on_uno_class_over_band,
test_policy_merge05_fires_on_leonardo_growth and test_policy_merge05_fires_on_ram_move ->
their respective `_v151` plants; test_policy_merge05_admits_the_documented_defect_fix's
Arm 2 (negative control) -> the shared planted_size_baseline_policy_leonardo_growth_v151.log,
and that same test gains Arm 3 (Coverage 8c above) reading the three
merge05_lock_status_v151_*.log. Arm 1 of that test (merge05_defect_fix_fullflash_*.log)
and Coverage 8's merge05_base01_anchor_fullflash_*.log are UNTOUCHED: both assert at fixed,
sub-allowance deltas (+96 and +0 respectively), and any non-negative allowance -- however
many terms compose it -- still admits them, so the widened allowance changes nothing either
leg asserts.

test_base01_is_not_re_anchored_by_the_new_exemption was STRENGTHENED again, not repointed
(it reads BASE-01 and the checker source directly, never a fixture): its source-scan gained
a fourth pin, the exact string `MERGE05_LOCK_STATUS_READ_EXEMPTION_BYTES = 288`, alongside
the two it already pinned -- this is the direct, machine-checked tripwire on Plan 151-10's
own exemption, proving a new NAMED exemption is the sanctioned mechanism (leaves this leg
green) while re-anchoring BASE-01 is not (would turn it red).

RETIRED, read by no leg after this severance (disposition: KEPT in git history, NOT
deleted from tests/fixtures/, for the same reason the a7w severance gave -- deleting would
erase a still-legible measurement record without shrinking the test matrix):
captured_build_fullflash_{uno,uno328pb,leonardo}.log and its planted sibling
planted_size_baseline_flash_regression_fullflash.log, plus every family the a7w severance
already retired (unchanged by this plan).

Plan 153-15 (ERASE-08) — the fourth generation of this severance, and the reason the
phase's size gate can claim its tripwire is still armed above the widened allowance.

WHAT THE EXEMPTION FUNDS, in one sentence: the standalone software chip erase on
protocol `0x0D` (`eeprom28c_erase_execute`, dispatched via a new `CMD_ERASE` arm) plus the
two removed pre-write blank-check conditionals (`eeprom28c_write_init` and
`flash_5v_page_write_init`) that made the erase's blank-check step honest rather than
redundant.

THE MEASURED FIGURE, stated once, here, with its transcript pointer: +130 B flash on all
three AVR targets, +0 B RAM, funded as `MERGE05_ERASE_STANDALONE_EXEMPTION_BYTES = 130` in
`scripts/check_size_baseline.py` -- see
`.planning/phases/153-write-path-erase-policy/153-DECISIONS.md`'s "Post-change measured
position (cold)" section for the cold `rm -rf .pio/build/<env>` + `pio run -e <env>`
capture this figure was read from. Every fixture below is derived from
`_merge05_flash_allowance()`/`_merge05_ram_allowance()`'s own returned values, never from
this literal repeated by hand -- an absolute number copied into a docstring goes stale the
moment the allowance moves again, which is why the four-group inventory below names each
plant's single CAUSE and its one-byte-past-the-ceiling ROLE, never its figure, as a claim.

WHICH FAMILY IS RETIRED VERSUS REPOINTED: the `*_v151*` family (Plan 151-10's generation) is
retired in place and KEPT -- thirteen files, unmodified, `git status --porcelain
tests/fixtures/` shows zero touched `*_v151*` entries. It is not repointed and not deleted;
re-anchoring or repointing an existing family instead of severing onto a new one reddens
legs that assert at sub-allowance deltas, the standing lesson this module has already paid
for once (Phase 151's own severance record cites the same lesson from the `*_fullflash*`
generation before it). A NEW family, `*_v153*`, thirteen files, is added instead, in the
same four groups every prior generation used:

  GROUP 1 -- three cold captures (`captured_build_v153_{uno,uno328pb,leonardo}.log`).
  CAUSE: a fresh `rm -rf .pio/build/<env>` + single `pio run -e <env>` invocation per
  target, byte-for-byte, transcribed verbatim. ROLE: the clean-control reference every
  default-mode leg in this module now reads; each exits 0 against the live baseline.

  GROUP 2 -- three synthetic BASE-01 anchors (`merge05_base01_anchor_v153_
  {uno,uno328pb,leonardo}.log`). CAUSE: a Group 1 capture with BOTH the RAM: and Flash:
  lines' `used` figures set to BASE-01's own frozen anchor for that target (matching the
  `*_v151*` precedent's own shape, not a hand-picked subset of the two lines), everything
  else left as captured. ROLE: the zero-delta derivation source Group 4's plants are built
  from -- read by no leg directly at test time, exactly as the `*_v151*` generation's own
  anchor trio is read by no leg either.

  GROUP 3 -- three exemption-admission logs (`merge05_erase_standalone_v153_
  {uno,uno328pb,leonardo}.log`). CAUSE: none -- these are byte-for-byte copies of Group 1,
  by design, not a distinct measurement. ROLE: this exemption's own admission proof, read
  against BASE-01 in band mode as Arm 4 of
  `test_policy_merge05_admits_the_documented_defect_fix`, where leonardo sits EXACTLY at
  the new ceiling (zero headroom) and both uno-class targets sit 64 B inside their own
  ceiling. Stated explicitly here, as the plan requires: their numeric identity to Group 1
  is deliberate, not an accidental duplicate.

  GROUP 4 -- four plants, each derived from the allowance functions plus one and OBSERVED
  to fail before being trusted (`planted_size_baseline_policy_leonardo_growth_v153.log`,
  `planted_size_baseline_policy_uno_over_band_v153.log`,
  `planted_size_baseline_policy_ram_moved_v153.log`,
  `planted_size_baseline_flash_regression_v153.log`). Each carries exactly one deviation:
  a leonardo-growth plant one byte past the new leonardo flash allowance; a uno-class
  over-band plant one byte past the new uno-class flash allowance; a RAM-moved plant one
  byte past the RAM tolerance (arithmetically unchanged this generation -- the erase was
  built RAM-neutral by construction -- but re-planted onto the new family anyway so no leg
  reaches across generations); and a flash-regression plant for default mode, derived from
  the Group 1 leonardo capture with the same +512 B standing offset every prior generation
  of this fixture has used since Phase 123. All four were run through the checker in the
  mode they target and their verbatim failure output transcribed to this plan's own
  SUMMARY.md before any leg was written against them.

LEGS REPOINTED onto `*_v153*`: `test_clean_avr_all_three_envs_pass`,
`test_default_mode_is_unchanged_by_the_new_flag`,
`test_planted_flash_regression_flips_checker_to_failure`,
`test_baseline_seam_precedence_flips_clean_log_to_fail`,
`test_policy_merge05_fires_on_uno_class_over_band`,
`test_policy_merge05_fires_on_leonardo_growth`, `test_policy_merge05_fires_on_ram_move`, and
Arm 2 (the negative control) of `test_policy_merge05_admits_the_documented_defect_fix`,
which also gains a new Arm 4 reading the three `merge05_erase_standalone_v153_*.log` --
eight repointings plus one new arm, matching the count the previous generation's own
severance used.

RECONCILIATION against the observed failing-leg list: 153-14-SUMMARY.md's own hand-off
named exactly THREE red legs (Arm 2 of `test_policy_merge05_admits_the_documented_
defect_fix`, `test_policy_merge05_fires_on_uno_class_over_band`, `test_policy_merge05_
fires_on_leonardo_growth`). Running the full suite at the start of this plan showed SEVEN
red, not three -- a genuine disagreement, recorded honestly rather than silently absorbed.
The four not on the historical list: `test_clean_avr_all_three_envs_pass` and
`test_default_mode_is_unchanged_by_the_new_flag` (both still reading the retired `*_v151*`
family against a live default baseline Plan 153-14 had already moved -- an omission in that
plan's own hand-off, not a new coupling this phase introduced); `test_planted_flash_
regression_flips_checker_to_failure` (red, but for the WRONG reason -- it still asserted the
stale baseline figure 27500, which the checker's own FAIL text no longer echoed); and
`test_clean_native_both_envs_pass`, which genuinely IS a new coupling worth naming --
native case counts moved 163 -> 170 in the same Plan 153-14 revision that moved
`size_baseline.json`'s avr_targets, and that plan's hand-off never mentioned the native
side of its own change. All four are fixed by this severance, three by repointing onto the
new family and one (`test_clean_native_both_envs_pass`) by updating the native summary
fixtures in place, the established convention for that pair. No leg on the historical
three-item list failed to fire (none had gone vacuous).

LEGS DELIBERATELY LEFT UNTOUCHED, with the reason: `test_policy_merge05_permits_the_
measured_landing_deltas` (Coverage 8, reads `merge05_base01_anchor_fullflash_*.log`) and
Arm 1 of `test_policy_merge05_admits_the_documented_defect_fix` (reads `merge05_defect_
fix_fullflash_*.log`) both assert at fixed, sub-allowance deltas (+0 and +96 respectively),
so a widened allowance -- however many terms compose it -- changes nothing either leg
asserts; their FIXTURES are untouched. Arm 1's own decomposition-string ASSERTIONS are
WIDENED, not its fixture, to require the new `+erase130` term be visible in the same PASS
text -- the same "untouched fixture, widened assertion" treatment Plan 151-10 gave Arm 1
one generation earlier. Arm 3 (Plan 151-10's own admission proof, `merge05_lock_status_
v151_*.log`) receives the identical treatment for the identical reason: its fixture is KEPT,
unmodified, as a prior exemption's evidence, but its decomposition-string assertions are
widened to the current five-term allowance, since it no longer sits at zero headroom now
that this plan's own exemption widened the ceiling further (that role belongs to the new
Arm 4 instead).

THE NOT-RE-ANCHORED LEG, `test_base01_is_not_re_anchored_by_the_new_exemption`, is
STRENGTHENED again, not repointed: it reads BASE-01 and the checker's own source directly
and must NEVER read a fixture, or it could be satisfied by planting a convenient log. Its
source-scan gains a fifth pin, the exact string `MERGE05_ERASE_STANDALONE_EXEMPTION_BYTES
= 130`, plus two new checks unique to this generation: that the constant is actually
consumed inside `_merge05_flash_allowance()`'s own body (sliced from the function's `def`
line to the next), and that the constant's NAME never appears inside BASE-01's own raw JSON
text -- BASE-01 is the frozen anchor, never a place an exemption gets laundered into.

The checker itself, `check_size_baseline.py`, is invoked as a size gate by NO
`.github/` workflow in either repository -- that remains a local-run obligation this
milestone leans on. But THIS SUITE runs in CI: `build.yml` runs `pytest tests/ -v`
at `:161`, ungated by any `if:`, and that workflow's trigger (`:34`) is
`push: branches: ['**', '!beta']` -- every branch except `beta` fires it, so it runs on
this firmware milestone branch. The sibling leg in `beta-build.yml` (`:134`) covers
`beta`. Practical consequence, this phase's own central constraint: moving the live
baseline without severing the fixtures in the same commit turns this suite red in CI.

Evidence Ceiling (v1.32 PROJECT.md): the change this family guards is
software-proven and unvalidated on silicon -- no AT28C part was involved in measuring
the 130 B figure this exemption admits, and the figure says nothing about runtime
behaviour on real hardware.

Self-contained path resolution below — NOT in conftest.py (firestarter/tests/ has no
conftest.py anywhere in the repo; this is a recorded house-rule pattern decision, per
test_update_version.py's own comment, not an omission). Stdlib and pytest only.
"""

import json
import os
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent
_CHECKER = _REPO_ROOT / "scripts" / "check_size_baseline.py"
_FIXTURES = _HERE / "fixtures"
_BASELINE = _REPO_ROOT / "scripts" / "baseline" / "size_baseline.json"
_BASE01_BASELINE = _REPO_ROOT / "scripts" / "baseline" / "size_baseline_base01.json"


def _run_checker(argv=None, env_overrides=None):
    """Invoke check_size_baseline.py as a real subprocess (list argv, never shell=True).

    `env_overrides`, when given, is merged into the child's environment on top of
    the current process environment -- used by the baseline-seam-precedence test to
    set FIRESTARTER_SIZE_BASELINE without mutating this process's own environment.
    """
    env = {**os.environ, **(env_overrides or {})}
    return subprocess.run(
        [sys.executable, str(_CHECKER), *(argv or [])],
        cwd=str(_REPO_ROOT),
        capture_output=True,
        text=True,
        env=env,
    )


def test_clean_avr_all_three_envs_pass():
    """Coverage 1 — each captured_build_v185_*.log exits 0 against the LIVE
    default baseline, and its PASS: line names the env.

    SEVERED by Plan 185-01 (CLAIM-04/CLAIM-05): the live baseline was already
    +16 B stale on every target before Phase 183 began, and Phase 183 then shrank
    AVR flash on all three targets (uno/uno328pb/leonardo: -234/-238/-284 B,
    measured against 183-01's pre-deletion figures, not against this file's live
    figures) without size_baseline.json ever being re-recorded, so the prior
    captured_build_v158_*.log family no longer matches the live default baseline
    (which now reads uno 22734, uno328pb 22778, leonardo 24830) -- feeding it here
    would have made this leg permanently RED. The v158 family itself is NOT
    touched: it is retired, not repointed (frozen, proven byte-unchanged in plan
    185-02's Task 2 diff against the pre-plan base commit). This leg instead reads
    a new fixture family, captured_build_v185_{uno,uno328pb,leonardo}.log,
    committed byte-for-byte from a cold `rm -rf .pio/build/<env>` + single
    `pio run -e <env>` invocation per env, never re-derived warm and never read
    from `--rebuild`.

    RECONCILIATION: this leg's reddening was PREDICTED before it was observed --
    the exhaustive set of four legs that couple to a live baseline value move
    (this one, the native leg, the planted-regression leg, and the default-mode
    leg below) was named in the plan before Task 1 Step 4 ran the scoped suite
    against the moved JSON and the un-severed fixtures, and the observed failing
    set matched the predicted four exactly -- no fifth leg went red at this
    generation's start.
    `test_baseline_seam_precedence_flips_clean_log_to_fail` remains on
    `captured_build_v153_leonardo.log`, stale-but-green, deliberately left as
    Phase 158 left it -- so the account reads 4 red + 1 deliberately untouched,
    not 5 red."""
    for env_name, fixture in (
        ("uno", "captured_build_v185_uno.log"),
        ("uno328pb", "captured_build_v185_uno328pb.log"),
        ("leonardo", "captured_build_v185_leonardo.log"),
    ):
        result = _run_checker(["--avr-log", f"{env_name}={_FIXTURES / fixture}"])
        assert result.returncode == 0, (
            f"{env_name}: expected exit 0 on a clean captured log.\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        assert "PASS:" in result.stdout, (
            f"{env_name}: expected PASS: in stdout. Got:\n{result.stdout}"
        )
        assert env_name in result.stdout, (
            f"{env_name}: expected the env name in the PASS: line. Got:\n{result.stdout}"
        )


def test_clean_native_both_envs_pass():
    """Coverage 2 — both captured_test_native*.log files exit 0 with 179 and 16 in PASS:.

    Plan 188-06 (TOOLS-06/D-08/D-20) updated captured_test_native_summary.log and
    captured_test_native_nodevtools_summary.log IN PLACE again, 185 -> 179
    cases/succeeded and 17 -> 16 suites -- both were genuinely RE-CAPTURED
    from real `pio test -e native` / `-e native_nodevtools` runs at this plan's
    tree position, following the same in-place convention Phases 149, 151, 153,
    158 and 185 all used (D-09: these two fixtures are never severed). No
    severance needed here, unlike the AVR captured_build_*.log family: this is
    the ONLY leg in this module that consumes either native summary fixture, so
    nothing else depends on 179/16 staying frozen -- planted_size_baseline_suites_errored.log
    (Coverage 5) is its own independent, statically-planted fixture, re-derived
    from these two at this same plan's boundary rather than left stale.

    RECONCILIATION: the observed case/suite drop (185/17 -> 179/16) matches
    D-08's own deletion of the test_frame_vectors suite (6 cases, 1 suite),
    confirming no other suite moved between Phase 185 and this plan's own
    re-capture."""
    for env_name, fixture in (
        ("native", "captured_test_native_summary.log"),
        ("native_nodevtools", "captured_test_native_nodevtools_summary.log"),
    ):
        result = _run_checker(["--native-log", f"{env_name}={_FIXTURES / fixture}"])
        assert result.returncode == 0, (
            f"{env_name}: expected exit 0 on a clean captured native log.\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        assert "PASS:" in result.stdout
        assert "179" in result.stdout, f"Expected '179' in output. Got:\n{result.stdout}"
        assert "16" in result.stdout, f"Expected '16' in output. Got:\n{result.stdout}"


def test_planted_flash_regression_flips_checker_to_failure():
    """Coverage 3 — the planted +512 B Leonardo flash figure exits non-zero and names
    both the baseline (24830, the post-Phase-183 live figure) and observed (25342)
    figures -- the message must name both numbers, not merely fail on flash_total too.

    SEVERED by Plan 185-01 (CLAIM-04/CLAIM-05), for the same reason as
    test_clean_avr_all_three_envs_pass above: planted_size_baseline_flash_regression_
    v158.log is derived from captured_build_v158_leonardo.log, which still carries
    the pre-Phase-183 25098 B figure, so feeding it here after the live baseline moved
    to 24830 would make the checker fail for TWO reasons (flash_used has already
    diverged before the plant is even applied) instead of the one this leg names --
    exactly the false-green/false-cause pattern this project's own fixture-severance
    precedent exists to avoid. This leg instead reads a new plant,
    planted_size_baseline_flash_regression_v185.log, derived from
    captured_build_v185_leonardo.log with the same +512 B offset every prior version of
    this fixture has used since Phase 123 (24830 + 512 = 25342), against the
    now-current live default baseline (flash_used 24830, unaffected by the plant).
    Diffed against its own capture: exactly one changed line, the Flash: line, with
    the RAM: line and every other byte identical (verified with
    `diff | grep -c '^[<>]'` == 2). The prior v158 plant,
    planted_size_baseline_flash_regression_v158.log, is NOT touched -- it is
    retired, not repointed, and its byte-unchanged survival is proven in plan
    185-02's Task 2.

    RECONCILIATION: this leg's reddening was PREDICTED before it was observed, as
    one of the exhaustive four legs that couple to a live baseline value move; no
    false-cause surprise occurred, since the severance was planned before the
    re-record landed, not discovered after."""
    result = _run_checker(
        ["--avr-log", f"leonardo={_FIXTURES / 'planted_size_baseline_flash_regression_v185.log'}"]
    )
    assert result.returncode != 0, (
        f"expected non-zero exit on a planted flash regression.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "FAIL:" in result.stdout, f"Expected FAIL: in output. Got:\n{result.stdout}"
    assert "24830" in result.stdout, f"Expected baseline figure 24830. Got:\n{result.stdout}"
    assert "25342" in result.stdout, f"Expected observed figure 25342. Got:\n{result.stdout}"


def test_planted_unparseable_log_exits_exactly_2():
    """Coverage 4 — a build log missing both RAM:/Flash: report lines is a parse
    failure, categorically distinct from a size regression: exit code must be the
    literal 2, not merely non-zero, and PASS: must never appear."""
    result = _run_checker(
        ["--avr-log", f"uno={_FIXTURES / 'planted_size_baseline_unparseable.log'}"]
    )
    assert result.returncode == 2, (
        f"expected the literal exit code 2 (parse failure), got {result.returncode}.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    combined = result.stdout + result.stderr
    assert "RAM" in combined or "Flash" in combined, (
        f"Expected the missing RAM:/Flash: report named in the error output. "
        f"Got:\n{combined}"
    )
    assert "PASS:" not in result.stdout, (
        f"A parse failure must never print PASS:. Got:\n{result.stdout}"
    )


def test_planted_suites_errored_flips_checker_to_failure():
    """Coverage 5 — A-4's exact failure mode: 17 suites present but all ERRORED must
    exit non-zero, naming the ERRORED status -- proving the gate asserts statuses,
    not only the suite count."""
    result = _run_checker(
        ["--native-log", f"native={_FIXTURES / 'planted_size_baseline_suites_errored.log'}"]
    )
    assert result.returncode != 0, (
        f"expected non-zero exit when all 17 suites report ERRORED.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "ERRORED" in result.stdout, (
        f"Expected the failing ERRORED status named in output. Got:\n{result.stdout}"
    )


def test_never_vacuous_with_no_logs_and_no_rebuild():
    """Coverage 6 — invoking the checker with no logs and no --rebuild must exit
    non-zero, print the never-vacuous message, and print no PASS: -- a comparator
    that compares nothing must not report success."""
    result = _run_checker([])
    assert result.returncode != 0, (
        f"expected non-zero exit with zero envs supplied.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" not in result.stdout, (
        f"A vacuous run must never print PASS:. Got:\n{result.stdout}"
    )
    assert "no" in result.stdout.lower() and "compared" in result.stdout.lower(), (
        f"Expected the never-vacuous message naming 'no ... compared'. Got:\n{result.stdout}"
    )


def test_baseline_seam_precedence_flips_clean_log_to_fail(tmp_path):
    """Coverage 7 — pointing FIRESTARTER_SIZE_BASELINE at a temp JSON whose Leonardo
    flash figure differs must make a genuinely clean captured log FAIL. Proves the
    checker reads its baseline through the env seam rather than embedding the
    recorded numbers in the script itself.

    REPAIR, not an improvement introduced by the ceiling change -- stated honestly.
    This leg used to call its fixture (captured_build_leonardo.log) "previously
    clean", but it was not: since Phase 149 Plan 07 severed four legs onto the
    v132/fullflash-precedent fixture families and left captured_build_leonardo.log
    itself frozen at the pre-149 figures, that fixture had been failing against the
    untampered LIVE baseline on both flash_used (27212 vs the file's 27002) and
    ram_used (2016 vs 2014) the whole time. So the tampered flash_used=1 this test
    plants was proving nothing about the env seam specifically -- the fixture
    already failed for two unrelated reasons before the tamper was even applied.
    Quick task 260820-a7w's move of flash_total to 32768 would have added a THIRD
    unrelated failure reason on top (flash_total 28672 vs 32768) had this leg stayed
    pointed at the same stale fixture. Repointing it at
    captured_build_fullflash_leonardo.log -- genuinely clean against the live
    baseline on every figure -- restored the leg's actual premise: the ONLY failure
    this run should produce is the one this test plants.

    RE-SEVERED again by Plan 151-10: captured_build_fullflash_leonardo.log itself went
    stale the moment size_baseline.json's live avr_targets moved to the post-151
    figures (flash_used 27212 -> 27500), for the identical reason stated above. Moved
    to captured_build_v151_leonardo.log, genuinely clean against the re-recorded live
    baseline, restoring the same premise once more.

    RE-SEVERED again by Plan 153-15, for the identical reason: Plan 153-14 moved
    size_baseline.json's live avr_targets to the post-erase figures (flash_used
    27500 -> 27630), which captured_build_v151_leonardo.log no longer matches.
    Unlike most of this leg's fixture history, this leg was still GREEN at the start
    of this plan -- it only asserts a non-zero exit and the FAIL: marker, neither of
    which cares which of two stale-vs-tampered reasons produced the failure, so the
    staleness was silently riding along rather than causing a visible red. Moved to
    captured_build_v153_leonardo.log anyway, restoring the leg's actual premise: the
    ONLY failure this run should produce is the one this test plants, not an
    accidental second one."""
    real_baseline = json.loads(_BASELINE.read_text())
    real_baseline["avr_targets"]["leonardo"]["flash_used"] = 1
    tampered = tmp_path / "tampered_size_baseline.json"
    tampered.write_text(json.dumps(real_baseline))

    result = _run_checker(
        ["--avr-log", f"leonardo={_FIXTURES / 'captured_build_v153_leonardo.log'}"],
        env_overrides={"FIRESTARTER_SIZE_BASELINE": str(tampered)},
    )
    assert result.returncode != 0, (
        f"expected the checker to read the tampered baseline via the env seam and "
        f"FAIL, proving it does not embed the real numbers.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "FAIL:" in result.stdout, f"Expected FAIL: in output. Got:\n{result.stdout}"


def test_policy_merge05_permits_the_measured_landing_deltas():
    """Coverage 8 — --policy merge05 PASSES on the re-anchored figures, read against
    BASE-01 (scripts/baseline/size_baseline_base01.json), never the live default
    baseline.

    Before Phase 124 Plan 10, this test synthesized RESEARCH's *predicted* post-landing
    deltas (Leonardo -56, Uno +22, uno328pb +28, RAM unchanged) onto tmp_path copies of
    the then-still-pre-landing captured_build_*.log fixtures, because the real landing
    had not happened yet. Plan 124-10 re-captured captured_build_{uno,uno328pb,leonardo}
    .log directly from the real, now-landed tree (uno 23954/1573, uno328pb 24004/1579,
    leonardo 26016/2014) -- so this test fed those committed fixtures straight to
    the checker with no synthesis step, and the assertion was no longer a *prediction*
    but a direct measurement of the real MERGE-05 outcome.

    Phase 144 Plan 05 (D-10, D-11, D-14): the fixtures were re-captured again, from the
    real, now-landed v1.31 tree (uno 24824/1573, uno328pb 24874/1579, leonardo
    26906/2014) — and BASE-01 itself was re-anchored in place to those identical
    figures. This leg therefore now asserts EXACT IDENTITY at ZERO delta on all three
    targets, not growth staying inside a band. It reads PASS because the anchor moved
    to v1.31, not because growth stayed inside v1.24's original band.

    Debug session w27c512-program-fail-byte0 (Phase 145 Gate 2 root-cause fix)
    SEVERED this leg from captured_build_*.log and gave it its own frozen inputs,
    merge05_base01_anchor_*.log, which hold BASE-01's own anchor figures verbatim
    (uno 24824, uno328pb 24874, leonardo 26906). Reason, stated plainly: that fix
    added 96 B of flash to all three targets, and the captured_build_*.log fixtures
    have to track the LIVE tree because five other legs feed them to the default
    byte-identity mode. Continuing to feed them here would have quietly converted
    this leg from "the comparator passes at zero delta" into a false claim that the
    current tree is inside MERGE-05's original band -- it is NOT.

    v1.31 Phase 145 adjudicated that breach: the live tree's +96 B is now ADMITTED
    under a named, SHA-attributed exemption, and
    test_policy_merge05_admits_the_documented_defect_fix immediately below is the
    machine-checked record of the admission AND of the re-armed tripwire one byte
    past it (it replaced test_policy_merge05_fires_on_the_current_tree, which
    recorded the un-adjudicated breach). This leg's split from the live logs still
    earns its keep: frozen inputs mean neither a future re-anchor nor a future
    exemption change can move this leg's premise underneath it. So it keeps proving
    exactly the comparator property it was written for -- zero delta against the
    anchor passes -- and never doubles as a measurement of a tree that has moved.

    Phase 149 (D-12) added a second flash exemption
    (MERGE05_PAGE_SIZE_SEAM_EXEMPTION_BYTES, 210 B) and a RAM exemption
    (MERGE05_PAGE_SIZE_SEAM_RAM_EXEMPTION_BYTES, 2 B) alongside the existing
    defect-fix exemption -- this leg's own arithmetic did not need re-deriving:
    zero delta against the anchor sits inside ANY non-negative allowance, however
    many terms compose it, so the widened allowance changes nothing this leg
    asserts. Its frozen inputs (`merge05_base01_anchor_*.log`) were, correctly,
    untouched by that phase.

    Quick task 260820-a7w (make the flash-limit guards report the real 32768 B MCU
    size) went hard-RED here, unlike Phase 149 above: this leg asserts EXIT 0, and
    BASE-01's own flash_total moved from 32256/32384/28672 to 32768 (operator ruling
    -- a board-identity axis move, not a growth re-anchor; see
    scripts/baseline/size_baseline_base01.json's own meta note), which the frozen
    merge05_base01_anchor_*.log trio no longer matched on flash_total, even though
    flash_used still sat at exactly the anchor's figures. Re-frozen onto a new family,
    merge05_base01_anchor_fullflash_{uno,uno328pb,leonardo}.log, changing ONLY the
    Flash: line's total (24824/32256->32768, 24874/32384->32768, 26906/28672->32768)
    -- every `used` figure, the RAM: line, and the zero-delta premise this leg proves
    are all unchanged. The leg still proves exactly what it always did: a fixture at
    BASE-01's exact anchor passes at zero delta."""
    argv = ["--policy", "merge05", "--baseline", str(_BASE01_BASELINE)]
    for env, fixture in (
        ("leonardo", "merge05_base01_anchor_fullflash_leonardo.log"),
        ("uno", "merge05_base01_anchor_fullflash_uno.log"),
        ("uno328pb", "merge05_base01_anchor_fullflash_uno328pb.log"),
    ):
        argv += ["--avr-log", f"{env}={_FIXTURES / fixture}"]

    result = _run_checker(argv)
    assert result.returncode == 0, (
        f"expected --policy merge05 to permit the measured post-landing deltas "
        f"against the frozen BASE-01 record.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" in result.stdout, f"Expected PASS: in stdout. Got:\n{result.stdout}"


def test_policy_merge05_admits_the_documented_defect_fix():
    """Coverage 8b/8c/8d — the adjudication leg (v1.31 Phase 145), extended by Phase
    149 (PGSZ-04, D-12) to admit a SECOND named exemption, by Plan 151-10 (LOCK-02) to
    admit a THIRD, and now by Plan 153-15 (ERASE-08) to admit a FOURTH, without
    disturbing any predecessor. Four arms now.

    History, so nobody re-litigates this by accident. Debug session
    w27c512-program-fail-byte0 added +96 B of flash to all three AVR targets
    (eprom_internal_program_pulse plus its two VPP settle constants, commits
    eb563d2 and ebe9cb3 — a defect fix restoring behaviour the pre-v1.31 firmware
    had, not new feature surface). MERGE-05's base bands are 0 B on leonardo and
    64 B uno-class, so the tripwire fired, exactly as designed. The predecessor of
    this leg, `test_policy_merge05_fires_on_the_current_tree`, asserted that breach
    so it could not rot into a JSON prose field, and said in as many words that the
    day someone adjudicated it this leg would go RED and force the decision to be
    written down. That day was v1.31 Phase 145: the +96 B was ADMITTED as a named,
    SHA-attributed exemption (MERGE05_DEFECT_FIX_EXEMPTION_BYTES in
    scripts/check_size_baseline.py, where the full rationale lives), NOT by
    re-anchoring BASE-01 a third time, NOT by widening either band literal, and NOT
    by shrinking the fix.

    Phase 149 repeats the shape one level up: the page-size wire seam
    (PGSZ-01/PGSZ-02) added a further +210 B of flash and +2 B of RAM on all three
    targets. Rather than folding that growth into the existing 96 B constant --
    which would launder Phase 149's cost into Phase 145's already-adjudicated
    number -- it is admitted as a SECOND, separately-named exemption,
    MERGE05_PAGE_SIZE_SEAM_EXEMPTION_BYTES (flash) plus
    MERGE05_PAGE_SIZE_SEAM_RAM_EXEMPTION_BYTES (RAM, the first time the RAM clause
    has admitted anything beyond exact equality). BASE-01's avr_targets are still
    byte-unchanged (uno 24824, uno328pb 24874, leonardo 26906) and so are both
    flash band literals -- captured here, not merely stated, by
    test_base01_is_not_re_anchored_by_the_new_exemption below.

    Arm 1 — the tree as captured before Phase 149 (captured_build_*.log, still at
    +96 B flash / +0 B RAM against BASE-01) PASSES, and BOTH admitted figures are
    still VISIBLE in the PASS text with their full decomposition -- the +96 B
    inherited from Phase 145 and the +210 B / seam-RAM-tolerance headroom Phase 149
    adds alongside it. A pass whose output hid either delta would be laundering,
    not adjudication, so the visibility is asserted, not assumed.

    Arm 2 — the NEGATIVE CONTROL, and the reason arm 1 is not a blank cheque: one
    byte beyond the NEW effective allowance (a planted +307 B on leonardo, whose
    effective allowance is now 0 + 96 + 210 = 306) still exits 1. Without this arm
    the exemption would be untested and could silently widen to admit anything. The
    tripwire is re-armed at the new floor, not removed. It shares its fixture with
    test_policy_merge05_fires_on_leonardo_growth (Coverage 10) rather than
    committing a second byte-identical plant; the two legs assert different
    properties of the same firing — that one names the env and the delta, this one
    names the effective allowance and pairs the failure with arm 1's pass.

    Quick task 260820-a7w went hard-RED on Arm 1: BASE-01's flash_total moved to
    32768 (operator ruling), which captured_build_{uno,uno328pb,leonardo}.log no
    longer matches (they still carry the old bootloader-reduced totals), so feeding
    them here would add a `flash_total ... (board or framework moved)` failure to a
    leg that names its own exit code and PASS text explicitly. Arm 1 is repointed to
    a purpose-named fixture family, merge05_defect_fix_fullflash_{uno,uno328pb,
    leonardo}.log, derived from captured_build_{uno,uno328pb,leonardo}.log with ONLY
    the total changed to 32768 -- the purpose-name (rather than inheriting
    captured_build_*) marks that this family is now read only by this arm, not by
    any default-mode leg. Arm 2's fixture was already `planted_size_baseline_policy_
    leonardo_growth.log`, sharing with Coverage 10 below -- see that leg's own
    docstring update for why it too moved to the `_fullflash` family.

    Plan 151-10 (LOCK-02, D-01/D-02) repeats the shape a third time: `dev
    lock-status` (the firmware read landed in Plan 151-08, commits 32c32e7, f66d817,
    8db7e55, 0444b1c, 3ff9f34) added a further +288 B of flash, uniform on all three
    targets, and +0 B of RAM (151-SIZE-TRANSCRIPTS.md). Rather than folding it into
    either existing constant, it is admitted as a THIRD, separately-named exemption,
    MERGE05_LOCK_STATUS_READ_EXEMPTION_BYTES -- flash only; no second RAM exemption,
    since RAM did not move this phase. BASE-01's avr_targets and both flash band
    literals remain exactly as they were (test_base01_is_not_re_anchored_by_the_new_
    exemption's now-four-way source-scan is the direct proof).

    Arm 1 is untouched (its own fixed +96 B delta sits inside any non-negative
    allowance, however many terms compose it, so the widened ceiling changes
    nothing it asserts) -- but its assertions below are widened to also require the
    new `+lock288` term visible in the same PASS text, so a silently-dropped fourth
    term would still be caught here.

    Arm 2 (negative control) is RE-DERIVED again, for the same D-18 reason as
    Coverage 10 below: the old +307 B plant now sits comfortably inside the new
    594 B leonardo allowance and would go falsely green. Repointed to
    planted_size_baseline_policy_leonardo_growth_v151.log (+595 B, one byte past the
    new 0+96+210+288 = 594 B allowance), on the new `*_v151*` family.

    Arm 3 (from Plan 151-10) was the fully-landed post-151 tree
    (merge05_lock_status_v151_{uno,uno328pb,leonardo}.log, numerically identical to
    captured_build_v151_*.log but read here against BASE-01 under --policy merge05),
    PASSING at its delta (+594 B leonardo / +658 B uno-class) which sat EXACTLY at
    the THEN-new ceiling -- zero headroom -- with the four-term decomposition
    visible. That was the Arm-1 analog for Plan 151-10's own growth.

    Plan 153-15 (ERASE-08) repeats the shape a FOURTH time: the standalone
    `CMD_ERASE` software chip-erase (commits 0d90e5c, df09704, d9a9993, 8b7feac)
    added a further +130 B of flash, uniform on all three targets, and +0 B of RAM
    (153-DECISIONS.md's "Post-change measured position (cold)" section). Rather than
    folding it into any existing constant, it is admitted as a FOURTH,
    separately-named exemption, MERGE05_ERASE_STANDALONE_EXEMPTION_BYTES -- flash
    only; no second RAM exemption, since RAM did not move this phase. BASE-01's
    avr_targets and both flash band literals remain exactly as they were
    (test_base01_is_not_re_anchored_by_the_new_exemption's now-five-way source-scan
    is the direct proof).

    Arm 1 is untouched (its own fixed +96 B delta sits inside any non-negative
    allowance, however many terms compose it, so the widened ceiling changes
    nothing it asserts) -- but its assertions below are widened AGAIN to also
    require the new `+erase130` term visible in the same PASS text, so a
    silently-dropped fifth term would still be caught here.

    Arm 2 (negative control) is RE-DERIVED again, for the same D-18 reason as
    Coverage 10 below: the old +595 B plant now sits comfortably inside the new
    724 B leonardo allowance and would go falsely green. Repointed to
    planted_size_baseline_policy_leonardo_growth_v153.log (+725 B, one byte past the
    new 0+96+210+288+130 = 724 B allowance), on the new `*_v153*` family.

    Arm 3 (the Plan 151-10 admission proof, merge05_lock_status_v151_*.log) is KEPT
    -- its own history is a prior exemption's evidence and deleting it would erase
    that phase's record -- but its assertions below are widened AGAIN, the same way
    Arm 1's are: the ceiling it is compared against is now 724/788, not 594/658, so
    it no longer sits at zero headroom (that role now belongs to Arm 4 below); it
    sits at +594, 130 B inside the new ceiling, with the five-term decomposition
    visible.

    Arm 4 is NEW: the fully-landed post-erase tree (merge05_erase_standalone_v153_
    {uno,uno328pb,leonardo}.log, numerically identical to captured_build_v153_*.log
    but read here against BASE-01 under --policy merge05) PASSES at its delta
    (+724 B leonardo / +724 B uno-class raw, sitting 64 B inside the 788 B uno-class
    allowance) with leonardo EXACTLY at the new ceiling -- zero headroom -- and the
    full five-term decomposition visible on all three targets. This is the new
    exemption's own admission proof, the Arm-3 analog for Plan 153-15's own
    growth."""
    # Arm 1: the pre-Phase-149 tree is admitted, at exactly +96 flash / +0 RAM on
    # every target -- both comfortably inside the NEW allowance too.
    argv = ["--policy", "merge05", "--baseline", str(_BASE01_BASELINE)]
    for env, fixture in (
        ("leonardo", "merge05_defect_fix_fullflash_leonardo.log"),
        ("uno", "merge05_defect_fix_fullflash_uno.log"),
        ("uno328pb", "merge05_defect_fix_fullflash_uno328pb.log"),
    ):
        argv += ["--avr-log", f"{env}={_FIXTURES / fixture}"]

    result = _run_checker(argv)
    assert result.returncode == 0, (
        "expected --policy merge05 to PASS (exit 0) against the pre-Phase-149 tree "
        "under the adjudicated defect-fix exemption, still comfortably inside the "
        "new erase-standalone allowance.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "PASS:" in result.stdout, f"Expected PASS: in stdout. Got:\n{result.stdout}"
    assert result.stdout.count("+96<=") == 3, (
        "expected all three targets to report their flash delta as exactly +96 B "
        "IN THE PASS TEXT -- an exemption that makes the admitted growth invisible "
        "in the report is laundering. If this number moved, the fix's flash cost "
        "moved with it and MERGE05_DEFECT_FIX_EXEMPTION_BYTES, this leg and "
        "scripts/baseline/size_baseline.json's merge05_clause all need re-deriving.\n"
        f"Got:\n{result.stdout}"
    )
    assert (
        "band0+exempt96+seam210+lock288+erase130" in result.stdout
        and "band64+exempt96+seam210+lock288+erase130" in result.stdout
    ), (
        "expected the PASS text to show the flash allowance DECOMPOSED into FIVE "
        "terms -- the unchanged band literal, the Phase 145 defect-fix exemption, "
        "the Phase 149 page-size-seam exemption, Plan 151-10's lock-status-read "
        "exemption and Plan 153-15's erase-standalone exemption -- on both the "
        f"leonardo (0 B) and the uno-class (64 B) targets. Got:\n{result.stdout}"
    )
    assert "ram=1573/2048[+0<=2=seam2]" in result.stdout, (
        "expected uno's RAM figure to show the RAM allowance's decomposition "
        f"even at zero delta -- unchanged by Plan 153-15 (RAM did not move). "
        f"Got:\n{result.stdout}"
    )

    # Arm 2 (negative control): one byte past the NEW allowance still fails.
    over = _run_checker(
        [
            "--policy",
            "merge05",
            "--baseline",
            str(_BASE01_BASELINE),
            "--avr-log",
            f"leonardo={_FIXTURES / 'planted_size_baseline_policy_leonardo_growth_v153.log'}",
        ]
    )
    assert over.returncode == 1, (
        "NEGATIVE CONTROL: expected exit 1 on a planted +725 B leonardo growth — one "
        "byte beyond the new 724 B allowance (96 B defect-fix + 210 B page-size-seam "
        "+ 288 B lock-status-read + 130 B erase-standalone exemptions). If this "
        "passes, the exemption has become a blank cheque and the forward tripwire "
        f"is gone.\nstdout:\n{over.stdout}\nstderr:\n{over.stderr}"
    )
    assert "delta=+725" in over.stdout, (
        f"Expected the one-past-the-exemption delta named. Got:\n{over.stdout}"
    )
    assert "allowance of 724 B" in over.stdout, (
        f"Expected the leonardo effective allowance named. Got:\n{over.stdout}"
    )
    assert (
        "band 0 B + defect-fix exemption 96 B + page-size-seam exemption 210 B "
        "+ lock-status-read exemption 288 B + erase-standalone exemption 130 B"
        in over.stdout
    ), (
        f"Expected the FAIL line to decompose the allowance into all five terms. "
        f"Got:\n{over.stdout}"
    )

    # Arm 3 (Plan 151-10's admission proof, KEPT): now sits 130 B inside the new
    # ceiling rather than at zero headroom -- its assertions are widened to the
    # current five-term decomposition, never repointed or deleted.
    argv3 = ["--policy", "merge05", "--baseline", str(_BASE01_BASELINE)]
    for env, fixture in (
        ("leonardo", "merge05_lock_status_v151_leonardo.log"),
        ("uno", "merge05_lock_status_v151_uno.log"),
        ("uno328pb", "merge05_lock_status_v151_uno328pb.log"),
    ):
        argv3 += ["--avr-log", f"{env}={_FIXTURES / fixture}"]

    lock_status = _run_checker(argv3)
    assert lock_status.returncode == 0, (
        "expected --policy merge05 to still PASS (exit 0) against the fully-landed "
        "post-151 tree, now comfortably inside the widened post-erase allowance.\n"
        f"stdout:\n{lock_status.stdout}\nstderr:\n{lock_status.stderr}"
    )
    assert "PASS:" in lock_status.stdout, (
        f"Expected PASS: in stdout. Got:\n{lock_status.stdout}"
    )
    assert "+594<=724=band0+exempt96+seam210+lock288+erase130" in lock_status.stdout, (
        "expected leonardo's delta to sit 130 B inside the new ceiling (no longer "
        f"zero headroom -- that role moved to Arm 4), decomposition visible. "
        f"Got:\n{lock_status.stdout}"
    )
    assert (
        lock_status.stdout.count("+594<=788=band64+exempt96+seam210+lock288+erase130")
        == 2
    ), (
        "expected both uno-class targets to report the same +594 B delta against "
        f"their own 788 B allowance, decomposition visible. Got:\n{lock_status.stdout}"
    )

    # Arm 4 (NEW, Plan 153-15): the fully-landed post-erase tree PASSES at exactly
    # the new ceiling -- zero headroom on leonardo -- with the five-term
    # decomposition visible on all three targets. This is the new exemption's own
    # admission proof.
    argv4 = ["--policy", "merge05", "--baseline", str(_BASE01_BASELINE)]
    for env, fixture in (
        ("leonardo", "merge05_erase_standalone_v153_leonardo.log"),
        ("uno", "merge05_erase_standalone_v153_uno.log"),
        ("uno328pb", "merge05_erase_standalone_v153_uno328pb.log"),
    ):
        argv4 += ["--avr-log", f"{env}={_FIXTURES / fixture}"]

    admitted = _run_checker(argv4)
    assert admitted.returncode == 0, (
        "expected --policy merge05 to PASS (exit 0) against the fully-landed "
        "post-erase tree, exactly at the new allowance ceiling.\n"
        f"stdout:\n{admitted.stdout}\nstderr:\n{admitted.stderr}"
    )
    assert "PASS:" in admitted.stdout, f"Expected PASS: in stdout. Got:\n{admitted.stdout}"
    assert "+724<=724=band0+exempt96+seam210+lock288+erase130" in admitted.stdout, (
        "expected leonardo's delta to sit EXACTLY at the new ceiling (zero "
        f"headroom), decomposition visible. Got:\n{admitted.stdout}"
    )
    assert (
        admitted.stdout.count("+724<=788=band64+exempt96+seam210+lock288+erase130")
        == 2
    ), (
        "expected both uno-class targets to report the same +724 B delta against "
        f"their own 788 B allowance, decomposition visible. Got:\n{admitted.stdout}"
    )


def test_base01_is_not_re_anchored_by_the_new_exemption():
    """Phase 149 (D-12, PGSZ-04) binding precondition, captured as a leg rather
    than only stated in prose, STRENGTHENED by quick task 260820-a7w to machine-check
    the two-axis split the operator ruled on.

    CORRECTED (quick task 260820-a7w): this docstring used to claim "BASE-01's
    avr_targets are byte-unchanged" -- that is now FALSE. Quick task 260820-a7w moved
    BASE-01's avr_targets.*.flash_total from 32256/32384/28672 to 32768 on all three
    targets (plus each flash_free, derived), by explicit operator ruling. The true
    invariant this leg proves is narrower and still holds: BASE-01's GROWTH axis
    (flash_used, ram_used) is byte-unchanged, while its board-identity axis
    (flash_total) is licensed to move when the silicon ceiling genuinely changes. A
    green --policy merge05 run after a growth-axis re-anchor would mean the anchor
    moved, not that growth stayed inside the band -- BASE-01's own re_anchor_note
    says exactly this, and it is why this leg pins flash_used/ram_used, never
    flash_total, as the thing that must not move without cause. Both halves are now
    machine-checked below: the growth axis is pinned exactly as before, and the
    board-identity axis is pinned at its NEW value (32768) so a future accidental
    edit away from 32768 -- in either direction -- also fails this leg.

    STRENGTHENED again by Plan 151-10 (LOCK-02): the source-scan below gained a
    fourth pin, the exact string `MERGE05_LOCK_STATUS_READ_EXEMPTION_BYTES = 288`,
    the direct tripwire on this plan's own new exemption -- a new NAMED exemption
    is the sanctioned mechanism that leaves this leg green; re-anchoring BASE-01 a
    fourth time is not, and would turn it red.

    STRENGTHENED again by Plan 153-15 (ERASE-08): NOT repointed -- this leg never
    reads a fixture, by design, so it cannot be satisfied by planting a convenient
    log; the property it proves (BASE-01's own frozen figures, plus the checker's
    own source) is orthogonal to any *_v153* fixture. Three new pins added, all
    reading only BASE-01's own JSON and the checker's own source text:
    (1) the exact string `MERGE05_ERASE_STANDALONE_EXEMPTION_BYTES = 130` -- the
    direct tripwire on this plan's own fifth-named-total, fourth-flash exemption;
    (2) that the constant is actually READ by `_merge05_flash_allowance()` itself
    (not merely defined and left unconsumed) -- extracted by slicing the function's
    own source between its `def` line and the next; (3) that the constant's NAME
    never appears anywhere inside BASE-01's own raw JSON text -- BASE-01 is the
    frozen anchor, and an exemption constant leaking into it would be a form of
    laundering this leg exists to catch, not merely re-anchoring the figures
    directly."""
    with open(_BASE01_BASELINE) as f:
        base01 = json.load(f)
    assert base01["avr_targets"]["uno"]["flash_used"] == 24824
    assert base01["avr_targets"]["uno328pb"]["flash_used"] == 24874
    assert base01["avr_targets"]["leonardo"]["flash_used"] == 26906
    assert base01["avr_targets"]["uno"]["ram_used"] == 1573
    assert base01["avr_targets"]["uno328pb"]["ram_used"] == 1579
    assert base01["avr_targets"]["leonardo"]["ram_used"] == 2014
    # Board-identity axis (quick task 260820-a7w): licensed to move, pinned at its
    # new value so an accidental drift away from it is also caught.
    assert base01["avr_targets"]["uno"]["flash_total"] == 32768
    assert base01["avr_targets"]["uno328pb"]["flash_total"] == 32768
    assert base01["avr_targets"]["leonardo"]["flash_total"] == 32768

    checker_src = (_REPO_ROOT / "scripts" / "check_size_baseline.py").read_text()
    assert "MERGE05_UNO_CLASS_FLASH_BAND = 64" in checker_src, (
        "the uno-class flash band must stay exactly 64 -- widening it would "
        "silently admit unrelated future growth"
    )
    assert "MERGE05_DEFECT_FIX_EXEMPTION_BYTES = 96" in checker_src, (
        "the Phase 145 defect-fix exemption must stay exactly 96 -- it is not "
        "this phase's number to change"
    )
    assert "MERGE05_LOCK_STATUS_READ_EXEMPTION_BYTES = 288" in checker_src, (
        "Plan 151-10's own new exemption must be present, named and exactly 288 -- "
        "the direct tripwire on this plan's own admission"
    )
    assert "MERGE05_ERASE_STANDALONE_EXEMPTION_BYTES = 130" in checker_src, (
        "Plan 153-15's own new exemption must be present, named and exactly 130 -- "
        "the direct tripwire on this plan's own admission"
    )

    # Strengthening (1): the fourth flash exemption must actually be READ by the
    # flash allowance resolver, not merely defined and left unconsumed.
    func_start = checker_src.index("def _merge05_flash_allowance(")
    func_end = checker_src.index("\ndef ", func_start + 1)
    flash_allowance_body = checker_src[func_start:func_end]
    assert "MERGE05_ERASE_STANDALONE_EXEMPTION_BYTES" in flash_allowance_body, (
        "the erase-standalone exemption constant must be read inside "
        "_merge05_flash_allowance() itself -- a constant that is only defined and "
        "never consumed by the resolver is not a real exemption"
    )

    # Strengthening (2): the exemption constant's NAME must never appear inside
    # BASE-01's own raw JSON text -- BASE-01 is the frozen anchor, never a place an
    # exemption gets laundered into.
    base01_raw = _BASE01_BASELINE.read_text()
    assert "MERGE05_ERASE_STANDALONE_EXEMPTION_BYTES" not in base01_raw, (
        "the erase-standalone exemption constant must never be recorded inside "
        "BASE-01 itself -- BASE-01 is the frozen anchor, not a place exemptions "
        "get laundered into"
    )


def test_policy_merge05_fires_on_uno_class_over_band():
    """Coverage 9 — the planted +789 B Uno-class flash growth (one byte outside the
    EFFECTIVE 788 B allowance: the unchanged 64 B band plus the 96 B defect-fix
    exemption plus the 210 B page-size-seam exemption plus Plan 151-10's 288 B
    lock-status-read exemption plus Plan 153-15's new 130 B erase-standalone
    exemption) must fail --policy merge05, naming the computed delta, the allowance
    it exceeds, and the allowance's full five-term decomposition.

    Re-derived from +659 B by Plan 153-15 (ERASE-08), for the same D-18 reason
    every prior generation of this plant was re-derived: once the new exemption
    exists, a +659 B plant is INSIDE the new allowance and this leg would have
    gone falsely green while still claiming to prove a firing. The plant's single
    cause (a raised uno `used` figure) and its role (exactly one byte outside the
    enforced ceiling) are unchanged; only the number moved, and only because the
    ceiling moved.

    SEVERED onto the new `*_v153*` family for the same reason quick task 260820-a7w
    and Plan 151-10 severed it before: planted_size_baseline_policy_uno_over_band_
    v151.log's `used` figure (25483) is now well inside the new 788 B allowance and
    would fire for the wrong reason -- or not fire at all -- if fed here unmodified.
    Repointed to planted_size_baseline_policy_uno_over_band_v153.log, `used` raised
    to 25613 (BASE-01's 24824 + 789), preserving the single-byte-past-the-ceiling
    role, computed by importing the checker: `_merge05_flash_allowance("uno")`
    returns `(64, 96, 210, 288, 130, 788, "uno-class")`, so 24824 + 788 + 1 =
    25613."""
    result = _run_checker(
        [
            "--policy",
            "merge05",
            "--baseline",
            str(_BASE01_BASELINE),
            "--avr-log",
            f"uno={_FIXTURES / 'planted_size_baseline_policy_uno_over_band_v153.log'}",
        ]
    )
    assert result.returncode != 0, (
        f"expected non-zero exit on a planted +789 B uno-class flash growth.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "delta=+789" in result.stdout, f"Expected 'delta=+789'. Got:\n{result.stdout}"
    assert "allowance of 788 B" in result.stdout, (
        f"Expected 'allowance of 788 B'. Got:\n{result.stdout}"
    )
    assert (
        "band 64 B + defect-fix exemption 96 B + page-size-seam exemption 210 B "
        "+ lock-status-read exemption 288 B + erase-standalone exemption 130 B"
        in result.stdout
    ), (
        "Expected the allowance decomposed into all five terms: the unchanged "
        "64 B band, the 96 B defect-fix exemption, the 210 B page-size-seam "
        "exemption, the 288 B lock-status-read exemption and the new 130 B "
        f"erase-standalone exemption. Got:\n{result.stdout}"
    )


def test_policy_merge05_fires_on_leonardo_growth():
    """Coverage 10 — the planted +725 B Leonardo flash growth must fail --policy
    merge05 (Leonardo's base band is still 0 B must-not-grow, so its effective
    allowance is exactly 96 + 210 + 288 + 130 = 724 B, and +725 is one byte past
    it), naming the env and the delta.

    Re-derived from +595 B by Plan 153-15 (ERASE-08) for the same reason as
    Coverage 9 above: a +595 B plant now sits inside the new exemption and this
    leg would have gone falsely green. The plant's single cause and its
    one-byte-past-the-ceiling role are unchanged. This is the same fixture
    test_policy_merge05_admits_the_documented_defect_fix uses as its Arm 2
    negative control — deliberately shared rather than duplicated byte-identically;
    see that leg's docstring for the division of labour.

    SEVERED onto the new `*_v153*` family for the same reason as Coverage 9 above:
    this fixture's old +595 B plant is now well inside the new 724 B allowance, so
    it moved to planted_size_baseline_policy_leonardo_growth_v153.log -- `used`
    raised to 27631 (BASE-01's 26906 + 725), single-byte-past-the-ceiling role
    unchanged, computed by importing the checker:
    `_merge05_flash_allowance("leonardo")` returns
    `(0, 96, 210, 288, 130, 724, "leonardo")`, so 26906 + 724 + 1 = 27631. Shared,
    as before, with test_policy_merge05_admits_the_documented_defect_fix's Arm 2
    negative control."""
    result = _run_checker(
        [
            "--policy",
            "merge05",
            "--baseline",
            str(_BASE01_BASELINE),
            "--avr-log",
            f"leonardo={_FIXTURES / 'planted_size_baseline_policy_leonardo_growth_v153.log'}",
        ]
    )
    assert result.returncode != 0, (
        f"expected non-zero exit on a planted +725 B Leonardo flash growth.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "leonardo" in result.stdout, f"Expected 'leonardo'. Got:\n{result.stdout}"
    assert "delta=+725" in result.stdout, f"Expected 'delta=+725'. Got:\n{result.stdout}"


def test_policy_merge05_fires_on_ram_move():
    """Coverage 11 — the planted +3 B RAM move (one byte past the Phase 149
    page-size-seam RAM exemption of 2 B) must fail --policy merge05, naming
    ram_used and the RAM allowance's own decomposition.

    Before Phase 149, RAM equality was enforced with zero tolerance under the
    band mode, and a +1 B plant fired. Phase 149 (D-12) measured RAM moving by
    +2 B on all three AVR targets (the single `uint16_t page_size` handle field)
    and funded it with a named RAM exemption -- so the old +1 B plant now sits
    INSIDE the tolerance and would go falsely green. Re-derived to +3 B, one byte
    past the new 2 B tolerance, preserving the plant's single cause and its
    one-byte-past-the-ceiling role.

    SEVERED by quick task 260820-a7w for the same reason as Coverage 9/10 above:
    this fixture (derived from captured_build_uno.log) still carried the old
    32256 B total against BASE-01's new 32768 B, firing on flash_total as well as
    the planted RAM breach. Repointed to
    planted_size_baseline_policy_ram_moved_fullflash.log -- total changed to
    32768 only; the planted RAM `used` figure (1576) and flash `used` (24824,
    unchanged from the anchor) are untouched.

    RE-SEVERED by Plan 151-10 onto the new `*_v151*` family for family-consistency
    ONLY -- Plan 151-10 measured its own RAM growth at exactly 0 B
    (151-SIZE-TRANSCRIPTS.md), so NO second RAM exemption was authored and this
    leg's asserted values (delta=+3, ram allowance of 2 B, page-size-seam exemption
    2 B) are UNCHANGED. Repointed to planted_size_baseline_policy_ram_moved_v151.log,
    whose RAM/Flash figures match its `_fullflash` predecessor exactly -- moved
    purely so no leg in this module still reads a retired family.

    RE-SEVERED again by Plan 153-15 onto the new `*_v153*` family, for
    family-consistency ONLY -- Plan 153-15 measured its own RAM growth at exactly
    0 B against the immediately-prior position (153-DECISIONS.md's "Post-change
    measured position (cold)" section), so NO second RAM exemption was authored
    and this leg's asserted values (delta=+3, ram allowance of 2 B, page-size-seam
    exemption 2 B) remain UNCHANGED, byte-identical to the `_v151` figure -- the
    plan's own action calls this out explicitly ("that tolerance did not change in
    this phase, so this plant's figure is arithmetically the same as the previous
    generation's"). Repointed to planted_size_baseline_policy_ram_moved_v153.log,
    whose RAM/Flash figures match its `_v151` predecessor exactly -- moved purely
    so no leg in this module still reads a retired family."""
    result = _run_checker(
        [
            "--policy",
            "merge05",
            "--baseline",
            str(_BASE01_BASELINE),
            "--avr-log",
            f"uno={_FIXTURES / 'planted_size_baseline_policy_ram_moved_v153.log'}",
        ]
    )
    assert result.returncode != 0, (
        f"expected non-zero exit on a planted +3 B RAM move.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "ram_used" in result.stdout, f"Expected 'ram_used'. Got:\n{result.stdout}"
    assert "delta=+3" in result.stdout, f"Expected 'delta=+3'. Got:\n{result.stdout}"
    assert "ram allowance of 2 B" in result.stdout, (
        f"Expected 'ram allowance of 2 B'. Got:\n{result.stdout}"
    )
    assert "page-size-seam exemption 2 B" in result.stdout, (
        f"Expected the RAM allowance's own decomposition named. Got:\n{result.stdout}"
    )


def test_default_mode_is_unchanged_by_the_new_flag():
    """Coverage 12 — T-124-08: the default (no --policy) mode must be textually
    unchanged by the new flag's addition. All three captured logs still exit 0 and
    the output never contains the band-mode `<=64` substring.

    SEVERED by Plan 185-01 (CLAIM-04/CLAIM-05) for the same reason as
    test_clean_avr_all_three_envs_pass above: the live default baseline moved to
    the post-Phase-183 cold figures (uno 22734, uno328pb 22778, leonardo 24830 --
    Phase 183's flash shrink, confirmed unchanged since 183-04), so this leg now
    reads captured_build_v185_{uno,uno328pb,leonardo}.log -- the fixture, never
    the assertion, moved once more; the `<=64` substring check is exactly as it
    was. The v158 family is retired in place and kept, per the same convention
    prior generations used.

    RECONCILIATION: this leg's reddening was PREDICTED before it was observed, as
    one of the exhaustive four legs that couple to a live baseline value move
    (this one, the clean-AVR leg, the native leg, and the planted-regression leg
    above); the observed failing set at Task 1 Step 4 matched the predicted four
    exactly -- no fifth leg went red. `test_baseline_seam_precedence_flips_
    clean_log_to_fail` remains on `captured_build_v153_leonardo.log`,
    stale-but-green, deliberately left untouched as Phase 158 left it -- 4 red + 1
    deliberately untouched, not 5 red."""
    for env_name, fixture in (
        ("uno", "captured_build_v185_uno.log"),
        ("uno328pb", "captured_build_v185_uno328pb.log"),
        ("leonardo", "captured_build_v185_leonardo.log"),
    ):
        result = _run_checker(["--avr-log", f"{env_name}={_FIXTURES / fixture}"])
        assert result.returncode == 0, (
            f"{env_name}: expected exit 0 on a clean captured log in default mode.\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        assert "<=64" not in result.stdout, (
            f"{env_name}: default mode must never emit the band-mode '<=64' "
            f"substring. Got:\n{result.stdout}"
        )
