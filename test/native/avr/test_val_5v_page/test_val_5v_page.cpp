/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Tests the 0x05 family (0x05, 0x35, 0x39) through the recording bus stub:
 * configure_memory sends these protocols to configure_flash_5v_page, and the
 * page write, the chip erase and the lock-status read drive the expected
 * {address, data} command cycles and set no high-voltage control bit.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

#include <vector>
#include <cstdint>
#include <cstdio>

extern "C" {
#include "memory.h"
}
#include "operation_utils.h"
#include "firestarter.h"
#include "flash_utils.h"
#include "flash_5v_page.h"
#include "messages.h"
#include "rurp_pinout.h"

using namespace fakeit;

/* DIP32_SST39SF040, the pin map of every 32-pin 0x05/0x06 row, as the host
 * sends it: A0-A16 on bus lines 0-16, A17 on line 20, A18 on line 21, and WE
 * (pin 31) on line 22, the CTRL_READ_WRITE bit. Command cycles take the full
 * remap path, and a zeroed bus_config is not an identity map: it folds every
 * address onto bus line 0. */
static const bus_config_t DIP32_FLASH_BUS = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
      0x10, 0x14, 0x15, 0xFF },
    0x0031FFFFUL,
    17,
    22,
    0xFF,
    0x00000000UL
};

#include "../_shared/bus_recording.h"
#include "../_shared/dispatch_expect.h"

static std::vector<uint8_t> s_wire_bytes;
static uint32_t s_millis = 0;

static uint8_t s_stub_raw_value = 0;
static uint8_t stub_get_data_return_fixed(firestarter_handle_t* handle, uint32_t address) {
    (void)handle; (void)address;
    return s_stub_raw_value;
}

void setUp(void) {
    ArduinoFakeReset();
    s_wire_bytes.clear();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            s_wire_bytes.push_back(b);
            return (size_t)1;
        });
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    s_millis = 0;
    When(Method(ArduinoFake(), millis)).AlwaysDo([]() -> unsigned long { return s_millis++; });
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    clear_bus_recording();
}

void tearDown(void) {}

static firestarter_handle_t make_handle(uint32_t protocol, uint8_t cmd) {
    firestarter_handle_t h = {};
    h.bus_config = DIP32_FLASH_BUS;
    h.protocol   = protocol;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0; /* skip chip-id branch */
    h.mem_size   = 524288; /* 512 KB (SST39SF040) */
    return h;
}

/* Asserts that configure_memory sends `protocol` to configure_flash_5v_page,
 * for every command that family handles. */
static void check_dispatches_to_5v_page(uint32_t protocol) {
    static const uint8_t cmds[] = {CMD_READ, CMD_WRITE, CMD_ERASE, CMD_CHECK_CHIP_ID, CMD_LOCK_STATUS};
    for (size_t c = 0; c < sizeof(cmds); c++) {
        char ctx[80];
        snprintf(ctx, sizeof(ctx), "protocol 0x%02X cmd %u -> configure_flash_5v_page", (unsigned)protocol,
                 (unsigned)cmds[c]);
        assert_dispatches_to(make_handle(protocol, cmds[c]), configure_flash_5v_page, ctx);
    }
}

void test_5v_page_0x05_dispatches_to_5v_page(void) {
    check_dispatches_to_5v_page(0x05);
}

void test_5v_page_0x35_dispatches_to_5v_page(void) {
    check_dispatches_to_5v_page(0x35);
}

void test_5v_page_0x39_dispatches_to_5v_page(void) {
    check_dispatches_to_5v_page(0x39);
}

/*
 * These two tests exercise flash_5v_page_write_execute (the operation phase, not just
 * configure), using the recording-bus stub to observe side effects.
 *
 * Test setup: configure_memory(CMD_WRITE) wires function pointers including
 * firestarter_set_data = memory_set_data and the operation_main pointer.
 * Then clear_bus_recording() resets the capture, fill data_buffer with zeros
 * (so flash_5v_page_wait_for_page_write's DQ7 poll passes in one iteration since the
 * stub's rurp_read_data_buffer() always returns 0 = expected), and call
 * h.firestarter_operation_main(&h) to drive flash_5v_page_write_execute.
 *
 * The recording captures every rurp_write_to_register call:
 *   - flash_util_byte_flipping (SDP sequence) writes CONTROL_REGISTER
 *     (CTRL_READ_WRITE) + LSB/MSB for each command address pair.
 *   - memory_set_data writes LSB/MSB/CONTROL via mem_util_set_address.
 *
 * Test 1 (SDP emission, RED before fix): scans for the FLASH_ENABLE_WRITE
 * address signature — MSB writes of 0x55 (for 0x5555 and 0x5555 again) and
 * 0x2A (for 0x2AAA) in sequence before the first data address write. FAILS
 * today because flash_5v_page_write_execute has no flash_execute_command(FLASH_ENABLE_WRITE).
 *
 * Test 2 (operation-phase VPP-safety): asserts that no HV control bit appears
 * in any CONTROL_REGISTER write during the write-execute call.
 */

