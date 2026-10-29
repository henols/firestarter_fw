/*
 * Project Name: Firestarter
 * Copyright (c) 2025 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#if DEV_TOOLS
#include "dev_tools.h"

#include <Arduino.h>

#include "firestarter.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "operation_utils.h"
#include "rurp_internal_register_utils.h"
#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "rurp_voltage_math.h"
#include "messages.h"

// The release and CI workflows look for this symbol name in the ELF to prove
// that an image was built with the dev commands. The register table text that
// they looked for before is drawn on the host now. One byte of flash; `used`
// keeps it through LTO.
extern "C" const uint8_t firestarter_dev_tools_image PROGMEM __attribute__((used)) = 1;

// Time, in milliseconds, a dev command waits for the user button.
#define DT_BUTTON_WAIT_MS 30000UL

static bool dt_wait_for_button(void) {
    unsigned long start = millis();
    while (!rurp_user_button_pressed()) {
        if ((unsigned long)(millis() - start) >= DT_BUTTON_WAIT_MS) {
            return false;
        }
        delay(200);
    }
    return true;
}

// MSG_INFO_REGISTERS views: which registers the host table shows.
#define DT_VIEW_REG_RAW 0         // dev reg: CTRL written as given, no remap
#define DT_VIEW_REG_FIRESTARTER 1 // dev reg --firestarter: 9-bit CTRL and its remap
#define DT_VIEW_ADDR 2            // dev addr: CTRL holds the TOP address register

// One frame with the raw values. The host draws the bit table. Not gated on
// FLAG_VERBOSE: the table is what dev reg and dev addr are for, and no host
// command sets that flag.
static void dt_report_registers(uint8_t msb, uint8_t lsb, uint16_t ctrl, uint8_t remapped, uint8_t view) {
    uint8_t _b[6] = {msb, lsb, (uint8_t)(ctrl >> 8), (uint8_t)ctrl, remapped, view};
    LOG_ID_BYTES(MSG_INFO_REGISTERS, _b, 6);
}

bool dt_set_registers(firestarter_handle_t* handle) {
    // Wait for an ACK message that precedes the register data
    if (op_get_message(handle) != OP_MSG_ACK) {
        return false;
    };

    unsigned long payload_deadline = millis() + TIMEOUT_MS;
    while (rurp_communication_available() < 4) {
        if ((long)(millis() - payload_deadline) >= 0) {
            return false;
        }
    }

    uint8_t msb = rurp_communication_read();
    uint8_t lsb = rurp_communication_read();
    uint16_t ctrl_reg = rurp_communication_read() << 8;
    ctrl_reg |= rurp_communication_read();
    int firestarter_reg = ctrl_reg & 0x8000;
    ctrl_reg &= 0x01FF;

    {
        uint8_t _b[2] = {
            (uint8_t)is_flag_set(FLAG_CHIP_ENABLE),
            (uint8_t)is_flag_set(FLAG_OUTPUT_ENABLE),
        };
        LOG_INFO_ID_BYTES(MSG_INFO_CE_OE, _b, 2);
    }
#ifdef HARDWARE_REVISION
    uint8_t ctrl_version = firestarter_reg ? rurp_map_ctrl_reg_for_hardware_revision(ctrl_reg) : (uint8_t)ctrl_reg;
#else
    uint8_t ctrl_version = (uint8_t)ctrl_reg;
#endif
    dt_report_registers(msb, lsb, ctrl_reg, ctrl_version,
                        firestarter_reg ? DT_VIEW_REG_FIRESTARTER : DT_VIEW_REG_RAW);
    LOG_OK_ID_U16(MSG_OK_READY, (uint16_t)DATA_BUFFER_SIZE);  // semantics ≈ "setup done, waiting on user button"
    rurp_set_programmer_mode();

    rurp_write_to_register(LEAST_SIGNIFICANT_BYTE, lsb);
    rurp_write_to_register(MOST_SIGNIFICANT_BYTE, msb);
    if (firestarter_reg) {
        rurp_write_to_register(CONTROL_REGISTER, ctrl_reg);
    } else {
        rurp_internal_write_to_register(CONTROL_REGISTER, ctrl_reg);
    }

    rurp_set_chip_enable(!is_flag_set(FLAG_CHIP_ENABLE));
    rurp_set_chip_output(!is_flag_set(FLAG_OUTPUT_ENABLE));

    bool pressed = dt_wait_for_button();
    rurp_set_communication_mode();
    if (!pressed) {
        LOG_ERROR_ID(MSG_ERR_TIMEOUT);
    }

    return true;
}

bool dt_set_address(firestarter_handle_t* handle) {
    {
        uint8_t _b[2] = {
            (uint8_t)is_flag_set(FLAG_CHIP_ENABLE),
            (uint8_t)is_flag_set(FLAG_OUTPUT_ENABLE),
        };
        LOG_INFO_ID_BYTES(MSG_INFO_CE_OE, _b, 2);
    }
    LOG_INFO_ID_U24(MSG_INFO_ADDR, handle->address);
    mem_util_set_read_high_mask(handle);
    uint32_t address = mem_util_remap_address_bus(handle, handle->address, is_flag_set(FLAG_OUTPUT_ENABLE));
    LOG_INFO_ID_U24(MSG_INFO_ADDR_REMAP, address);

    uint8_t msb = mem_util_calculate_msb_register(handle, address);
    uint8_t lsb = mem_util_calculate_lsb_register(handle, address);
    uint8_t top_address = mem_util_calculate_top_address_register(handle, address);
#ifdef HARDWARE_REVISION
    uint8_t ctrl_version = rurp_map_ctrl_reg_for_hardware_revision(top_address);
#else
    uint8_t ctrl_version = top_address;
#endif
    dt_report_registers(msb, lsb, top_address, ctrl_version, DT_VIEW_ADDR);
    LOG_OK_ID_U16(MSG_OK_READY, (uint16_t)DATA_BUFFER_SIZE);  // semantics ≈ "setup done, waiting on user button"
    rurp_set_programmer_mode();
    mem_util_set_address(handle, address);
    rurp_set_chip_enable(!is_flag_set(FLAG_CHIP_ENABLE));
    rurp_set_chip_output(!is_flag_set(FLAG_OUTPUT_ENABLE));
    bool pressed = dt_wait_for_button();
    rurp_set_communication_mode();
    if (!pressed) {
        LOG_ERROR_ID(MSG_ERR_TIMEOUT);
    }

    return true;
}

// Rail modes sampled by dt_read_adc, in the order it sweeps them. The mode
// byte is the first field of every MSG_DATA_ADC_RAW frame.
#define DT_ADC_MODE_RAILS_OFF 0
#define DT_ADC_MODE_VPP       1
#define DT_ADC_MODE_VPE       2

// Settle time after changing the rail, before sampling. Matches the delay
// hw_read_voltage uses for the same transition (hardware_operations.cpp).
#define DT_ADC_SETTLE_MS 100

static void dt_emit_adc_frame(uint8_t mode, uint16_t divider_adc, uint16_t bandgap_adc,
                              uint16_t voltage_mv, uint16_t vcc_mv,
                              uint32_t r1, uint32_t r2) {
    // 17 bytes, big-endian, matching MSG_DATA_ADC_RAW in the catalog:
    // [mode u8][divider u16][bandgap u16][voltage_mv u16][vcc_mv u16][r1 u32][r2 u32]
    uint8_t buf[17];
    buf[0]  = mode;
    buf[1]  = (uint8_t)((divider_adc >> 8) & 0xFF);
    buf[2]  = (uint8_t)((divider_adc     ) & 0xFF);
    buf[3]  = (uint8_t)((bandgap_adc >> 8) & 0xFF);
    buf[4]  = (uint8_t)((bandgap_adc     ) & 0xFF);
    buf[5]  = (uint8_t)((voltage_mv  >> 8) & 0xFF);
    buf[6]  = (uint8_t)((voltage_mv      ) & 0xFF);
    buf[7]  = (uint8_t)((vcc_mv      >> 8) & 0xFF);
    buf[8]  = (uint8_t)((vcc_mv          ) & 0xFF);
    buf[9]  = (uint8_t)((r1 >> 24) & 0xFF);
    buf[10] = (uint8_t)((r1 >> 16) & 0xFF);
    buf[11] = (uint8_t)((r1 >>  8) & 0xFF);
    buf[12] = (uint8_t)((r1      ) & 0xFF);
    buf[13] = (uint8_t)((r2 >> 24) & 0xFF);
    buf[14] = (uint8_t)((r2 >> 16) & 0xFF);
    buf[15] = (uint8_t)((r2 >>  8) & 0xFF);
    buf[16] = (uint8_t)((r2      ) & 0xFF);
    LOG_ID_BYTES(MSG_DATA_ADC_RAW, buf, 17);
}

typedef struct {
    uint16_t divider_adc;
    uint16_t bandgap_adc;
    uint16_t voltage_mv;
    uint16_t vcc_mv;
} dt_adc_sample_t;

static void dt_sample_adc(dt_adc_sample_t* out, rurp_register_t ctrl) {
    rurp_configuration_t* cfg = rurp_get_config();
    // Written directly, exactly as hw_read_voltage does. handle's
    // firestarter_set_control_register is NULL here: only configure_memory()
    // sets it, and CMD_DEV_ADC is deliberately not a memory command.
    rurp_write_to_register(CONTROL_REGISTER, ctrl);
    if (ctrl != 0) {
        delay(DT_ADC_SETTLE_MS);
    }

    // EXACTLY ONE sample of each channel, and every figure below derived from
    // those two. Calling rurp_read_vcc_mv() and rurp_read_voltage_mv() here
    // instead would take THREE bandgap samples and two divider samples, so the
    // reported counts would describe different instants from the millivolt
    // figures printed beside them -- a diagnostic that contradicts itself by
    // about one count, which is 0.5 % on the bandgap channel.
    out->divider_adc = rurp_read_divider_adc();
    out->bandgap_adc = (uint16_t)rurp_get_bandgap_adc_reading();
    out->vcc_mv = rurp_scale_vcc_mv(out->bandgap_adc, cfg->bandgap_mv);
    out->voltage_mv = rurp_scale_voltage_mv(out->divider_adc, out->bandgap_adc,
                                            (uint32_t)cfg->r1, (uint32_t)cfg->r2,
                                            cfg->bandgap_mv);
}

/*
 * Raw ADC diagnostic readout.
 *
 * Sweeps the three rail states in one command so all three samples are taken
 * at ONE pot setting, which is what makes them a usable pair for a
 * calibration measurement. Emits one MSG_DATA_ADC_RAW frame per state and
 * leaves every rail off.
 *
 * Sets no socket-routing bit (CTRL_VPP_P1_ENABLE, CTRL_VPE_ENABLE,
 * CTRL_VPP_A9_ENABLE), exactly as hw_read_voltage does, so the high voltage
 * never reaches the socket and this is safe to run with a chip seated.
 *
 * Every frame is emitted AFTER returning to communication mode. On
 * SERIAL_ON_IO targets (uno, uno328pb) the UART is stopped for the whole
 * programmer-mode window and log frames queue in a 4-slot buffer that drops
 * the next frame without an error once full -- so three frames sent from
 * inside that window would be silently lost on exactly the boards the bench
 * session may use.
 */
