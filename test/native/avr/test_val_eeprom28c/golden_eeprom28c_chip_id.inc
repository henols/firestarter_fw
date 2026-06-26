// golden_eeprom28c_chip_id.inc — pinned (reg,data) for eeprom28c chip-id (P4/P7) path.
// Input: CMD_WRITE, chip_id=0x1F08, scripted bytes {0x1F, 0x08, 0x20},
//   FLAG_SKIP_BLANK_CHECK|FLAG_SKIP_ERASE; operation_init only (not main).
// Low-byte semantics only: CTRL_VPP_VPE_DROP_ENABLE (0x100) not captured (Pitfall 1).
// Trace covers: eeprom28c_check_chip_id A9-12V VPP enable+disable (P4) +
//   SDP unlock (P7, flash_util_byte_flipping 6 entries) +
//   SDP wait (P5, eeprom28c_wait_for_write: first poll returns 0x20).
// Re-bless: run pio test -e native -f "*test_val_eeprom28c*" with -DGOLDEN_BLESS.
    { 0x08, 0x80 },
    { 0x08, 0x02 },
    { 0x08, 0x00 },
    { 0x08, 0x00 },
    { 0x01, 0x55 },
    { 0x02, 0x55 },
    { 0x01, 0xAA },
    { 0x02, 0x2A },
    { 0x01, 0x55 },
    { 0x02, 0x55 },
    { 0x01, 0x55 },
    { 0x02, 0x55 },
    { 0x01, 0xAA },
    { 0x02, 0x2A },
    { 0x01, 0x55 },
    { 0x02, 0x55 },
    { 0x08, 0x00 },