static firestarter_handle_t make_write_handle_with_data(void) {
    firestarter_handle_t h = {};
    h.bus_config = DIP32_FLASH_BUS;
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0; /* skip chip-id branch in write_init */
    h.mem_size   = 524288; /* 512 KB (W29C040) */
    h.page_size  = 256; /* W29C040's real page */
    h.address    = 0;
    h.data_size  = 256;
    /* data_buffer is zero-initialized by {} */
    /* ctrl_flags = 0: no FLAG_CAN_ERASE, and write-init performs no blank
     * check at all any more regardless of ctrl_flags -- we bypass init and
     * call operation_main directly here anyway. */
    return h;
}

/* Drives flash_5v_page_write_init through the dispatched pointer, with
 * FLAG_CAN_ERASE clear (ctrl_flags = 0): the blank-check conditional this
 * fixture used to gate is deleted outright now, so write-init performs no
 * blank check regardless of ctrl_flags. mem_size is a small 2048, a value kept
 * from when this fixture needed to be smaller than the firmware's
 * then-existing blank-check chunk size; that machinery left the firmware
 * in 3.1.0, but the small mem_size is harmless to retain since the oracle
 * below never depended on how large mem_size is. */
static firestarter_handle_t make_write_init_handle_blank_check_enabled(void) {
    firestarter_handle_t h = {};
    h.bus_config = DIP32_FLASH_BUS;
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0; /* skip chip-id branch in write_init */
    h.mem_size   = 2048;
    h.address    = 0;
    h.data_size  = 0;
    /* ctrl_flags = 0: FLAG_CAN_ERASE clear. The blank-check axis this
     * comment used to describe as "live" no longer exists in write-init at
     * all -- see the factory's own doc comment above. */
    return h;
}

static firestarter_handle_t make_write_init_handle_can_erase_set(void) {
    firestarter_handle_t h = {};
    h.bus_config = DIP32_FLASH_BUS;
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 2048;
    h.address    = 0;
    h.data_size  = 0;
    h.ctrl_flags = FLAG_CAN_ERASE;
    return h;
}

/* Helper: scan recording for FLASH_ENABLE_WRITE address signature.
 * FLASH_ENABLE_WRITE addresses: 0x5555, 0x2AAA, 0x5555.
 * Each command cycle latches LSB=addr&0xFF and MSB=(addr>>8)&0xFF (identity
 * on DIP32_FLASH_BUS).
 * Signature MSB pattern at start: 0x55, 0x2A, 0x55 in consecutive MSB writes.
 * Returns true if found, false if not. */
/* Counts every occurrence of the SDP MSB signature in the recording and
 * reports the recorded index of each, via an optional out-array. Immune to
 * register-write elision because the three unlock addresses (0x5555, 0x2AAA,
 * 0x5555) differ from each other and from every data address. */
static int count_sdp_signatures(int* out_indices, int max_out) {
    int count = 0;
    int msb_seq_index = 0;
    const uint8_t msb_pattern[3] = {0x55, 0x2A, 0x55};
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == MOST_SIGNIFICANT_BYTE) {
            if (recorded_data(i) == msb_pattern[msb_seq_index]) {
                msb_seq_index++;
                if (msb_seq_index == 3) {
                    if (out_indices != NULL && count < max_out) {
                        out_indices[count] = i;
                    }
                    count++;
                    msb_seq_index = 0;
                }
            } else {
                /* Reset if sequence breaks (partial match then mismatch) */
                msb_seq_index = (recorded_data(i) == msb_pattern[0]) ? 1 : 0;
            }
        }
    }
    return count;
}

static bool recording_contains_sdp_signature(void) {
    return count_sdp_signatures(NULL, 0) > 0;
}

/* Test 1 (FIX-02B SDP): flash_5v_page_write_execute must emit FLASH_ENABLE_WRITE
 * SDP 3-byte sequence at the start of each page load.
 * RED before fix (no flash_execute_command(FLASH_ENABLE_WRITE) in write path). */
