#include "hardware_operations.h"

#include <Arduino.h>
#include <stdlib.h>

#include "firestarter.h"
#include "logging_id.h"
#include "messages.h"
#include "operation_utils.h"
#include "rurp_serial_utils.h"
#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "rurp_voltage_math.h"
#include "version.h"

bool hw_read_voltage(firestarter_handle_t* handle) {
    // State 0: Initialization. This runs only once per command.
    if (handle->operation_state == 0) {
        LOG_DEBUG_ID_SUB(DBG_INIT_READ_VOLTAGE);
#ifdef HARDWARE_REVISION
        if (rurp_get_hardware_revision() == REVISION_0) {
            LOG_ERROR_ID(MSG_ERR_REV0_VPP_RD);
            return true;  // Finish command with an error.
        }
#endif
        rurp_set_programmer_mode();
        if (handle->cmd == CMD_READ_VPP) {
            LOG_DEBUG_ID_SUB(DBG_SETTING_UP_VPP);
            rurp_write_to_register(CONTROL_REGISTER, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE);
        } else if (handle->cmd == CMD_READ_VPE) {
            LOG_DEBUG_ID_SUB(DBG_SETTING_UP_VPE);
            rurp_write_to_register(CONTROL_REGISTER, CTRL_VPP_REGULATOR_ENABLE);
        } else {
            rurp_set_communication_mode();
            LOG_ERROR_ID(MSG_ERR_CMD);
            return true;  // Finish command with an error.
        }
        delay(100);                     // Allow voltage to stabilize.
        rurp_set_communication_mode();  // Switch back to communication mode to send the ready signal.
        handle->operation_state = 1;    // Transition to the reading state.

        // Send a ready signal to the client to prompt it for the first ACK.
        // This establishes a handshake and avoids a race condition.
        LOG_OK_ID_U16(MSG_OK_READY, (uint16_t)DATA_BUFFER_SIZE);

        // Return false to keep the command active, but do not fall through.
        // The next call will be in state 1, ready to receive the client's ACK.
        return false;
    }

    // State 1+: Continuous reading loop.
    // We expect an "OK" (ACK) from the client to trigger a reading, or a
    // "DONE" to end the command cleanly. Capture the message once (a second
    // op_get_message call would consume further incoming bytes).
    op_message_type msg_type = op_get_message(handle);
    if (msg_type == OP_MSG_DONE) {
        // The host has signaled it is done sampling. Finish the command
        // cleanly instead of leaving it dangling for the 1 s watchdog to
        // reap.
        return true;
    }
    if (msg_type != OP_MSG_ACK) {
        // Neither ACK nor DONE yet. Keep waiting.
        // Returning false keeps the command active without doing anything.
        return false;
    }
    delay(500);

    // An ACK was received. Proceed with taking a measurement.
    uint16_t vcc_mv = rurp_read_vcc_mv();
    uint16_t voltage_mv = rurp_read_voltage_mv();

    // Millivolts, unformatted. The host formats them and checks the VCC band
    // (an internal VCC the board cannot run at means the stored bandgap is
    // wrong, and that error scales every VPP/VPE reading).
    LOG_DATA_ID_U16_U16(handle->cmd == CMD_READ_VPE ? MSG_DATA_VPE_MV : MSG_DATA_VPP_MV,
                        voltage_mv, vcc_mv);

    op_reset_timeout();  // Reset the command timeout since we're actively communicating.

    // Return false to indicate the command is not finished. It will continue
    // to wait for the next ACK from the client.
    return false;
}

bool fw_get_version(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_GET_FW_VERSION);
    // The only text-format emit. F("OK: FW: ") keeps the literal in PROGMEM.
    SERIAL_PORT.print(F("OK: FW: "));
    SERIAL_PORT.println(FW_VERSION);
    SERIAL_PORT.flush();
    return true;
}

#ifdef HARDWARE_REVISION
bool hw_get_version(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_GET_HW_VERSION);
    rurp_configuration_t* rurp_config = rurp_get_config();
    uint8_t physical  = (uint8_t)rurp_get_physical_hardware_revision();
    uint8_t effective = (rurp_config->hardware_revision < 0xFF)
                        ? (uint8_t)rurp_config->hardware_revision
                        : 0xFF;  // 0xFF: no override active
    LOG_OK_ID_U8_U8(MSG_OK_REV, physical, effective);
    return true;
}
#endif

bool hw_get_config(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_GET_CONFIG);
    rurp_configuration_t* rurp_config = rurp_get_config();
    // u32 r1 + u32 r2 + u8 override (0xFF = no override) + u16 bandgap_mv
    // + u16 bandgap_adc = 13 bytes. The host back-solves a calibration from
    // the count: bandgap_mv = meter_mv * bandgap_adc / RURP_ADC_FULL_SCALE.
    uint16_t bandgap_adc = 0;
#if RURP_HAS_BANDGAP_CAL
    // Sample in PROGRAMMER mode, because that is the mode every real
    // measurement is taken in: hw_read_voltage and eprom_check_vpp both
    // switch to it before reading. The I/O state differs between the two
    // modes and moves the count by a whole count on a SERIAL_ON_IO board --
    // about 0.5 %, stored permanently by a calibration.
    rurp_set_programmer_mode();
    bandgap_adc = (uint16_t)rurp_get_bandgap_adc_reading();
    rurp_set_communication_mode();
#endif
    uint8_t override_byte = (rurp_config->hardware_revision < 0xFF)
                            ? (uint8_t)rurp_config->hardware_revision
                            : 0xFF;
    uint8_t _cfg[13];
    _cfg[0] = (uint8_t)((rurp_config->r1 >> 24) & 0xFF);
    _cfg[1] = (uint8_t)((rurp_config->r1 >> 16) & 0xFF);
    _cfg[2] = (uint8_t)((rurp_config->r1 >>  8) & 0xFF);
    _cfg[3] = (uint8_t)((rurp_config->r1      ) & 0xFF);
    _cfg[4] = (uint8_t)((rurp_config->r2 >> 24) & 0xFF);
    _cfg[5] = (uint8_t)((rurp_config->r2 >> 16) & 0xFF);
    _cfg[6] = (uint8_t)((rurp_config->r2 >>  8) & 0xFF);
    _cfg[7] = (uint8_t)((rurp_config->r2      ) & 0xFF);
    _cfg[8] = override_byte;
    _cfg[9] = (uint8_t)((rurp_config->bandgap_mv >> 8) & 0xFF);
    _cfg[10] = (uint8_t)((rurp_config->bandgap_mv) & 0xFF);
    _cfg[11] = (uint8_t)(bandgap_adc >> 8);
    _cfg[12] = (uint8_t)bandgap_adc;
    LOG_ID_BYTES(MSG_OK_CFG, _cfg, 13);
    return true;
}

