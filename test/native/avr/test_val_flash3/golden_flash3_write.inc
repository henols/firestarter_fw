// golden_flash3_write.inc — pinned (reg,data) trace for flash3 0x06 (FLASH_AMD_ALT) write.
// Input: 1-byte write (data_size=1, minimal representative — D-04), address=0,
//        FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE, chip_id=0.
// Produced by: GOLDEN_BLESS mode in test_golden_flash3_write (Phase 88 Plan 03).
// Re-bless: PLATFORMIO_BUILD_FLAGS=-DGOLDEN_BLESS pio test -e native -f "*test_val_flash3*" -v;
//           grep '{ 0x' from verbose output; redirect rows here.
//
// Trace covers flash3_write_execute for 1 byte at address 0:
//   - flash_execute_command(FLASH_ENABLE_WRITE): flash_util_byte_flipping writes
//     CTRL=0x00 (CTRL_READ_WRITE cleared), then LSB+MSB for 0x5555/0x2AAA/0x5555
//     SDP unlock sequence, then CTRL=0x00 again.
//   - memory_set_data (firestarter_set_data): LSB+MSB of data address 0x0000.
//   - flash_util_verify_operation: CTL set to CTRL_READ_WRITE=1 (read mode = 0x40)
//     for DQ7 poll; rurp_read_data_buffer() stub returns 0x00 = data_buffer[0],
//     DQ7 match on first poll; cleanup leaves CTRL=0x00.
//
// low-byte-only semantics (Pitfall 1):
//   CTRL_VPP_VPE_DROP_ENABLE is 0x100 (HARDWARE_REVISION branch) — it does NOT fit
//   in uint8_t and is NOT captured in this trace.  The existing INV-09 bit-level
//   assertion in test_val_flash3.cpp remains as the complementary guard.
    { 0x08, 0x00 },
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
    { 0x08, 0x40 },
