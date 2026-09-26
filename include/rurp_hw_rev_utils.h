#ifndef __RURP_HW_REV_UTILS_H__
#define __RURP_HW_REV_UTILS_H__

#ifdef HARDWARE_REVISION

#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "logging_id.h"
#include <Arduino.h>
#include <stdint.h>
#include <string.h>

uint8_t revision = 0xFF;

uint8_t rurp_map_ctrl_reg_for_hardware_revision(rurp_register_t data) {
    uint8_t ctrl_reg = 0;
    uint8_t hw = rurp_get_hardware_revision();
    switch (hw) {
    case REVISION_2_0:
    case REVISION_2_1:
    case REVISION_2_2:
    case REVISION_2_3:  // ctrl-reg layout identical to REV_2_x
        ctrl_reg = data & (CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE | CTRL_VPP_P1_ENABLE | CTRL_ADDRESS_LINE_17 | CTRL_READ_WRITE | CTRL_VPP_REGULATOR_ENABLE);
        ctrl_reg |= data & CTRL_VPP_VPE_DROP_ENABLE ? CTRL_VPP_VPE_DROP_ENABLE_REV2 : 0;
        ctrl_reg |= data & CTRL_ADDRESS_LINE_16 ? CTRL_ADDRESS_LINE_16_REV2 : 0;
        ctrl_reg |= data & CTRL_ADDRESS_LINE_18 ? CTRL_ADDRESS_LINE_18_REV2 : 0;
        break;
    case REVISION_0:
    case REVISION_1:
        ctrl_reg = data;
        ctrl_reg |= data & CTRL_VPP_VPE_DROP_ENABLE ? CTRL_VPP_VPE_DROP_ENABLE_REV1 : 0;
        break;
    default:
        // REVISION_UNKNOWN + any unrecognized byte fall through to ctrl_reg = 0
        // (fail-safe — no VPP enables, no VPE enables; EEPROM override is the
        // operator escape hatch).
        break;
    }

    return ctrl_reg;
}

uint8_t rurp_get_physical_hardware_revision() {
    return revision;
}

// 8-sample averaging on a single ADC pin — pure shift-divide, no library call.
// Robustifies the A3 detect-divider read against AVcc switching noise (the
// RURP shield's AVcc plane carries data-buffer + control-register switching
// loads — not a quiet ADC reference). ~104 µs added boot latency,
// ~30 B added Flash — well within the [20, 300] B delta band.
static uint16_t analog_read_avg8(uint8_t pin) {
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) {
        sum += (uint16_t)analogRead(pin);
    }
    return (uint16_t)(sum >> 3);  // average over 8 samples
}

void rurp_detect_hardware_revision() {
    // Both ADC pins INPUT (high-Z); let the R41+R_top divider drive A3.
    pinMode(PIN_HW_REVISION_DETECT_ADC, INPUT);
    pinMode(PIN_VPP_VOLTAGE_ADC, INPUT);

    // 8-sample averaging on the A3 detect divider to robustify against AVcc
    // switching noise.
    uint16_t adc_a3 = analog_read_avg8(PIN_HW_REVISION_DETECT_ADC);

    if (adc_a3 < ADC_BAND_R41_4K7_HIGH) {
        // Rev 2.0/2.1/2.2 with R41=4k7 — reports as REVISION_2_0 (broad bucket
        // operator distinguishes 2.1/2.2 via EEPROM hw_revision
        // override if needed).
        revision = REVISION_2_0;
    } else if (adc_a3 >= ADC_BAND_R41_10K_LOW && adc_a3 < ADC_BAND_R41_10K_HIGH) {
        // Rev 2.3 with R41=10k.
        revision = REVISION_2_3;
    } else if (adc_a3 >= ADC_BAND_R41_10K_HIGH) {
        // High band — no R41 (pre-detect-resistor era). Disambiguate Rev 0 vs
        // Rev 1 via the legacy A2 divider check (preserved from prior code —
        // already correct, already shipped).
        revision = analogRead(PIN_VPP_VOLTAGE_ADC) < 1000 ? REVISION_1 : REVISION_0;
    } else {
        // adc_a3 in the [ADC_BAND_R41_4K7_HIGH, ADC_BAND_R41_10K_LOW) guard gap —
        // physical detect inconclusive. EEPROM hw_revision override is the
        // escape hatch. 0xFE NOT 0xFF — 0xFF stays reserved as the
        // EEPROM-override-absent sentinel.
        revision = REVISION_UNKNOWN;
    }

    // Hard-fail-loud: a one-shot boot-time warn surfaces detect-inconclusive;
    // the dispatcher's silent ctrl_reg=0 fail-safe and the EEPROM override
    // escape hatch are both preserved. Re-uses MSG_INFO_HW (0x5B).
    if (revision == REVISION_UNKNOWN && rurp_get_config()->hardware_revision == 0xFF) {
        LOG_WARN_ID_U8(MSG_INFO_HW, (uint8_t)REVISION_UNKNOWN);
    }
}

uint8_t rurp_get_hardware_revision() {
    rurp_configuration_t* rurp_config = rurp_get_config();
    if (rurp_config->hardware_revision < 0xFF) {
        return rurp_config->hardware_revision;
    }
    return rurp_get_physical_hardware_revision();
}

#endif

#endif // __RURP_HW_REV_UTILS_H__