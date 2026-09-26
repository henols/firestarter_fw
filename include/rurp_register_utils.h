#ifndef __RURP_REGISTERS_UTILS_H__
#define __RURP_REGISTERS_UTILS_H__

#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "rurp_internal_register_utils.h"

#ifdef HARDWARE_REVISION
#include "rurp_hw_rev_utils.h"
#endif

uint8_t lsb_address = 0xff;
uint8_t msb_address = 0xff;
rurp_register_t control_register = 0xff;

// Function to write data to a specific register on the RURP shield
// This function also caches the register values and adds a small delay
// if the CTRL_VPP_P1_ENABLE bit is being cleared, to allow voltage to settle.
// It also maps the control register data based on the hardware revision.
//
// Parameters:
//   reg: The register to write to (e.g., LEAST_SIGNIFICANT_BYTE, MOST_SIGNIFICANT_BYTE, CONTROL_REGISTER).
//   data: The data to write to the register.
void rurp_write_to_register(uint8_t reg, rurp_register_t data) {
    bool settle = false;
    switch (reg) {
    case LEAST_SIGNIFICANT_BYTE:
        if (lsb_address == (uint8_t)data) {
            return;
        }
        lsb_address = data;
        break;
    case MOST_SIGNIFICANT_BYTE:
        if (msb_address == (uint8_t)data) {
            return;
        }
        msb_address = data;
        break;
    case CONTROL_REGISTER:
        if (control_register == data) {
            return;
        }
        if ((control_register & CTRL_VPP_P1_ENABLE) > (data & CTRL_VPP_P1_ENABLE)) {
            settle = true;
        }
        control_register = data;
#ifdef HARDWARE_REVISION
        data = rurp_map_ctrl_reg_for_hardware_revision(data);
#endif
        break;
    default:
        return;
    }
    rurp_internal_write_to_register(reg, data);

    //Take a break here if an address change needs time to settle
    if (settle) {
        delayMicroseconds(4);
    }
}


void rurp_internal_write_to_register(uint8_t reg, rurp_register_t data) {
#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_ATmega328PB)
    // On Uno, PORTD bit 6 doubles as the chip socket's data line D6 AND the
    // 74HC573 MSB latch's D6 input. When an MSB strobe writes a value with
    // bit 6 HIGH (read mode for chips with R/W on MSB bit 6, e.g. FM1608 and
    // other JEDEC 28-pin SRAM/EEPROM/FRAM), the PD6 LOW->HIGH transition
    // coincides with the LE strobe and capacitively couples to the chip's
    // /CE line, briefly pulling it LOW and causing the chip to capture the
    // PORTD value at the LSB-latched address. Pre-clearing PORTD = 0 with a
    // ~250 ns settle (4 NOPs) before the data write keeps the PD6 rising
    // edge in a quiet window before LE rises. Verified at the bench:
    // restores 32-of-33 byte fidelity for FM1608. Leonardo path is
    // unaffected (PD6 is not on the chip's data bus there).
    if (reg == MOST_SIGNIFICANT_BYTE) {
        rurp_set_data_output();
        PORTD = 0;
        __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t");
    }
#endif
    rurp_write_data_buffer(data);
    rurp_set_control_pin(reg, 1);
    // Probably useless - verify later
    delayMicroseconds(1);

    rurp_set_control_pin(reg, 0);
}

rurp_register_t rurp_read_from_register(uint8_t reg) {
    switch (reg) {
    case LEAST_SIGNIFICANT_BYTE:
        return lsb_address;
    case MOST_SIGNIFICANT_BYTE:
        return msb_address;
    case CONTROL_REGISTER:
        return control_register;
    }
    return 0;
}


#endif // __RURP_REGISTERS_UTILS_H__