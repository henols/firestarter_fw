// golden_flash_intel_chip_id.inc — pinned (reg,data) for flash_intel chip-id (P4) path.
// Input: CMD_CHECK_CHIP_ID, chip_id=0x89B4, scripted bytes {0x89, 0xB4}.
// low-byte semantics only: CTRL_VPP_VPE_DROP_ENABLE (0x100) not captured (Pitfall 1).
// Trace covers: flash_intel_check_chip_id command-register autoselect sequence:
//   write 0x90 (autoselect enter) + write 0xFF (reset to read-array) —
//   2 × firestarter_set_data via mem_util_set_address (LSB, MSB, CTL per write).
//   Reads (0x0000, 0x0001) go through get_data which does not write to registers.
// Re-bless: run pio test -e native -f "*test_val_flash_intel*" with -DGOLDEN_BLESS.
    { 0x01, 0x01 },
    { 0x02, 0x00 },
    { 0x08, 0x00 },
    { 0x01, 0x01 },
    { 0x02, 0x00 },
    { 0x08, 0x00 },
