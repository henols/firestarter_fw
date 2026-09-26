/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __RURP_SHIELD_H__
#define __RURP_SHIELD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "rurp_platform_compat.h"
#include "rurp_types.h"
#include "rurp_pinout.h"

#ifdef HARDWARE_REVISION
// Hardware-revision enum values (out of the alias-migration scope —
// these are revision identifiers, not RURP-signal aliases).
#define REVISION_0 0
#define REVISION_1 1
#define REVISION_2_0 2
#define REVISION_2_1 3
#define REVISION_2_2 4
#define REVISION_2_3 5
#define REVISION_UNKNOWN 0xFE  // ADC band-gap fall-through; 0xFF reserved for EEPROM-override-absent sentinel
#endif

// VPP DIP-bus magic constants. Set by the Python host in bus_config.vpp_line;
// firmware's using_p1_as_vpp() (memory_utils.h) detects 24/28/32-pin chips
// whose physical VPP pin is socket pin 1 (after bodge wire / Rev 2.2 JP4) and
// redirects CTRL_VPE_ENABLE → CTRL_VPP_P1_ENABLE in eprom.cpp.
#define VPP_P1_32_DIP               0x15
#define VPP_P1_28_DIP               0x0F
#define VPP_P21_24_DIP              0x0B


// Constants
#define CONFIG_VERSION "VER06"

// Default configuration
#define VALUE_R1 270000
#define VALUE_R2 44000

// Bit masks
#define LEAST_SIGNIFICANT_BYTE 0x01  // LEAST SIGNIFICANT BYTE
#define MOST_SIGNIFICANT_BYTE 0x02   // MOST SIGNIFICANT BYTE
#define OUTPUT_ENABLE 0x04           // OUTPUT ENABLE
#define CONTROL_REGISTER 0x08        // CONTROL REGISTER
#define CHIP_ENABLE 0x20             // CHIP ENABLE

    // Function prototypes
    void rurp_board_setup();
    void rurp_load_config();


#ifdef SERIAL_ON_IO
    void rurp_set_programmer_mode();
    void rurp_set_communication_mode();
#else
    /* Dedicated USB/UART targets do not need to disconnect communication
     * from the PROM data bus. Keep typed no-op functions instead of macros so
     * call sites are checked consistently on AVR, RP2040 and native builds. */
    static inline void rurp_set_programmer_mode(void) {
    }

    static inline void rurp_set_communication_mode(void) {
    }
#endif

    int rurp_communication_available();
    int rurp_communication_read();
    int rurp_communication_peak();
    size_t rurp_communication_write(const char* buffer, size_t size);
    size_t rurp_communication_read_bytes(char* buffer, size_t length);
    int rurp_communication_read_data(char* buffer, size_t cap);


    // ID-encoded wire frame emit -- the sole log surface.
    void rurp_log_id(uint8_t id, const uint8_t* params, uint8_t param_count);

    // Fixed-shape packers — wrap the byte-array pack + rurp_log_id call so
    // each LOG_*_ID_U{8,16,24,32} macro invocation collapses to a single
    // CALL instruction at the call site. MSB-first wire encoding per
    // MSB-first parameter encoding.
    void rurp_log_id_u8(uint8_t id, uint8_t v);
    void rurp_log_id_u16(uint8_t id, uint16_t v);
    void rurp_log_id_u24(uint8_t id, uint32_t v);
    void rurp_log_id_u32(uint8_t id, uint32_t v);

    // Wide variant for MSG_DATA_CHUNK payloads > 255 bytes.
    // param_count is uint16_t to avoid overflow for 512 / 1024-byte chunks.
    void rurp_log_id_wide(uint8_t id, const uint8_t* params, uint16_t param_count);

    void rurp_set_data_output();
    void rurp_set_data_input();
    void rurp_set_control_pin(uint8_t pin, uint8_t state);

    /* Logical control helpers are typed inline functions rather than
     * function-like macros. This preserves zero-overhead call sites while
     * avoiding repeated argument evaluation and improving portability. */
    static inline void rurp_set_chip_enable(uint8_t state) {
        rurp_set_control_pin(CHIP_ENABLE, state);
    }

    static inline void rurp_set_chip_output(uint8_t state) {
        rurp_set_control_pin(OUTPUT_ENABLE, state);
    }

    static inline void rurp_chip_enable(void) {
        rurp_set_chip_enable(0);  // CE active low: enable chip
    }

    static inline void rurp_chip_disable(void) {
        rurp_set_chip_enable(1);  // CE active low: disable chip
    }

    static inline void rurp_chip_output(void) {
        rurp_set_chip_output(0);  // OE active low: enable chip output
    }

    static inline void rurp_chip_input(void) {
        rurp_set_chip_output(1);  // OE active low: disable chip output
    }

    void rurp_write_to_register(uint8_t reg, rurp_register_t data);
    rurp_register_t rurp_read_from_register(uint8_t reg);

    void rurp_write_data_buffer(uint8_t data);
    uint8_t rurp_read_data_buffer();

    uint16_t rurp_read_vcc_mv();
    uint16_t rurp_read_voltage_mv();

    long rurp_get_bandgap_adc_reading();
    uint8_t rurp_user_button_pressed();

    rurp_configuration_t* rurp_get_config();
    void rurp_save_config(rurp_configuration_t* config);
    void rurp_validate_config(rurp_configuration_t* config);

#ifdef HARDWARE_REVISION
    void rurp_detect_hardware_revision();
    uint8_t rurp_get_hardware_revision();
    uint8_t rurp_get_physical_hardware_revision();
    uint8_t rurp_map_ctrl_reg_for_hardware_revision(rurp_register_t data);
#endif

#ifdef __cplusplus
}
#endif

#endif // __RURP_SHIELD_H__
