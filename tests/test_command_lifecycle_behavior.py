"""Behavioral host harness for the real firmware command dispatcher.

Runs production firestarter.cpp, json_parser.c and jsmn.c against stubbed
transport/hardware functions; assertions operate on register writes and command
outcomes, never source-code spellings. Requires the #118 fix at the branch base.
"""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

ARDUINO = """#pragma once
#include <stdint.h>
#include <stddef.h>
unsigned long millis();
"""

DRIVER = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

#define RURP_BOARD_NAME "unit"
#include "src/firestarter.cpp"

static unsigned long fake_ms = 100;
static std::string incoming;
static unsigned reads = 0;
static unsigned configured = 0;
static unsigned writes = 0;
static unsigned ram_tests = 0;
static unsigned logged = 0;
static int ce_level = 0;
static int cleared[3] = {};
static rurp_register_t latch[256] = {};

unsigned long millis() { return fake_ms; }
extern "C" void rurp_load_config() {}
extern "C" void rurp_board_setup() {}
extern "C" uint8_t rurp_get_physical_hardware_revision() { return 0; }
extern "C" rurp_configuration_t* rurp_get_config() {
    static rurp_configuration_t config = {};
    return &config;
}
extern "C" void rurp_save_config(rurp_configuration_t*) {}
extern "C" bool rurp_config_migrate(rurp_configuration_t*) { return false; }
extern "C" bool rurp_config_commit(rurp_configuration_t*, const rurp_configuration_t*) {
    return true;
}
extern "C" long rurp_get_bandgap_adc_reading() { return 0; }
extern "C" uint16_t rurp_bandgap_from_measured_vcc(uint32_t, uint32_t, uint32_t) {
    return 1100;
}
extern "C" void rurp_set_control_pin(uint8_t pin, uint8_t value) {
    if (pin == CHIP_ENABLE) ce_level = value;
}
extern "C" void rurp_write_to_register(uint8_t reg, rurp_register_t value) {
    latch[reg] = value;
    if (value == 0) {
        if (reg == CONTROL_REGISTER) cleared[0]++;
        if (reg == LEAST_SIGNIFICANT_BYTE) cleared[1]++;
        if (reg == MOST_SIGNIFICANT_BYTE) cleared[2]++;
    }
}
extern "C" void rurp_log_id(uint8_t, const uint8_t*, uint8_t) { logged++; }
extern "C" void rurp_log_id_u8(uint8_t, uint8_t) { logged++; }
extern "C" void rurp_log_id_u16(uint8_t, uint16_t) { logged++; }
extern "C" void rurp_log_id_u24(uint8_t, uint32_t) { logged++; }
extern "C" void rurp_log_id_u32(uint8_t, uint32_t) { logged++; }
extern "C" int rurp_communication_available() { return incoming.empty() ? 0 : 1; }
extern "C" int rurp_communication_read_data(char* buf, size_t capacity) {
    if (incoming.empty()) return 0;
    assert(incoming.size() <= capacity);
    memcpy(buf, incoming.data(), incoming.size());
    int length = static_cast<int>(incoming.size());
    incoming.clear();
    return length;
}
extern "C" uint16_t eprom_block_budget_s(uint32_t, uint32_t, uint32_t) { return 5; }
extern "C" bool op_execute_function(void (*fn)(firestarter_handle_t*),
                                      firestarter_handle_t* h) {
    fn(h);
    return h->response_code != RESPONSE_CODE_ERROR;
}
extern "C" void configure_memory(firestarter_handle_t* h) {
    configured++;
    assert(h->protocol != 0 && h->mem_size != 0);
    assert(h->pins != 0 && h->vpp_mv != 0);
}
extern "C" bool eprom_read(firestarter_handle_t*) { reads++; return true; }
extern "C" bool eprom_write(firestarter_handle_t*) { writes++; return true; }
extern "C" bool eprom_erase(firestarter_handle_t*) { writes++; return true; }
extern "C" bool eprom_check_chip_id(firestarter_handle_t*) { return true; }
extern "C" bool eprom_sdp_unlock(firestarter_handle_t*) { return true; }
extern "C" bool eprom_sdp_lock(firestarter_handle_t*) { return true; }
extern "C" bool eprom_lock_status(firestarter_handle_t*) { return true; }
extern "C" bool eprom_ram_test(firestarter_handle_t*) { ram_tests++; return true; }
extern "C" bool hw_read_voltage(firestarter_handle_t*) { return true; }
extern "C" bool hw_get_version(firestarter_handle_t*) { return true; }
extern "C" bool hw_get_config(firestarter_handle_t*) { return true; }
extern "C" bool fw_get_version(firestarter_handle_t*) { return true; }

