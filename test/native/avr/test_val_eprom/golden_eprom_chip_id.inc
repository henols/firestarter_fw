// golden_eprom_chip_id.inc — pinned (reg,data) trace for eprom chip-id (P4) path.
// Input: CMD_CHECK_CHIP_ID, protocol=0x07, chip_id=0x1F00 (non-zero enables compare),
//        scripted bytes: {0x1F, 0x00} (match → no error set).
// Produced by: GOLDEN_BLESS mode in test_golden_eprom_chip_id (Phase 88 Plan 01).
// Re-bless: pio test -e native -f "*test_val_eprom*" with -DGOLDEN_BLESS; redirect rows here.
//
// low-byte-only semantics (Pitfall 1):
//   CTRL_VPP_VPE_DROP_ENABLE is 0x100 (HARDWARE_REVISION branch) — invisible in trace.
//   The INV-01 bit-level assertions remain as the complementary guard.
//
// Trace structure (5 entries):
//   eprom_check_chip_id_init (calls eprom_check_vpp):
//     CTRL_VPP_REGULATOR_ENABLE|VPP_VPE_DROP_ENABLE set  → { 0x08, 0x80 } (DROP invisible)
//     check voltage; VPP+DROP clear                       → { 0x08, 0x00 }
//   eprom_check_chip_id_execute → eprom_internal_check_chip_id → eprom_get_chip_id:
//     CTRL_VPP_REGULATOR_ENABLE set                       → { 0x08, 0x80 }
//     CTRL_VPP_A9_ENABLE set                              → { 0x08, 0x02 }
//     CTRL_VPP_REGULATOR_ENABLE|CTRL_VPP_A9_ENABLE clear  → { 0x08, 0x00 }
    { 0x08, 0x80 },
    { 0x08, 0x00 },
    { 0x08, 0x80 },
    { 0x08, 0x02 },
    { 0x08, 0x00 },
