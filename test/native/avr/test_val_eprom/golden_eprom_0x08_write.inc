// golden_eprom_0x08_write.inc — pinned (reg,data) trace for eprom 0x08 (EPROM_QUICK) write.
// Input: 1-byte write (data_size=1, minimal representative — D-04), address=0,
//        FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE.
// Produced by: GOLDEN_BLESS mode in test_golden_eprom_0x08_write (Phase 88 Plan 01).
// Re-bless: pio test -e native -f "*test_val_eprom*" with -DGOLDEN_BLESS; redirect rows here.
//
// low-byte-only semantics (Pitfall 1):
//   CTRL_VPP_VPE_DROP_ENABLE is 0x100 (HARDWARE_REVISION branch) — it does NOT fit
//   in uint8_t and is NOT captured in this trace.  The existing INV-01/INV-03
//   bit-level assertions in test_val_eprom.cpp remain as the complementary guard.
//   0x08 uses CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE (same as 0x07)
//   but CTRL_VPP_VPE_DROP_ENABLE (0x100) is invisible here.
    { 0x08, 0x80 },
    { 0x08, 0x00 },
    { 0x08, 0x80 },
    { 0x08, 0x04 },
    { 0x01, 0x01 },
    { 0x02, 0x00 },
    { 0x08, 0x00 },
    { 0x08, 0x00 },
    { 0x01, 0x01 },
    { 0x02, 0x00 },
    { 0x08, 0x00 },