void test_5v_page_write_execute_emits_sdp(void) {
    firestarter_handle_t h = make_write_handle_with_data();
    configure_memory(&h);
    clear_bus_recording(); /* reset after configure_memory's set_address call */

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "flash_5v_page_write_execute must not error on a full-page zero write");
    TEST_ASSERT_TRUE_MESSAGE(recording_contains_sdp_signature(),
        "flash_5v_page_write_execute must emit FLASH_ENABLE_WRITE SDP (0x5555,0x2AAA,0x5555 MSB pattern) at page start");
    static const expected_cycle_t want[] = {
        {BUS_CYCLE_WRITE, 0x5555, 0xAA}, {BUS_CYCLE_WRITE, 0x2AAA, 0x55}, {BUS_CYCLE_WRITE, 0x5555, 0xA0},
        {BUS_CYCLE_WRITE, 0x0000, 0x00},
    };
    assert_bus_cycles_from(0, want, 4, "the page unlock AA/55/A0, then the first data byte");
}

/* Test 2 (FIX-02B VPP-safety operation phase): flash_5v_page_write_execute must NEVER
 * set an HV control bit in any CONTROL_REGISTER write during the write-execute call.
 * Passes for the bare loop today; MUST remain green after the SDP fix since
 * flash_util_byte_flipping only sets CTRL_READ_WRITE (not VPP bits). */
void test_5v_page_write_execute_no_vpp(void) {
    firestarter_handle_t h = make_write_handle_with_data();
    configure_memory(&h);
    clear_bus_recording();

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "flash_5v_page_write_execute must not error on a full-page zero write");
    assert_no_vpp_in_recording(
        "flash_5v_page_write_execute (operation phase) must NOT set any VPP-enable CTL bit");
}

/* Configures a 0x05 CMD_WRITE of `data_size` bytes at `address`, clears the
 * recording after configure_memory's own set_address, and runs
 * operation_main once. data_buffer is zero-initialized, so the write poll
 * converges on its first iteration. */
static firestarter_handle_t drive_page_write(uint32_t mem_size, uint16_t page_size,
                                             uint32_t address, uint32_t data_size) {
    firestarter_handle_t h = {};
    h.bus_config = DIP32_FLASH_BUS;
    h.protocol       = 0x05;
    h.cmd            = CMD_WRITE;
    h.response_code  = RESPONSE_CODE_OK;
    h.chip_id        = 0;
    h.mem_size       = mem_size;
    h.page_size      = page_size;
    h.address        = address;
    h.data_size      = data_size;
    configure_memory(&h);
    clear_bus_recording();

    h.firestarter_operation_main(&h);
    return h;
}

/* Asserts that the recorded write opened exactly two pages, and that the
 * second page start came only after at least page_size bytes. */
static void assert_page_starts(const firestarter_handle_t& h, int expected,
                               uint16_t page_size) {
    int indices[8];
    int sig_count = count_sdp_signatures(indices, 8);
    TEST_ASSERT_FALSE_MESSAGE(bus_recording_saturated(),
        "recorder saturated -- the boundary assertion below would be vacuous");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "non-vacuity control: the write must have actually run");
    TEST_ASSERT_EQUAL_MESSAGE(expected, sig_count,
        "one SDP signature per page the span covers -- a capacity-derived page would produce a different count");
    if (expected > 1) {
        TEST_ASSERT_TRUE_MESSAGE(indices[1] >= (int)(3 * page_size),
            "the second page start must not be observable before at least page_size bytes were processed -- an under-sized derived page fires it earlier");
    }
}

/* Drives a write that a bad page size must refuse with zero register writes.
 * Refusing without performing no write would still corrupt the device
 * silently. */
static void run_page_size_refusal_case(uint16_t page_size) {
    firestarter_handle_t h = drive_page_write(65536, page_size, 0, 128);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "a rejected page size must refuse");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "a refused write must perform zero register writes");
}

/* W29C512's real geometry is (mem_size 65536, page_size 128). A
 * capacity-derived page size would have picked 64 here, producing a second
 * page start at address 64 -- a second SDP signature. Exactly one signature
 * over 128 bytes proves the firmware used the real 128-byte page. */
void test_5v_page_write_execute_page_starts_at_real_page_not_derived(void) {
    assert_page_starts(drive_page_write(65536, 128, 0, 128), 1, 128);
}

/* Refusal counterpart: the same geometry with page_size 0 (absent). */
void test_5v_page_write_execute_refuses_with_no_page_size(void) {
    run_page_size_refusal_case(0);
}

void test_5v_page_write_execute_refuses_unaligned_start(void) {
    firestarter_handle_t h = drive_page_write(262144, 128, 64, 128);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "a chunk starting one byte inside a page must refuse");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "a refused unaligned chunk must perform zero register writes");
}

void test_5v_page_write_execute_accepts_page_exact_write(void) {
    firestarter_handle_t h = drive_page_write(262144, 128, 128, 128);

    int indices[8];
    int sig_count = count_sdp_signatures(indices, 8);
    TEST_ASSERT_FALSE_MESSAGE(bus_recording_saturated(),
        "recorder saturated -- the assertions below would be vacuous");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "a page-exact write at a non-zero page-aligned start must succeed");
    TEST_ASSERT_EQUAL_MESSAGE(1, sig_count,
        "exactly one SDP signature from the single page start");
}

