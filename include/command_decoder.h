/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __COMMAND_DECODER_H__
#define __COMMAND_DECODER_H__

#include "command_frame.h"
#include "firestarter.h"
#include "rurp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* cmd_decode results. Any other value is a CF_REFUSE_* code. */
#define CMD_DECODE_OK 0
#define CMD_DECODE_BAD_FRAME 0xFF   /* short frame or wrong magic */
#define CMD_DECODE_BAD_VERSION 0xFE /* right magic, another frame version */

/* cmd_apply_config result bits. */
#define CMD_CFG_FIELD_CHANGED 1
#define CMD_CFG_CALIBRATE 2

/* Decode the binary command frame in handle->data_buffer[0..data_size).
 *
 * Sets handle->cmd and handle->ctrl_flags from the header. A memory payload
 * is validated in full before any handle field is written, so a refused
 * frame leaves every command parameter as it was. An accepted frame without
 * a memory payload clears protocol and pulse_delay, so the MSG_OK_READY write
 * budget is zero for it. A config payload is validated here and applied by
 * cmd_apply_config. */
uint8_t cmd_decode(firestarter_handle_t* handle);

/* Write the config values the frame carries into `config`. Call only after
 * cmd_decode accepted a CMD_CONFIG frame. Returns CMD_CFG_* bits. */
uint8_t cmd_apply_config(const firestarter_handle_t* handle, rurp_configuration_t* config);

/* The measured supply voltage a calibrating config frame carries, in mV. */
uint16_t cmd_config_measured_vcc_mv(const firestarter_handle_t* handle);

#ifdef __cplusplus
}
#endif

#endif  // __COMMAND_DECODER_H__
