/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Firestarter -- v1.2 log-message catalog (C++ firmware side)
 *
 * DO NOT EDIT, and do not regenerate from this repository.
 * Generated in the henols/firestarter meta repo by its
 * tools/catalog/codegen.py from tools/catalog/messages.toml,
 * then synced here. Edit the catalog there and re-sync.
 *
 * Catalog version: 1
 * Total messages: 79
 */

#ifndef __MESSAGES_H__
#define __MESSAGES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <avr/pgmspace.h>

// --- Severity codes (mirrors host catalog) ---
#define MSG_SEVERITY_OK    0x01
#define MSG_SEVERITY_INIT  0x02
#define MSG_SEVERITY_MAIN  0x03
#define MSG_SEVERITY_END   0x04
#define MSG_SEVERITY_INFO  0x05
#define MSG_SEVERITY_WARN  0x06
#define MSG_SEVERITY_ERROR 0x07
#define MSG_SEVERITY_DATA  0x08

// --- Message IDs (sorted ascending) ---
#define MSG_NONE                          0x00
#define MSG_OK_READY                      0x01
#define MSG_OK_REQ_DATA                   0x02
#define MSG_OK_FW_VERSION                 0x03
#define MSG_OK_REV                        0x04
#define MSG_OK_CFG                        0x05
#define MSG_INIT_DONE                     0x10
#define MSG_MAIN_DONE                     0x20
#define MSG_END_DONE                      0x30
#define MSG_INFO_MAIN_START               0x40
#define MSG_INFO_MAIN_DONE                0x41
#define MSG_INFO_INIT_START               0x42
#define MSG_INFO_END_START                0x43
#define MSG_INFO_RETRIES                  0x51
#define MSG_INFO_REG_HEADER               0x52
#define MSG_INFO_BIT_HEADER               0x53
#define MSG_INFO_BIT_STR                  0x54
#define MSG_INFO_CE_OE                    0x55
#define MSG_INFO_ADDR                     0x56
#define MSG_INFO_ADDR_REMAP               0x57
#define MSG_INFO_SKIPPING_ERASE           0x58
#define MSG_INFO_SKIPPING_ERASE_MEM       0x59
#define MSG_INFO_FW                       0x5A
#define MSG_INFO_HW                       0x5B
#define MSG_INFO_PHYSICAL_HW              0x5C
#define MSG_INFO_CMD                      0x5D
#define MSG_INFO_SDP_UNLOCK               0x5E
#define MSG_INFO_SDP_UNLOCK_DONE_US       0x5F
#define MSG_INFO_SDP_LOCK                 0x60
#define MSG_INFO_SDP_LOCK_DONE_US         0x61
#define MSG_INFO_PAGE_LOAD_WORST_US       0x62
#define MSG_WARN_REV0_VPP_UNSUPPORTED     0x80
#define MSG_WARN_VPP_LOW                  0x81
#define MSG_WARN_VPP_HIGH                 0x82
#define MSG_WARN_CHIP_ID_MISMATCH         0x83
#define MSG_WARN_MEM_SIZE_TOO_SMALL       0x84
#define MSG_WARN_FL4_BOOT_BLOCK_LOCKED    0x85
#define MSG_WARN_SDP_UNLOCK_SKIPPED       0x86
#define MSG_WARN_SDP_TBLC_EXCEEDED        0x87
#define MSG_ERR_BAD_JSON                  0xA0
#define MSG_ERR_NO_CMD                    0xA1
#define MSG_ERR_SETUP                     0xA2
#define MSG_ERR_PARSE_CFG                 0xA3
#define MSG_ERR_EMPTY_INPUT               0xA4
#define MSG_ERR_NOT_SUPPORTED             0xA5
#define MSG_ERR_NO_CHIP_ID                0xA6
#define MSG_ERR_OUT_OF_RANGE              0xA7
#define MSG_ERR_TIMEOUT                   0xA8
#define MSG_ERR_DATA_ERR_N                0xA9
#define MSG_ERR_CMD_TIMEOUT               0xAA
#define MSG_ERR_UNKNOWN_CMD               0xAB
#define MSG_ERR_REV0_VPP_RD               0xAC
#define MSG_ERR_CMD                       0xAD
#define MSG_ERR_PULSE_TOO_WIDE            0xAE
#define MSG_ERR_VERIFY                    0xAF
#define MSG_ERR_NOT_BLANK                 0xB0
#define MSG_ERR_WRITE_FAILED              0xB1
#define MSG_ERR_EEPROM_TIMEOUT            0xB2
#define MSG_ERR_FL4_VERIFY_TIMEOUT        0xB3
#define MSG_ERR_INTEL_VPP                 0xB4
#define MSG_ERR_INTEL_PROGRAM             0xB5
#define MSG_ERR_INTEL_SR_TIMEOUT          0xB6
#define MSG_ERR_OP_TIMEOUT                0xB7
#define MSG_ERR_VPP_HIGH                  0xB8
#define MSG_ERR_CHIP_ID_MISMATCH          0xB9
#define MSG_ERR_MEM_SIZE_TOO_SMALL        0xBA
#define MSG_ERR_PROTOCOL_NOT_IMPLEMENTED  0xBB
#define MSG_ERR_FL4_BOOT_BLOCK_LOCKED     0xBC
#define MSG_ERR_MAX_PULSES                0xBD
#define MSG_ERR_ENERGY_CAP                0xBE
#define MSG_ERR_FL4_PAGE_SIZE             0xBF
#define MSG_ERR_FL4_PAGE_ALIGN            0xC0
#define MSG_DATA_PROGRESS                 0xE0
#define MSG_DATA_PROTECTION_STATUS        0xE1
#define MSG_DATA_SENDING                  0xE2
#define MSG_DATA_VPP_VOLTAGE              0xE4
#define MSG_DATA_VPE_VOLTAGE              0xE5
#define MSG_DATA_CHUNK                    0xE6
#define MSG_DEBUG                         0xF0