bool dt_read_adc(firestarter_handle_t* handle) {
    dt_adc_sample_t samples[3];
    uint8_t i;
    rurp_configuration_t* cfg;

    (void)handle;
#ifdef HARDWARE_REVISION
    if (rurp_get_hardware_revision() == REVISION_0) {
        LOG_ERROR_ID(MSG_ERR_REV0_VPP_RD);  // Rev 0 carries no divider at all
        return true;
    }
#endif
    rurp_set_programmer_mode();
    dt_sample_adc(&samples[DT_ADC_MODE_RAILS_OFF], 0);
    dt_sample_adc(&samples[DT_ADC_MODE_VPP],
                  CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE);
    dt_sample_adc(&samples[DT_ADC_MODE_VPE], CTRL_VPP_REGULATOR_ENABLE);
    rurp_write_to_register(CONTROL_REGISTER, 0);  // leave every rail off
    rurp_set_communication_mode();

    cfg = rurp_get_config();
    for (i = 0; i < 3; i++) {
        dt_emit_adc_frame(i, samples[i].divider_adc, samples[i].bandgap_adc,
                          samples[i].voltage_mv, samples[i].vcc_mv,
                          (uint32_t)cfg->r1, (uint32_t)cfg->r2);
    }
    return true;
}

#endif
