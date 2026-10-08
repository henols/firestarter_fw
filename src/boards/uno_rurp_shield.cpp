/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_ATmega328PB)
#include "rurp_shield.h"
#include <Arduino.h>
#include "rurp_register_utils.h"

#include "rurp_serial_utils.h"

#define USER_BUTTON 0x10             // USER BUTTON

bool com_mode = true;

// Deferred-log buffer.
//
// On the Uno PORTD doubles as the data bus in programmer mode, so emitting a
// frame mid-operation would corrupt the programming pulse -- hence the com_mode
// gate below. Dropping those frames instead loses every operation-emitted error
// and progress frame, and the host times out with nothing. So they are BUFFERED
// and flushed once communication mode is restored.
//
// Sizing: DEBUG frames compile out in production and an operation emits at most
// ~1-2 critical frames per programmer-mode window, so 4 has headroom. Narrow
// frames only -- the wide MSG_DATA_CHUNK path runs in communication mode.
#define DEFERRED_LOG_MAX 4
#define DEFERRED_PARAM_MAX 8  // widest narrow frame (U32_U32 / U16x4 / progress) = 8 bytes
static uint8_t deferred_count = 0;
static struct {
    uint8_t id;
    uint8_t len;
    uint8_t params[DEFERRED_PARAM_MAX];
} deferred_log[DEFERRED_LOG_MAX];


void rurp_board_setup() {
    // Set control pins on PORTB to output.
    // PB0-3 are register select lines, PB5 is Chip Enable.
    // PB4 is User Button input.
    // PB6 is a crystal pin on a standard Uno, so it is not used for I/O.
    // Write the port latch first so CE and OE are already high (chip disabled,
    // outputs off) when the pins become outputs, and the button pull-up is set.
    PORTB = USER_BUTTON | OUTPUT_ENABLE | CHIP_ENABLE;
    DDRB = LEAST_SIGNIFICANT_BYTE | MOST_SIGNIFICANT_BYTE | CONTROL_REGISTER | OUTPUT_ENABLE | CHIP_ENABLE;

    rurp_chip_disable();
    rurp_chip_input();

    rurp_write_to_register(LEAST_SIGNIFICANT_BYTE, 0x00);
    rurp_write_to_register(MOST_SIGNIFICANT_BYTE, 0x00);
    rurp_write_to_register(CONTROL_REGISTER, 0x00);

    rurp_set_communication_mode();
}

void rurp_set_communication_mode() {
    // PD0 doubles as UART RX and data-bus bit 0. In programmer mode it is driven
    // as an output holding the last data byte's bit 0. Clearing DDRD bit 0 and
    // calling Serial.begin() immediately enables RXEN0 while PD0 may still be LOW,
    // which the UART samples as a START BIT and queues spurious bytes into the RX
    // ring. The host then reads those concatenated with the legitimate reply and
    // the parser times out.
    //
    // So: set PORTD bit 0 HIGH *before* clearing DDRD bit 0 -- the pin is actively
    // driven high, and the internal pull-up holds it high through the transition,
    // so the UART sees idle. Then drain any leaked RX bytes after Serial.begin().
    PORTD |= 0x01;
    DDRD &= ~(0x01);
    rurp_serial_begin(MONITOR_SPEED);
    while (SERIAL_PORT.available()) SERIAL_PORT.read();
    com_mode = true;

    // Flush frames deferred during the programmer-mode window now that the UART is
    // up and the wire is safe to drive.
    for (uint8_t i = 0; i < deferred_count; i++) {
        _firestarter_emit_frame(deferred_log[i].id, deferred_log[i].params, deferred_log[i].len);
    }
    deferred_count = 0;
}

void rurp_set_programmer_mode() {
    com_mode = false;
    rurp_serial_end();
    DDRD |= 0x01;
}


// Uno strong override of rurp_log_id. The com_mode gate is critical:
// emitting on the wire while PORTD is repurposed as the data bus would
// corrupt the programming pulse.
void rurp_log_id(uint8_t id, const uint8_t* params, uint8_t param_count) {
    if (com_mode) {
        _firestarter_emit_frame(id, params, param_count);
        return;
    }
    // Programmer mode: the wire is the data bus. Defer the frame and flush it in
    // rurp_set_communication_mode. A frame with more parameters than a slot
    // holds is dropped: truncating it would send a valid-CRC frame with wrong data.
    if (param_count > DEFERRED_PARAM_MAX) {
        return;
    }
    if (deferred_count < DEFERRED_LOG_MAX) {
        deferred_log[deferred_count].id = id;
        deferred_log[deferred_count].len = param_count;
        for (uint8_t i = 0; i < param_count; i++) {
            deferred_log[deferred_count].params[i] = params[i];
        }
        deferred_count++;
    }
    // else: buffer full; drop the frame rather than risk emitting on the active bus.
}

// Uno strong override for rurp_log_id_wide (MSG_DATA_CHUNK path).
// Same com_mode discipline as rurp_log_id; calls the wide frame emitter.
void rurp_log_id_wide(uint8_t id, const uint8_t* params, uint16_t param_count) {
    if (com_mode) {
        _firestarter_emit_frame_wide(id, params, param_count);
    }
}


void rurp_set_control_pin(uint8_t pin, uint8_t state) {
    // This function modifies only the specified control pin on PORTB,
    // leaving other bits (like the USER_BUTTON pull-up) untouched.
    if (state) {
        PORTB |= pin;
    } else {
        PORTB &= ~pin;
    }
}

uint8_t rurp_user_button_pressed() {
    return (PINB & USER_BUTTON) == 0;
}

void rurp_write_data_buffer(uint8_t data) {
    rurp_set_data_output();
    PORTD = data;
}

uint8_t rurp_read_data_buffer() {
    return PIND;
}

void rurp_set_data_output() {
    DDRD = 0xff;
}

void rurp_set_data_input() {
    // Clear PORTD before switching to input so internal pullups are disabled
    // on every data line. Without this, residual PORTD bits from the last
    // register-strobe or rurp_set_communication_mode (PORTD bit 0 = 1) leave
    // 1..2 data pins weakly biased HIGH against the chip's drive.
    PORTD = 0x00;
    DDRD = 0x00;
}

// Structured debug emit routes through the main serial port as id-frames
// (LOG_DEBUG_ID_SUB* in logging_id.h) rather than through a separate
// soft-serial debug channel.
#endif