void test_5v_page_write_execute_refuses_partial_length(void) {
    firestarter_handle_t h = drive_page_write(262144, 128, 0, 64);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "a page-aligned start with a partial-length chunk must refuse");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "a refused partial-length chunk must perform zero register writes");
}

void test_5v_page_write_execute_refuses_unaligned_start_and_partial_length(void) {
    firestarter_handle_t h = drive_page_write(262144, 128, 64, 64);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "both the start and length clauses fire here -- a guard written with && instead of || would pass the single-clause cases and fail only this one");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "a refused chunk with both clauses firing must perform zero register writes");
}

void test_5v_page_write_execute_refuses_single_byte_payload(void) {
    firestarter_handle_t h = drive_page_write(262144, 128, 0, 1);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "one byte is never a whole page on any part whose page size exceeds one byte");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "a refused single-byte payload must perform zero register writes");
}

void test_5v_page_write_execute_accepts_zero_length_chunk(void) {
    firestarter_handle_t h = drive_page_write(262144, 128, 0, 0);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "a zero-length chunk is a whole multiple of every page size and must not be refused");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "a zero-length chunk drives no page cycle at all -- committing a page here would be the defect itself");
}

void test_5v_page_write_execute_boundary_32768_64(void) {
    assert_page_starts(drive_page_write(32768, 64, 0, 128), 2, 64);
}

void test_5v_page_write_execute_boundary_65536_128(void) {
    assert_page_starts(drive_page_write(65536, 128, 0, 256), 2, 128);
}

void test_5v_page_write_execute_boundary_131072_128(void) {
    assert_page_starts(drive_page_write(131072, 128, 0, 256), 2, 128);
}

void test_5v_page_write_execute_boundary_262144_128(void) {
    assert_page_starts(drive_page_write(262144, 128, 0, 256), 2, 128);
}

void test_5v_page_write_execute_boundary_262144_256(void) {
    assert_page_starts(drive_page_write(262144, 256, 0, 512), 2, 256);
}

void test_5v_page_write_execute_boundary_524288_256(void) {
    assert_page_starts(drive_page_write(524288, 256, 0, 512), 2, 256);
}

/* Two page-sized calls, so the two page starts span the call boundary. */
void test_5v_page_write_execute_boundary_524288_512(void) {
    firestarter_handle_t h = drive_page_write(524288, 512, 0, 512);

    h.address   = 512;
    h.data_size = 512;
    h.firestarter_operation_main(&h);

    assert_page_starts(h, 2, 512);
}

void test_5v_page_write_execute_refuses_unaligned_second_chunk(void) {
    firestarter_handle_t h = drive_page_write(262144, 128, 0x40, 512);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "the first chunk starts mid-page and must refuse on its own start clause");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "a refused first chunk must perform zero register writes");

    h.response_code = RESPONSE_CODE_OK;
    clear_bus_recording();
    h.address   = 0x40 + 512;
    h.data_size = 512;
    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "the second chunk is unaligned only because the first chunk ended mid-page -- the guard refuses it on its own terms, with no memory of the first chunk's history");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "a refused second chunk must perform zero register writes, so no page already committed by the first chunk is reopened and re-committed across the boundary");
}

void test_5v_page_write_execute_accepts_aligned_two_chunk_p256(void) {
    firestarter_handle_t h = drive_page_write(524288, 256, 0, 512);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "the first aligned chunk must succeed before the second chunk is driven");

    clear_bus_recording();
    h.address   = 512;
    h.data_size = 512;
    h.firestarter_operation_main(&h);

    int indices[8];
    int sig_count = count_sdp_signatures(indices, 8);
    TEST_ASSERT_FALSE_MESSAGE(bus_recording_saturated(),
        "recorder saturated -- the signature count below would be vacuous");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "the second aligned chunk must also succeed");
    TEST_ASSERT_EQUAL_MESSAGE(2, sig_count,
        "one page start per 256-byte page in the 512-byte chunk -- each page is opened exactly once across the chunk boundary");
}

void test_5v_page_write_execute_accepts_aligned_two_chunk_p512(void) {
    firestarter_handle_t h = drive_page_write(524288, 512, 0, 512);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "the first aligned chunk must succeed before the second chunk is driven");

    clear_bus_recording();
    h.address   = 512;
    h.data_size = 512;
    h.firestarter_operation_main(&h);

    int indices[8];
    int sig_count = count_sdp_signatures(indices, 8);
    TEST_ASSERT_FALSE_MESSAGE(bus_recording_saturated(),
        "recorder saturated -- the signature count below would be vacuous");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "the second aligned chunk must also succeed");
    TEST_ASSERT_EQUAL_MESSAGE(1, sig_count,
        "one page start per 512-byte chunk at page size 512 -- the only native coverage of the two 512-byte-page parts");
}

