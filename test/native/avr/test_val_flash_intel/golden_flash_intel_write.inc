// golden_flash_intel_write.inc — pinned (reg,data) for flash_intel 0x10 write, 1-byte input
// (data_size=1, chip_id=0, FLAG_SKIP_BLANK_CHECK|FLAG_SKIP_ERASE).
// low-byte semantics only: CTRL_VPP_VPE_DROP_ENABLE (0x100) not captured (Pitfall 1).
// Trace covers: init VPP-gate (P3): CTRL_VPP_REGULATOR_ENABLE|CTRL_VPP_P1_ENABLE enable +
//   execute: 2 × firestarter_set_data (0x40 setup cmd + data byte at address 0) via
//   mem_util_set_address (LSB, MSB, CTL per write). flash_intel_cleanup not called here.
// Re-bless: run pio test -e native -f "*test_val_flash_intel*" with -DGOLDEN_BLESS.
    { 0x08, 0x88 },
    { 0x01, 0x01 },
    { 0x02, 0x00 },
    { 0x08, 0x00 },
    { 0x01, 0x01 },
    { 0x02, 0x00 },
    { 0x08, 0x00 },