static void arm() {
    latch[CONTROL_REGISTER] = 0xFF;
    latch[LEAST_SIGNIFICANT_BYTE] = 0xA5;
    latch[MOST_SIGNIFICANT_BYTE] = 0x5A;
    ce_level = 0;
}
static void assert_idle() {
    assert(handle.cmd == CMD_IDLE);
    assert(ce_level == 1);
    assert(latch[CONTROL_REGISTER] == 0);
    assert(latch[LEAST_SIGNIFICANT_BYTE] == 0);
    assert(latch[MOST_SIGNIFICANT_BYTE] == 0);
    for (int n : cleared) assert(n > 0);
}
static void reset() {
    handle = {};
    handle.cmd = CMD_IDLE;
    fake_ms = 100;
    timeout_started_ms = 0;
    incoming.clear();
    reads = configured = writes = ram_tests = logged = 0;
    ce_level = 0;
    memset(latch, 0, sizeof(latch));
    memset(cleared, 0, sizeof(cleared));
}
static void frame(const char* json) {
    incoming = json;
    loop();
}
static const char* read_cmd =
    "{\"cmd\":1,\"memory-size\":65536,\"algorithm\":7,"
    "\"pin-count\":28,\"vpp_mv\":12500}";

int main() {
    reset();
    arm();
    handle.cmd = CMD_WRITE;
    command_done(&handle);
    assert_idle();
    puts("PASS cleanup");

    reset();
    arm();
    handle.cmd = CMD_READ;
    timeout_started_ms = fake_ms - TIMEOUT_MS;  // started one timeout ago; abort before dispatch
    loop();
    assert_idle();
    assert(reads == 0);
    puts("PASS timeout");

    reset();
    frame(read_cmd);
    assert(handle.cmd == CMD_READ && configured == 1);
    arm();
    loop();
    assert(reads == 1);
    assert_idle();
    puts("PASS completion");

    reset();
    timeout_started_ms = fake_ms;  // unexpired timer carried over from prior command
    arm();
    frame("{\"cmd\":2,\"address\":-256,\"memory-size\":65536,"
          "\"algorithm\":7,\"pin-count\":28,\"vpp_mv\":12500}");
    assert_idle();
    loop();
    assert(configured == 0 && writes == 0);
    puts("PASS negative-address rejection");

    reset();
    frame(read_cmd);
    assert(configured == 1);
    command_done(&handle);
    arm();
    frame("{\"cmd\":2,\"memory-size\":65536,\"algorithm\":7}");
    assert_idle();
    assert(configured == 1 && writes == 0);
    puts("PASS required-fields rejection");

    reset();
    arm();
    frame("{\"cmd\":2,\"memory-size\":65536,\"algorithm\":7,"
          "\"pin-count\":28,\"vpp_mv\":12500,"
          "\"bus-config\":{\"bus\":false}}");
    assert_idle();
    assert(configured == 0 && writes == 0);
    puts("PASS malformed bus rejection");

    reset();
    frame("{\"cmd\":18,\"memory-size\":32768,\"algorithm\":40,"
          "\"pin-count\":28,\"vpp_mv\":12000}");
    assert(handle.cmd == CMD_RAM_TEST && configured == 1);
    arm();
    loop();
    assert(ram_tests == 1);
    assert_idle();
    puts("PASS ram-test dispatch");
    return 0;
}
"""

def _compile(argv: list[str]) -> None:
    proc = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    assert proc.returncode == 0, (
        f"compiler failed: {' '.join(argv)}\n{proc.stdout}\n{proc.stderr}"
    )

def test_command_lifecycle_and_safe_cleanup(tmp_path: Path) -> None:
    cc = shutil.which(os.environ.get("CC", "gcc"))
    cxx = shutil.which(os.environ.get("CXX", "g++"))
    assert cc and cxx, "host C and C++ compilers are required (no silent skip)"

    (tmp_path / "Arduino.h").write_text(ARDUINO)
    main = tmp_path / "driver.cpp"
    main.write_text(DRIVER)
    includes = [
        "-I", str(tmp_path), "-I", str(ROOT),
        "-I", str(ROOT / "include"), "-I", str(ROOT / "lib/jsmn/src"),
    ]
    parser_obj, tokenizer_obj = tmp_path / "parser.o", tmp_path / "jsmn.o"
    exe = tmp_path / "driver"
    _compile([cc, "-std=c11", "-c", *includes,
              str(ROOT / "src/json_parser.c"), "-o", str(parser_obj)])
    _compile([cc, "-std=c11", "-c", *includes,
              str(ROOT / "lib/jsmn/src/jsmn.c"), "-o", str(tokenizer_obj)])
    _compile([cxx, "-std=c++17", *includes, str(main),
              str(parser_obj), str(tokenizer_obj), "-o", str(exe)])
    result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode == 0, (
        f"dispatcher integration test failed:\n{result.stdout}\n{result.stderr}"
    )
    assert set(result.stdout.splitlines()) == {
        "PASS cleanup",
        "PASS timeout",
        "PASS completion",
        "PASS negative-address rejection",
        "PASS required-fields rejection",
        "PASS malformed bus rejection",
        "PASS ram-test dispatch",
    }