/* Boundaries land on multiples of page_size 128, not the
 * mem_size-524288-derived 256. */
void test_5v_page_write_execute_ignores_mem_size_entirely(void) {
    assert_page_starts(drive_page_write(524288, 128, 0, 256), 2, 128);
}

void test_5v_page_write_execute_refuses_page_size_not_power_of_two(void) {
    run_page_size_refusal_case(96);
}

void test_5v_page_write_execute_refuses_page_size_above_ceiling(void) {
    run_page_size_refusal_case(1024);
}

void test_5v_page_write_execute_refuses_page_size_transport_saturated(void) {
    run_page_size_refusal_case(65535);
}

void test_5v_page_write_init_does_not_blank_check(void) {
    firestarter_handle_t h = make_write_init_handle_blank_check_enabled();
    configure_memory(&h);
    clear_bus_recording();

    h.firestarter_operation_init(&h);

    TEST_ASSERT_FALSE_MESSAGE(is_operation_in_progress(&h),
        "ERASE-02: is_operation_in_progress must be FALSE after exactly one "
        "flash_5v_page_write_init call -- write-init assigns no operation-end "
        "at all any more (the region-scoped blank check that used to gate it "
        "left the firmware in 3.1.0), so this reads FALSE unconditionally "
        "now, not merely on this call");
    /* The companion "must be NULL" assertion on the removed heap-allocated
     * handle field is GONE, and so is the field itself: the region-scoped
     * blank check's saved-address cursor was a file-scope static, never a
     * heap allocation, and it left the firmware in 3.1.0 along with the
     * rest of that machinery, so there is no allocation and no cursor left
     * to observe. This is the loss of a redundant PROBE, not of coverage -- is_operation_in_progress
     * above and the removed allocation used to be unconditionally adjacent
     * statements in the same then-branch of the same if, with no
     * intervening control flow, early return or condition, so a FALSE
     * result there strictly implied the branch -- and therefore the
     * allocation -- never executed. The behaviour under test is still
     * pinned by the assertion above. */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "ERASE-02: the removed blank check can no longer fail a write on a "
        "non-blank part");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "ERASE-02: with FLAG_CAN_ERASE clear and no blank check left in "
        "write-init at all, flash_5v_page_write_init must write no register");
}

void test_5v_page_write_init_no_vpp_with_flag_can_erase_set(void) {
    firestarter_handle_t h = make_write_init_handle_can_erase_set();
    configure_memory(&h);
    clear_bus_recording();

    h.firestarter_operation_init(&h);

    TEST_ASSERT_FALSE_MESSAGE(is_operation_in_progress(&h),
        "flash_5v_page_write_init must leave is_operation_in_progress FALSE "
        "after exactly one call, even with FLAG_CAN_ERASE set");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "flash_5v_page_write_init must not error with FLAG_CAN_ERASE set");
    TEST_ASSERT_EQUAL_MESSAGE(0, bus_recording_count(),
        "flash_5v_page_write_init must write no register even when "
        "FLAG_CAN_ERASE is set: the 0x05 family has no erase in write-init");
}

/* Leg 1 (Dispatch): CMD_LOCK_STATUS must wire firestarter_operation_main to
 * flash_5v_page_read_protection_execute. Unlike flash_nor_unlock.cpp, this
 * file assigns no firestarter_operation_init before the switch, so
 * firestarter_operation_init stays NULL from configure_memory's own
 * top-of-function reset -- there is nothing for the CMD_LOCK_STATUS arm to
 * null. */
void test_5v_page_lock_status_dispatch(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_LOCK_STATUS);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x05 CMD_LOCK_STATUS");
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const void*)flash_5v_page_read_protection_execute,
        (const void*)h.firestarter_operation_main,
        "flash_5v_page: CMD_LOCK_STATUS must wire firestarter_operation_main to flash_5v_page_read_protection_execute");
    TEST_ASSERT_NULL_MESSAGE((const void*)h.firestarter_operation_init,
        "flash_5v_page: CMD_LOCK_STATUS must leave firestarter_operation_init NULL (this file assigns none before the switch)");
}

