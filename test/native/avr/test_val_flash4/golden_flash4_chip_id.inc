// golden_flash4_chip_id.inc — pinned (reg,data) trace for flash4 chip-id (P4 via flash_utils).
// Input: CMD_CHECK_CHIP_ID, protocol=0x05, chip_id=0xBFB7 (SST39SF040: mfr=0xBF dev=0xB7).
//        Scripted-byte mock returns {0xBF, 0xB7} → chip_id match, no error.
//        Mock re-assigned AFTER configure_memory() (Pitfall 3).
// Produced by: GOLDEN_BLESS mode in test_golden_flash4_chip_id (Phase 88 Plan 03).
// Re-bless: PLATFORMIO_BUILD_FLAGS=-DGOLDEN_BLESS pio test -e native -f "*test_val_flash4*" -v;
//           extract rows between test_golden_flash4_write:PASS and
//           test_golden_flash4_chip_id:PASS; redirect here.
//
// Trace covers flash4_check_chip_id_execute → flash_util_check_chip_id_execute →
// flash_util_get_chip_id:
//   - FLASH_ENABLE_ID (8 entries): CTL=0x00, LSB+MSB for 0x5555/0x2AAA/0x5555, CTL=0x00.
//   - 2 × firestarter_get_data (scripted mock — 0 register writes; reads bypass rurp_write_to_register).
//   - FLASH_DISABLE_ID (8 entries): CTL=0x00, LSB+MSB for 0x5555/0x2AAA/0x5555, CTL=0x00.
//   Total: 16 entries (< 256 cap).
//
// low-byte-only semantics (Pitfall 1):
//   CTRL_VPP_VPE_DROP_ENABLE is 0x100 (HARDWARE_REVISION branch) — it does NOT fit
//   in uint8_t and is NOT captured in this trace.  This is a 5V-only path;
//   no VPP bits are set in the chip-id sequence.
    { 0x08, 0x00 },
    { 0x01, 0x55 },
    { 0x02, 0x55 },
    { 0x01, 0xAA },
    { 0x02, 0x2A },
    { 0x01, 0x55 },
    { 0x02, 0x55 },
    { 0x08, 0x00 },
    { 0x08, 0x00 },
    { 0x01, 0x55 },
    { 0x02, 0x55 },
    { 0x01, 0xAA },
    { 0x02, 0x2A },
    { 0x01, 0x55 },
    { 0x02, 0x55 },
    { 0x08, 0x00 },
