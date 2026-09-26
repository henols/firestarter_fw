/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */


#ifdef ARDUINO_AVR_LEONARDO
#include "rurp_shield.h"
#include <Arduino.h>
#include "rurp_register_utils.h"

#include "rurp_serial_utils.h"

#define PORTC_DATA_MASK 0x40
#define PORTD_DATA_MASK 0x9f // D0(PD2), D1(PD3), D2(PD1), D3(PD0), D4(PD4), D7(PD7)
#define PORTE_DATA_MASK 0x40

#define PORTB_CONTROL_MASK 0xf0
#define PORTD_CONTROL_MASK 0x40 // D12 (PD6)
#define PORTC_CONTROL_MASK 0x80


// Constant for VCC calculation using the internal 1.1V bandgap reference.
// Formula: (1.1V * 1024 ADC steps * 1000 mV/V)
static constexpr long VCC_CALC_CONSTANT = 1126400L;


uint8_t control_pins = 0x00;

void rurp_board_setup() {
    DDRB |= PORTB_CONTROL_MASK; // Set pins D8-D13 as output
    DDRC |= PORTC_CONTROL_MASK; // Set pin D13 as output (PC7)
    DDRD |= PORTD_CONTROL_MASK; // Set pin D12 as output (PD6)

    // Note: User button is not available on Leonardo shield revision.

    SERIAL_PORT.begin(MONITOR_SPEED);
    while (!SERIAL_PORT) {
        delayMicroseconds(1);
    }
    SERIAL_PORT.flush();
    delay(1);
}

void rurp_set_control_pin(uint8_t pin, uint8_t state) {
    // Update the state of the logical control pins
    control_pins = state ? (control_pins | pin) : (control_pins & ~pin);

    // Map logical control_pins bits to physical port pins
    // The control lines are scattered across PORTB, PORTC, and PORTD.
    // Logical Bit -> Arduino Pin -> MCU Pin
    // ---------------------------------------
    // Bit 0       -> D8          -> PB4
    // Bit 1       -> D9          -> PB5
    // Bit 2       -> D10         -> PB6
    // Bit 3       -> D11         -> PB7
    // Bit 4       -> D12         -> PD6
    // Bit 5       -> D13         -> PC7

    // Bits 0-3 -> PB4-PB7
    uint8_t portb_val = control_pins << 4;
    // Bit 4 -> PD6
    uint8_t portd_val = (control_pins & 0x10) << 2;
    // Bit 5 -> PC7
    uint8_t portc_val = (control_pins & 0x20) << 2;

    // Atomically update the ports using read-modify-write
    PORTB = (PORTB & ~PORTB_CONTROL_MASK) | portb_val;
    PORTD = (PORTD & ~PORTD_CONTROL_MASK) | portd_val;
    PORTC = (PORTC & ~PORTC_CONTROL_MASK) | portc_val;
}

uint8_t rurp_user_button_pressed() {
    // User button is not connected on the Leonardo shield revision.
    return 0;
}

void rurp_write_data_buffer(uint8_t data) {
    rurp_set_data_output(); // Ensure data lines are output

    // Map data bus bits (D0-D7) to their respective port pins for Leonardo.
    // This is complex due to the non-contiguous pinout.
    // Data Bit -> Arduino Pin -> MCU Pin
    // -----------------------------------
    // D0       -> D0          -> PD2
    // D1       -> D1          -> PD3
    // D2       -> D2          -> PD1
    // D3       -> D3          -> PD0
    // D4       -> D4          -> PD4
    // D5       -> D5          -> PC6
    // D6       -> D6          -> PD7
    // D7       -> D7          -> PE6

    uint8_t portd_val = ((data & _BV(0)) << 2) | // D0 to PD2
                        ((data & _BV(1)) << 2) | // D1 to PD3
                        ((data & _BV(2)) >> 1) | // D2 to PD1
                        ((data & _BV(3)) >> 3) | // D3 to PD0
                        (data & _BV(4)) |        // D4 to PD4
                        ((data & _BV(6)) << 1);  // D6 to PD7

    uint8_t portc_val = (data & _BV(5)) << 1;    // D5 to PC6
    uint8_t porte_val = (data & _BV(7)) >> 1;    // D7 to PE6

    // Atomically update the ports using read-modify-write
    PORTD = (PORTD & ~PORTD_DATA_MASK) | portd_val;
    PORTC = (PORTC & ~PORTC_DATA_MASK) | portc_val;
    PORTE = (PORTE & ~PORTE_DATA_MASK) | porte_val;
}

uint8_t rurp_read_data_buffer() {
    // Read from ports and map back to data bus bits (D0-D7).
    // Insert a single _NOP() between each PINx read to let the AVR's input
    // synchronizer latch settle before the next port read. The 32U4 PINx
    // register has a 0.5-1.5 clock-cycle latch latency (datasheet 7766J
    // §10.2.4); with a partially-erased EPROM (weak chip drive) plus the
    // address-bus driven through nearby PCB traces, three back-to-back PINx
    // reads can sample mid-transition values. One _NOP() @ 16 MHz = 62.5 ns;
    // worst-case W27C512 tACC at 5V is 90 ns. Two stalls put total settling
    // at ~125 ns - comfortably > tACC, < 1 µs / 64KB read overhead.
    uint8_t pind_val = PIND;
    _NOP();
    uint8_t pinc_val = PINC;
    _NOP();
    uint8_t pine_val = PINE;

    uint8_t data = 0;
    data |= ((pind_val & _BV(2)) >> 2); // PD2 -> D0
    data |= ((pind_val & _BV(3)) >> 2); // PD3 -> D1
    data |= ((pind_val & _BV(1)) << 1); // PD1 -> D2
    data |= ((pind_val & _BV(0)) << 3); // PD0 -> D3
    data |= (pind_val & _BV(4));        // PD4 -> D4
    data |= ((pinc_val & _BV(6)) >> 1); // PC6 -> D5
    data |= ((pind_val & _BV(7)) >> 1); // PD7 -> D6
    data |= ((pine_val & _BV(6)) << 1); // PE6 -> D7

    return data;
}

void rurp_set_data_output() {
    DDRD |= PORTD_DATA_MASK; // Set pins D0-D3 and D4-D7 as output
    DDRC |= PORTC_DATA_MASK; // Set pin D5 as output
    DDRE |= PORTE_DATA_MASK; // Set pin D6 as output
}

void rurp_set_data_input() {
    DDRD &= ~PORTD_DATA_MASK; // Set pins D0-D3 and D4-D7 as output
    DDRC &= ~PORTC_DATA_MASK; // Set pin D5 as output
    DDRE &= ~PORTE_DATA_MASK; // Set pin D6 as output
}

#endif