/* Leg 2 (Sequence pinning): asserts every {address, byte} pair and every
 * named constant Task 1 transcribed from 151-SEQUENCES.md, by symbol. This
 * is a CHANGE DETECTOR, NOT A CORRECTNESS PROOF -- infoic.xml's `config`
 * field is the literal string "NULL" on every 0x05 entry, so there is no
 * machine-readable upstream to diff this pinning against; it can only
 * prove the bytes committed today match the bytes committed yesterday.
 * The address/decode pair here is also this artifact's lowest-confidence
 * citation (151-SEQUENCES.md: sourced by structural analogy, not an
 * independently re-checked page) -- pinning it does not upgrade that. */
void test_5v_page_lock_status_pinned_sequence(void) {
    TEST_ASSERT_EQUAL_HEX32(0x0002UL, (uint32_t)FLASH_5V_PAGE_BOOT_BLOCK_STATUS_ADDR);
    TEST_ASSERT_EQUAL_HEX8(0xFF, FLASH_5V_PAGE_BOOT_BLOCK_UNLOCKED);
    TEST_ASSERT_EQUAL_HEX8(0xFE, FLASH_5V_PAGE_BOOT_BLOCK_LOCKED);

    /* Mode entry/exit is a FINDING (151-SEQUENCES.md): the same AA/55/90 as
     * FLASH_ENABLE_ID -- no distinct Product-ID-mode table exists in this
     * project. Pin every {address, byte} pair, not just presence. */
    TEST_ASSERT_EQUAL_UINT32(0x5555UL, FLASH_ENABLE_ID[0].address);
    TEST_ASSERT_EQUAL_HEX8(0xAA, FLASH_ENABLE_ID[0].byte);
    TEST_ASSERT_EQUAL_UINT32(0x2AAAUL, FLASH_ENABLE_ID[1].address);
    TEST_ASSERT_EQUAL_HEX8(0x55, FLASH_ENABLE_ID[1].byte);
    TEST_ASSERT_EQUAL_UINT32(0x5555UL, FLASH_ENABLE_ID[2].address);
    TEST_ASSERT_EQUAL_HEX8(0x90, FLASH_ENABLE_ID[2].byte);

    TEST_ASSERT_EQUAL_UINT32(0x5555UL, FLASH_DISABLE_ID[0].address);
    TEST_ASSERT_EQUAL_HEX8(0xAA, FLASH_DISABLE_ID[0].byte);
    TEST_ASSERT_EQUAL_UINT32(0x2AAAUL, FLASH_DISABLE_ID[1].address);
    TEST_ASSERT_EQUAL_HEX8(0x55, FLASH_DISABLE_ID[1].byte);
    TEST_ASSERT_EQUAL_UINT32(0x5555UL, FLASH_DISABLE_ID[2].address);
    TEST_ASSERT_EQUAL_HEX8(0xF0, FLASH_DISABLE_ID[2].byte);
}

/* Leg 3 (5 V only): mirrors this suite's own central claim -- no VPP-enable
 * CTL bit may appear in any CONTROL_REGISTER write, this time for the
 * actual CMD_LOCK_STATUS operation, not just its configure phase. */
void test_5v_page_lock_status_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_LOCK_STATUS);
    configure_memory(&h);
    clear_bus_recording();

    flash_5v_page_read_protection_execute(&h);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "flash_5v_page_read_protection_execute must not error");
    assert_no_vpp_in_recording(
        "flash_5v_page_read_protection_execute must NOT set any VPP-enable CTL bit -- this is a 5V read");
}

void test_5v_page_lock_status_raw_byte_fidelity(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_LOCK_STATUS);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x05 CMD_LOCK_STATUS");

    s_stub_raw_value = 0x37; /* matches neither _UNLOCKED (0xFF) nor _LOCKED (0xFE) */
    h.firestarter_get_data = stub_get_data_return_fixed;
    s_wire_bytes.clear();

    flash_5v_page_read_protection_execute(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "an unrecognised raw value must set RESPONSE_CODE_WARNING, never ERROR");
    /* Frame shape: 4 magic + 2 len + 1 id + 2 params + 1 crc + 1 anchor = 11 bytes. */
    TEST_ASSERT_EQUAL_size_t_MESSAGE((size_t)11, s_wire_bytes.size(),
        "expected exactly one 2-param MSG_DATA_PROTECTION_STATUS id-frame");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE((uint8_t)MSG_DATA_PROTECTION_STATUS, s_wire_bytes[6],
        "frame id must be MSG_DATA_PROTECTION_STATUS");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x37, s_wire_bytes[7],
        "byte 0 must equal the stubbed raw value EXACTLY -- never coerced to 0xFF or 0xFE");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, s_wire_bytes[8],
        "byte 1 must be the 0xFF indeterminate sentinel");
}