// --- Debug sub-IDs (sorted ascending) ---
#define DBG_FIRESTARTER_STARTED      0x00
#define DBG_FIRMWARE_VERSION         0x01
#define DBG_HARDWARE_REVISION        0x02
#define DBG_PARSE_JSON               0x03
#define DBG_CMD                      0x04
#define DBG_SETUP                    0x05
#define DBG_CMD_FINISHED             0x06
#define DBG_WRITE_EPROM              0x07
#define DBG_VERIFY_PROM              0x08
#define DBG_ERASE_PROM               0x09
#define DBG_CHECK_CHIP_ID_OP         0x0A
#define DBG_BLANK_CHECK_PROM         0x0B
#define DBG_INIT_READ_VOLTAGE        0x0C
#define DBG_SETTING_UP_VPP           0x0D
#define DBG_SETTING_UP_VPE           0x0E
#define DBG_GET_FW_VERSION           0x0F
#define DBG_GET_HW_VERSION           0x10
#define DBG_GET_CONFIG               0x11
#define DBG_CONFIGURING_EPROM        0x12
#define DBG_CHECK_CHIP_ID            0x13
#define DBG_ERASE                    0x14
#define DBG_PULSE_DELAY_MISMATCH     0x15
#define DBG_GET_CHIP_ID              0x16
#define DBG_CHECK_VPP                0x17
#define DBG_CHECKING_VPP_VOLTAGE     0x18
#define DBG_INTERNAL_ERASE           0x19
#define DBG_CHECK_VPP_INTEL          0x1A
#define DBG_CONFIGURING_INTEL_FLASH  0x1B
#define DBG_ERASE_COMPLETE           0x1C
#define DBG_CONFIGURING_FLASH        0x1D
#define DBG_SKIPPING_ERASE_MEMORY    0x1E
#define DBG_SECTOR_ERASE             0x1F
#define DBG_CHIP_ERASE               0x20
#define DBG_CONFIGURING_FLASH4       0x21
#define DBG_CONFIGURING_SRAM         0x22
#define DBG_CONFIGURING_MEMORY       0x23
#define DBG_ADDRESS                  0x24
#define DBG_TOP_MSB_LSB              0x25
#define DBG_READING_FROM_ADDRESS     0x26
#define DBG_CONFIGURING_EEPROM_28C   0x27
#define DBG_CHECK_CHIP_ID_28C        0x28
#define DBG_BUF_VAL                  0x29
#define DBG_TOKEN_COUNT              0x2A
#define DBG_FLAG_FORCE               0x2B
#define DBG_FLAG_CAN_ERASE           0x2C
#define DBG_FLAG_SKIP_ERASE          0x2D
#define DBG_FLAG_SKIP_BLANK          0x2E
#define DBG_FLAG_VPE_AS_VPP          0x2F
#define DBG_FLAG_OUTPUT_EN           0x30
#define DBG_FLAG_CHIP_EN             0x31
#define DBG_BUFFER_SIZE              0x32
#define DBG_MEM_SIZE                 0x33
#define DBG_ADDR_MASK                0x34
#define DBG_MATCH_LINES              0x35

#ifdef __cplusplus
}
#endif

#endif  // __MESSAGES_H__
