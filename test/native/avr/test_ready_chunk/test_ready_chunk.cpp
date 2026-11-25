/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Native coverage for fs_ready_chunk_size() (include/ready_chunk.h): the
 * chunk size that the MSG_OK_READY ack advertises for one command.
 */

#include <unity.h>

#include <stdint.h>

#include "ready_chunk.h"

static const uint16_t kBuffers[] = {512, 1024, 1280, 1536, 1792};
static const uint16_t kPages[] = {1, 2, 64, 128, 256, 512};

void setUp(void) {}
void tearDown(void) {}

// A page-family chunk is whole pages, never more than the buffer, and loses
// less than one page.
static void assert_whole_pages(uint16_t buffer, uint16_t page, uint16_t chunk) {
    TEST_ASSERT_EQUAL_UINT16(0, chunk % page);
    TEST_ASSERT_TRUE(chunk <= buffer);
    TEST_ASSERT_TRUE(chunk > buffer - page);
    TEST_ASSERT_TRUE(chunk > 0);
}

static void test_flash_5v_page_family_rounds_to_whole_pages(void) {
    const uint8_t protos[] = {PROTO_FLASH_5V_PAGE, PROTO_PHANTOM_0x35, PROTO_PHANTOM_0x39};
    for (uint8_t proto : protos) {
        for (uint16_t buffer : kBuffers) {
            for (uint16_t page : kPages) {
                assert_whole_pages(buffer, page, fs_ready_chunk_size(buffer, proto, page));
            }
        }
    }
}

static void test_flash_5v_page_known_values(void) {
    TEST_ASSERT_EQUAL_UINT16(1024, fs_ready_chunk_size(1280, PROTO_FLASH_5V_PAGE, 512));
    TEST_ASSERT_EQUAL_UINT16(1280, fs_ready_chunk_size(1280, PROTO_FLASH_5V_PAGE, 256));
    TEST_ASSERT_EQUAL_UINT16(1280, fs_ready_chunk_size(1280, PROTO_FLASH_5V_PAGE, 128));
    TEST_ASSERT_EQUAL_UINT16(1536, fs_ready_chunk_size(1792, PROTO_FLASH_5V_PAGE, 512));
    TEST_ASSERT_EQUAL_UINT16(1792, fs_ready_chunk_size(1792, PROTO_FLASH_5V_PAGE, 256));
    TEST_ASSERT_EQUAL_UINT16(512, fs_ready_chunk_size(512, PROTO_FLASH_5V_PAGE, 512));
}

// An invalid page size gives the full buffer. flash_5v_page_write_execute
// refuses that command with MSG_ERR_FL4_PAGE_SIZE before it writes.
static void test_flash_5v_page_invalid_page_gives_full_buffer(void) {
    const uint16_t invalid[] = {0, 3, 100, 513, 1024, 0xFFFF};
    for (uint16_t page : invalid) {
        TEST_ASSERT_EQUAL_UINT16(1280, fs_ready_chunk_size(1280, PROTO_FLASH_5V_PAGE, page));
        TEST_ASSERT_EQUAL_UINT16(1792, fs_ready_chunk_size(1792, PROTO_PHANTOM_0x39, page));
    }
}

static void test_eeprom28c_rounds_to_whole_pages(void) {
    for (uint16_t buffer : kBuffers) {
        for (uint16_t page : kPages) {
            assert_whole_pages(buffer, page, fs_ready_chunk_size(buffer, PROTO_EEPROM_PARALLEL, page));
        }
    }
    TEST_ASSERT_EQUAL_UINT16(1024, fs_ready_chunk_size(1280, PROTO_EEPROM_PARALLEL, 512));
    TEST_ASSERT_EQUAL_UINT16(1792, fs_ready_chunk_size(1792, PROTO_EEPROM_PARALLEL, 128));
}

// An absent or invalid page size uses the 64 B fallback of eeprom28c_page_mask().
static void test_eeprom28c_invalid_page_uses_64_byte_fallback(void) {
    const uint16_t invalid[] = {0, 3, 513, 1024};
    for (uint16_t page : invalid) {
        TEST_ASSERT_EQUAL_UINT16(1280, fs_ready_chunk_size(1280, PROTO_EEPROM_PARALLEL, page));
        // 1248 and 1270 are not multiples of 64: both round down to 1216.
        TEST_ASSERT_EQUAL_UINT16(1216, fs_ready_chunk_size(1248, PROTO_EEPROM_PARALLEL, page));
        TEST_ASSERT_EQUAL_UINT16(1216, fs_ready_chunk_size(1270, PROTO_EEPROM_PARALLEL, page));
    }
}

// Byte-by-byte families use the full buffer, whatever page size the frame carries.
static void test_other_protocols_get_full_buffer(void) {
    const uint8_t protos[] = {0x00,
                              PROTO_FLASH_NOR_UNLOCK,
                              PROTO_EPROM_28PIN,
                              PROTO_EPROM_32PIN,
                              PROTO_EPROM_24PIN,
                              PROTO_FLASH_INTEL,
                              PROTO_SRAM_32PIN,
                              PROTO_SRAM_24PIN,
                              PROTO_SRAM_28PIN,
                              PROTO_SRAM_32PIN_NVRAM};
    for (uint8_t proto : protos) {
        for (uint16_t buffer : kBuffers) {
            TEST_ASSERT_EQUAL_UINT16(buffer, fs_ready_chunk_size(buffer, proto, 0));
            TEST_ASSERT_EQUAL_UINT16(buffer, fs_ready_chunk_size(buffer, proto, 512));
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_flash_5v_page_family_rounds_to_whole_pages);
    RUN_TEST(test_flash_5v_page_known_values);
    RUN_TEST(test_flash_5v_page_invalid_page_gives_full_buffer);
    RUN_TEST(test_eeprom28c_rounds_to_whole_pages);
    RUN_TEST(test_eeprom28c_invalid_page_uses_64_byte_fallback);
    RUN_TEST(test_other_protocols_get_full_buffer);
    return UNITY_END();
}
