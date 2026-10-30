"""Behavioral host harness for the real firmware command dispatcher.

Runs production firestarter.cpp and command_decoder.c against stubbed
transport/hardware functions; assertions operate on register writes and command
outcomes, never source-code spellings. Frames are built from the generated
include/command_frame.h offsets.
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
static unsigned logged = 0;
static int ce_level = 0;
static int cleared[3] = {};
static rurp_register_t latch[256] = {};
static std::string boot_order;

unsigned long millis() { return fake_ms; }
extern "C" void rurp_load_config() { boot_order += "config;"; }
extern "C" void rurp_board_setup() { boot_order += "board;"; }
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
    reads = configured = writes = logged = 0;
    ce_level = 0;
    memset(latch, 0, sizeof(latch));
    memset(cleared, 0, sizeof(cleared));
}
static void put32(std::string& s, size_t off, uint32_t v) {
    s[off] = (char)(v >> 24);
    s[off + 1] = (char)(v >> 16);
    s[off + 2] = (char)(v >> 8);
    s[off + 3] = (char)v;
}
// A memory-command frame with no address lines; every other field is zero.
static std::string memory_frame(uint8_t cmd, uint32_t mem_size, uint32_t address,
                                uint8_t pins, uint16_t vpp_mv) {
    std::string f(CF_HEADER_SIZE + CF_MEMORY_SIZE, '\0');
    f[CF_OFF_MAGIC] = (char)CF_MAGIC;
    f[CF_OFF_VERSION] = (char)CF_VERSION;
    f[CF_OFF_CMD] = (char)cmd;
    f[CF_OFF_LENGTH] = (char)(CF_MEMORY_SIZE >> 8);
    f[CF_OFF_LENGTH + 1] = (char)(CF_MEMORY_SIZE & 0xFF);
    const size_t p = CF_HEADER_SIZE;
    put32(f, p + CF_MEMORY_OFF_MEM_SIZE, mem_size);
    put32(f, p + CF_MEMORY_OFF_ADDRESS, address);
    f[p + CF_MEMORY_OFF_PROTOCOL] = 7;
    f[p + CF_MEMORY_OFF_PINS] = (char)pins;
    f[p + CF_MEMORY_OFF_VPP_MV] = (char)(vpp_mv >> 8);
    f[p + CF_MEMORY_OFF_VPP_MV + 1] = (char)(vpp_mv & 0xFF);
    for (int i = 0; i < CF_ADDRESS_LINES; i++) {
        f[p + CF_MEMORY_OFF_ADDRESS_LINES + i] = (char)CF_LINE_ABSENT;
    }
    f[p + CF_MEMORY_OFF_RW_LINE] = (char)CF_LINE_ABSENT;
    f[p + CF_MEMORY_OFF_VPP_LINE] = (char)CF_LINE_ABSENT;
    return f;
}
static void frame(const std::string& bytes) {
    incoming = bytes;
    loop();
}
static const std::string read_cmd = memory_frame(CMD_READ, 65536, 0, 28, 12500);

int main() {
    reset();
    boot_order.clear();
    setup();
    // The shield latches are cleared before the config load can write EEPROM.
    assert(boot_order == "board;config;");
    assert(handle.cmd == CMD_IDLE);
    puts("PASS boot order");

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
    frame(memory_frame(CMD_WRITE, 65536, 0xFFFFFF00UL, 28, 12500));  // -256 as u32
    assert_idle();
    loop();
    assert(configured == 0 && writes == 0);
    puts("PASS address-range rejection");

    reset();
    frame(read_cmd);
    assert(configured == 1);
    command_done(&handle);
    arm();
    frame(memory_frame(CMD_WRITE, 65536, 0, 0, 0));  // no pin-count, no vpp_mv
    assert_idle();
    assert(configured == 1 && writes == 0);
    puts("PASS required-fields rejection");

    reset();
    arm();
    std::string bad_bus = memory_frame(CMD_WRITE, 65536, 0, 28, 12500);
    bad_bus[CF_HEADER_SIZE + CF_MEMORY_OFF_ADDRESS_LINES] = (char)CF_BUS_LINES;  // off the bus
    frame(bad_bus);
    assert_idle();
    assert(configured == 0 && writes == 0);
    puts("PASS malformed bus rejection");
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
        "-I", str(ROOT / "include"),
    ]
    decoder_obj = tmp_path / "command_decoder.o"
    exe = tmp_path / "driver"
    _compile([cc, "-std=c11", "-c", *includes,
              str(ROOT / "src/command_decoder.c"), "-o", str(decoder_obj)])
    _compile([cxx, "-std=c++17", *includes, str(main),
              str(decoder_obj), "-o", str(exe)])
    result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode == 0, (
        f"dispatcher integration test failed:\n{result.stdout}\n{result.stderr}"
    )
    assert set(result.stdout.splitlines()) == {
        "PASS boot order",
        "PASS cleanup",
        "PASS timeout",
        "PASS completion",
        "PASS address-range rejection",
        "PASS required-fields rejection",
        "PASS malformed bus rejection",
    }
