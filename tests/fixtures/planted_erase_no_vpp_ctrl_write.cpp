// This is a copy of the real eeprom28c_erase_execute body
// (firestarter/src/proms/eeprom_28c.cpp) with one splice added: a VPP/VPE
// control-register write bracketed by rurp_chip_enable()/rurp_chip_disable()
// -- the exact fragment an executor would produce by re-deriving the
// AT28C256 datasheet's own HARDWARE Chip Erase mode (12V on OE) and
// asserting CTRL_VPE_ENABLE plus the VPP boost regulator around that same
// bracket, then splicing it into this handler by mistake.
// Both non-vacuity anchors (handle->firestarter_set_data( and
// delay(AT28C_TEC_MAX_MS)) are kept intact so this fixture exercises the
// VIOLATION path, not the anchor path -- a checker that only ever reaches
// the anchor path would never prove it can see a real hazard.

static void eeprom28c_erase_execute(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHIP_ERASE);
    eeprom28c_sdp_unlock_execute(handle);
    rurp_set_data_output();

    // PLANTED VIOLATION -- re-derived from the AT28C256 datasheet's own
    // hardware 12V-on-OE Chip Erase mode, not from AN 0544B. This is
    // exactly the hazard D-153-03 exists to catch: a control-register
    // write, wrapped in a chip-enable/disable bracket, reaching this body.
    rurp_chip_disable();
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE | CTRL_VPE_ENABLE, 1);
    rurp_chip_enable();

    handle->firestarter_set_data(handle, 0x5555, 0xAA);
    handle->firestarter_set_data(handle, 0x2AAA, 0x55);
    handle->firestarter_set_data(handle, 0x5555, 0x80);
    handle->firestarter_set_data(handle, 0x5555, 0xAA);
    handle->firestarter_set_data(handle, 0x2AAA, 0x55);
    handle->firestarter_set_data(handle, 0x5555, 0x10);
    delay(AT28C_TEC_MAX_MS);

    rurp_chip_disable();
}
