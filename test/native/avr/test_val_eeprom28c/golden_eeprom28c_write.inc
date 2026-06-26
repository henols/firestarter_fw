// golden_eeprom28c_write.inc — pinned (reg,data) for eeprom28c 0x0D write, 1-byte input
// (data_size=1, chip_id=0, FLAG_SKIP_BLANK_CHECK|FLAG_SKIP_ERASE).
// low-byte semantics only: CTRL_VPP_VPE_DROP_ENABLE (0x100) not captured (Pitfall 1).
// Trace covers: init SDP unlock (P7, flash_util_byte_flipping 6 entries) +
//   SDP wait (P5, eeprom28c_wait_for_write: first poll returns 0x20) +
//   execute: one firestarter_set_data write to address 0 (mem_util_set_address).
// Re-bless: run pio test -e native -f "*test_val_eeprom28c*" with -DGOLDEN_BLESS.
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
    { 0x01, 0x01 },
    { 0x02, 0x00 },
    { 0x08, 0x00 },
