/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __JSON_PARSER_H__
#define __JSON_PARSER_H__

#include "firestarter.h"
#include "jsmn.h"
#include "rurp_shield.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NUMBER_JSNM_TOKENS 64

    uint8_t json_get_cmd(const char* json, jsmntok_t* tokens, int token_count, firestarter_handle_t* handle);
    int json_parse(const char* json, jsmntok_t* tokens, int token_count, firestarter_handle_t* handle);
    /*
 * json_parse_config result bits. Negative is still a parse error; 0 still
 * means "nothing to do". A bitmask rather than a single 1 because one command
 * can both change a stored field and request a calibration.
 */
#define JSON_CFG_FIELD_CHANGED 1
#define JSON_CFG_CALIBRATE 2

/* The measured supply voltage from the last parsed config command, in mV. */
long json_config_measured_vcc_mv(void);

int json_parse_config(const char* json, jsmntok_t* tokens, int token_count, rurp_configuration_t* config, firestarter_handle_t* handle);

#ifdef __cplusplus
}
#endif


#endif // __JSON_PARSER_H__