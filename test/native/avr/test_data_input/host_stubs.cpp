/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * host stub TU for the test_data_input suite.
 *
 * This suite uses the include-as-source pattern (the test cpp #includes
 * leonardo_rurp_shield.cpp directly per RESEARCH.md Q2 Option D) and defines
 * PORTx/DDRx/PINx as test-local globals. The shared host stubs include file
 * under _shared/ is INTENTIONALLY NOT INCLUDED because:
 *   1. test_data_input does not link src/proms/*.cpp (it doesn't need the
 *      dispatch surface).
 *   2. The included leonardo_rurp_shield.cpp provides real implementations
 *      of rurp_set_data_input, rurp_read_data_buffer, rurp_set_data_output,
 *      rurp_write_data_buffer, rurp_set_control_pin, rurp_board_setup, and
 *      rurp_user_button_pressed — which would multiple-define against the
 *      shared stubs.
 *
 * The only host stub content needed is the Serial bool-cast operator (a
 * link-only stub referenced indirectly through ArduinoFake's USB-CDC
 * surface — used inside leonardo_rurp_shield.cpp's rurp_board_setup, which
 * the tests never call but the linker still resolves).
 */
#include <stdint.h>

#include <Arduino.h>
#include <ArduinoFake.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

/* Link-only stub for ArduinoFake's USB-CDC bool-cast — referenced by
 * leonardo_rurp_shield.cpp's rurp_board_setup `while (!SERIAL_PORT)` loop. */
Serial_::operator bool() {
    return true;
}

/* minimum link-time stubs.
 *
 * `[env:native].build_src_filter = +<proms/> +<boards/rurp_serial_utils.cpp>`
 * links src/proms/*.cpp into the test binary. Those TUs reference a handful
 * of `rurp_*` symbols that the shared host_stubs_common.inc usually supplies.
 * Since we cannot include the shared inc (would multiple-define
 * rurp_set_data_input / rurp_read_data_buffer against the real Leonardo
 * implementations pulled into the test cpp via #include-as-source), we
 * inline the minimum required stubs here.
 *
 * Each stub is intentionally a no-op — the data_input tests never assert on
 * voltage / hardware-revision / config behavior. They only exist so the
 * binary links. */

/* rurp_read_voltage_mv: referenced by eprom.cpp::eprom_check_vpp and
 * flash_intel.cpp::flash_intel_check_vpp. Neither is called by the tests. */
extern "C" uint16_t rurp_read_voltage_mv() {
    return 0;
}

/* rurp_get_config: referenced by the header-inlined
 * rurp_get_hardware_revision() (include/rurp_hw_rev_utils.h:61) which the
 * included leonardo_rurp_shield.cpp transitively pulls into the test TU.
 * Returns a zero-initialized config so the hardware-revision fallback path
 * (config->hardware_revision == 0xFF) is taken — that path calls
 * rurp_get_physical_hardware_revision(), which is ALSO header-defined into
 * the test TU by rurp_hw_rev_utils.h:37. Both are safe defaults — neither
 * is exercised by the data_input tests. */
static rurp_configuration_t s_host_config = {};
extern "C" rurp_configuration_t* rurp_get_config() {
    return &s_host_config;
}

extern "C" void op_reset_timeout() {
}
