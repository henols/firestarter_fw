// golden_eprom_0x0B_write.inc — pinned (reg,data) trace for eprom 0x0B (EPROM_LEGACY) write.
// Input: 1-byte write (data_size=1, minimal representative — D-04), address=0,
//        FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE.
// Produced by: GOLDEN_BLESS mode in test_golden_eprom_0x0B_write (Phase 88 Plan 01).
// Re-bless: pio test -e native -f "*test_val_eprom*" with -DGOLDEN_BLESS; redirect rows here.
//
// Low-byte-only semantics (Pitfall 1):
//   0x0B uses direct VPE rail: only CTRL_VPP_REGULATOR_ENABLE (0x80), NO
//   CTRL_VPP_VPE_DROP_ENABLE.  The 8-bit trace is identical to 0x07/0x08 here
//   because CTRL_VPP_VPE_DROP_ENABLE (0x100) is invisible in the low-byte recording.
//   The INV-01 bit-level assertion distinguishes 0x0B from 0x07/0x08 at the bit level.
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