/* Leg 5 (Mode bracketing): this suite's host_stubs.cpp activates
 * HOST_STUBS_RECORD_BUS, whose rurp_write_to_register override
 * (_shared/host_stubs_common.inc's `#elif defined(HOST_STUBS_RECORD_BUS)`
 * arm) records every call unconditionally -- it does NOT compose with
 * rurp_register_utils.h's cache-compare elision, which lives behind the
 * separate, independent HOST_STUBS_REAL_REGISTER_UTILS opt-in used by
 * other suites (e.g. test_vpp_eprom_v131). Measured: no register write is
 * elided here, so an exact-count/position assertion over the recorded
 * MOST_SIGNIFICANT_BYTE writes is valid for this suite. flash_util_read_
 * in_id_mode's call shape is exactly: 3 MSB writes (FLASH_ENABLE_ID's
 * three byte_flip_t entries) + 1 MSB write (the read's own
 * mem_util_set_address call, via handle->firestarter_get_data) + 3 MSB
 * writes (FLASH_DISABLE_ID) = 7 total, deterministically -- this leg
 * asserts that exact shape, proving entry precedes the read and exit
 * follows it, without needing to know the read's own (address-remap-
 * dependent) MSB value. */
void test_5v_page_lock_status_mode_bracketing(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_LOCK_STATUS);
    configure_memory(&h);
    clear_bus_recording();

    flash_5v_page_read_protection_execute(&h);

    static const expected_cycle_t want[] = {
        {BUS_CYCLE_WRITE, 0x5555, 0xAA}, {BUS_CYCLE_WRITE, 0x2AAA, 0x55}, {BUS_CYCLE_WRITE, 0x5555, 0x90},
        {BUS_CYCLE_READ, 0x0002, 0},
        {BUS_CYCLE_WRITE, 0x5555, 0xAA}, {BUS_CYCLE_WRITE, 0x2AAA, 0x55}, {BUS_CYCLE_WRITE, 0x5555, 0xF0},
    };
    assert_bus_cycles(want, 7, "ID entry (AA/55/90), the status read at 0x0002, then ID exit (AA/55/F0)");
}

/* The page wait polls DQ7 of the last byte loaded, not the whole byte. */
static uint8_t s_part_answer = 0;
static int s_part_reads = 0;
static uint8_t stub_part(firestarter_handle_t* handle, uint32_t address) {
    (void)handle; (void)address;
    s_part_reads++;
    return s_part_answer;
}

static firestarter_handle_t make_one_page_write(uint8_t last_byte) {
    firestarter_handle_t h = {};
    h.bus_config = DIP32_FLASH_BUS;
    h.protocol       = 0x05;
    h.cmd            = CMD_WRITE;
    h.response_code  = RESPONSE_CODE_OK;
    h.mem_size       = 65536;
    h.page_size      = 64;
    h.address        = 0x1000;
    h.data_size      = 64;
    h.data_buffer[63] = (char)last_byte;
    configure_memory(&h);
    h.firestarter_get_data = stub_part;
    s_part_reads = 0;
    return h;
}

void test_5v_page_wait_ends_on_dq7_not_the_whole_byte(void) {
    firestarter_handle_t h = make_one_page_write(0x7F);
    s_part_answer = 0x00; /* DQ7 agrees with 0x7F, the other bits do not */

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "DQ7 alone says the page write ended; the host verify checks the data");
    TEST_ASSERT_EQUAL_MESSAGE(2, s_part_reads, "two consecutive DQ7 matches end the wait");
}

void test_5v_page_wait_times_out_with_the_last_byte_in_the_report(void) {
    firestarter_handle_t h = make_one_page_write(0x7F);
    s_part_answer = 0x80; /* DQ7 stays complemented: still writing */

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_TRUE(s_wire_bytes.size() > 11);
    TEST_ASSERT_EQUAL_HEX8(MSG_ERR_FL4_VERIFY_TIMEOUT, s_wire_bytes[6]);
    /* [expected][u24 address][observed] */
    TEST_ASSERT_EQUAL_HEX8(0x7F, s_wire_bytes[7]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_wire_bytes[8]);
    TEST_ASSERT_EQUAL_HEX8(0x10, s_wire_bytes[9]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, s_wire_bytes[10]);
    TEST_ASSERT_EQUAL_HEX8(0x80, s_wire_bytes[11]);
}

/* Issue #62: a software chip erase for the 0x05 family. */
void test_5v_page_erase_dispatch(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_ERASE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NOT_NULL_MESSAGE((const void*)h.firestarter_operation_main,
        "0x05 must accept CMD_ERASE");
    TEST_ASSERT_NOT_NULL_MESSAGE((const void*)h.firestarter_operation_init,
        "the erase checks the chip ID first when the host sends one");
}

void test_5v_page_erase_sends_the_six_cycle_sequence_and_waits(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_ERASE);
    configure_memory(&h);
    h.firestarter_get_data = stub_part;
    s_part_answer = 0xFF;
    s_part_reads = 0;
    clear_bus_recording();

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
    static const expected_cycle_t want[] = {
        {BUS_CYCLE_WRITE, 0x5555, 0xAA}, {BUS_CYCLE_WRITE, 0x2AAA, 0x55}, {BUS_CYCLE_WRITE, 0x5555, 0x80},
        {BUS_CYCLE_WRITE, 0x5555, 0xAA}, {BUS_CYCLE_WRITE, 0x2AAA, 0x55}, {BUS_CYCLE_WRITE, 0x5555, 0x10},
    };
    assert_bus_cycles(want, 6, "the JEDEC six-cycle chip erase");
    TEST_ASSERT_EQUAL_MESSAGE(3, s_part_reads, "two DQ7 reads, then the erased-byte check");
    assert_no_vpp_in_recording("the 0x05 chip erase is a 5 V operation");
}

void test_5v_page_erase_that_leaves_address_zero_is_not_blank(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_ERASE);
    configure_memory(&h);
    h.firestarter_get_data = stub_part;
    s_part_answer = 0xC3; /* DQ7 high, so the wait ends: a locked boot block */

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_HEX8(MSG_ERR_NOT_BLANK, s_wire_bytes[6]);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_5v_page_wait_ends_on_dq7_not_the_whole_byte);
    RUN_TEST(test_5v_page_wait_times_out_with_the_last_byte_in_the_report);
    RUN_TEST(test_5v_page_erase_dispatch);
    RUN_TEST(test_5v_page_erase_sends_the_six_cycle_sequence_and_waits);
    RUN_TEST(test_5v_page_erase_that_leaves_address_zero_is_not_blank);

    /* configure_memory sends 0x05, 0x35 and 0x39 to configure_flash_5v_page */
    RUN_TEST(test_5v_page_0x05_dispatches_to_5v_page);
    RUN_TEST(test_5v_page_0x35_dispatches_to_5v_page);
    RUN_TEST(test_5v_page_0x39_dispatches_to_5v_page);

    /* FIX-02B: operation-phase SDP emission + VPP-safety proofs */
    RUN_TEST(test_5v_page_write_execute_emits_sdp);
    RUN_TEST(test_5v_page_write_execute_no_vpp);

    RUN_TEST(test_5v_page_write_execute_page_starts_at_real_page_not_derived);
    RUN_TEST(test_5v_page_write_execute_refuses_with_no_page_size);
    RUN_TEST(test_5v_page_write_execute_refuses_unaligned_start);
    RUN_TEST(test_5v_page_write_execute_accepts_page_exact_write);
    RUN_TEST(test_5v_page_write_execute_refuses_partial_length);
    RUN_TEST(test_5v_page_write_execute_refuses_unaligned_start_and_partial_length);
    RUN_TEST(test_5v_page_write_execute_refuses_single_byte_payload);
    RUN_TEST(test_5v_page_write_execute_accepts_zero_length_chunk);

    RUN_TEST(test_5v_page_write_execute_boundary_32768_64);
    RUN_TEST(test_5v_page_write_execute_boundary_65536_128);
    RUN_TEST(test_5v_page_write_execute_boundary_131072_128);
    RUN_TEST(test_5v_page_write_execute_boundary_262144_128);
    RUN_TEST(test_5v_page_write_execute_boundary_262144_256);
    RUN_TEST(test_5v_page_write_execute_boundary_524288_256);
    RUN_TEST(test_5v_page_write_execute_boundary_524288_512);
    RUN_TEST(test_5v_page_write_execute_refuses_unaligned_second_chunk);
    RUN_TEST(test_5v_page_write_execute_accepts_aligned_two_chunk_p256);
    RUN_TEST(test_5v_page_write_execute_accepts_aligned_two_chunk_p512);
    RUN_TEST(test_5v_page_write_execute_ignores_mem_size_entirely);

    RUN_TEST(test_5v_page_write_execute_refuses_page_size_not_power_of_two);
    RUN_TEST(test_5v_page_write_execute_refuses_page_size_above_ceiling);
    RUN_TEST(test_5v_page_write_execute_refuses_page_size_transport_saturated);

    RUN_TEST(test_5v_page_write_init_does_not_blank_check);
    RUN_TEST(test_5v_page_write_init_no_vpp_with_flag_can_erase_set);

    RUN_TEST(test_5v_page_lock_status_dispatch);
    RUN_TEST(test_5v_page_lock_status_pinned_sequence);
    RUN_TEST(test_5v_page_lock_status_no_vpp);
    RUN_TEST(test_5v_page_lock_status_raw_byte_fidelity);
    RUN_TEST(test_5v_page_lock_status_mode_bracketing);

    return UNITY_END();
}
