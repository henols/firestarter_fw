"""Behavioral host harness for the write and read transfer loops.

Compiles production eprom_operations.cpp and operation_utils.cpp against a
scripted host byte stream and a capturing log stub. Assertions operate on the
frames the firmware emits and the bytes it hands to the memory handler, never
on source text.

The host side of each scenario follows what firestarter_app 3.2.4 sends:
"OK" acks, '#' data frames and a "DONE" only in answer to a data request
after its file is exhausted.
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
void delay(unsigned long ms);
"""

DRIVER = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include <Arduino.h>
#include "eprom_operations.h"
#include "firestarter.h"
#include "memory_utils.h"
#include "messages.h"
#include "operation_utils.h"
#include "rurp_shield.h"

static unsigned long fake_ms = 0;
static std::string rx;
static size_t rx_pos = 0;
static std::vector<uint8_t> ids;
static std::vector<uint32_t> chunks;  // sizes of emitted read chunks
static std::vector<uint8_t> written;  // bytes handed to the write handler
static uint32_t written_at = 0xFFFFFFFF;

// The 3.2.4 host write loop: on each data request it sends the next chunk
// of its file, or DONE once the file is exhausted.
static int host_file = -1;  // bytes left in the host's file; -1 = no host
static uint8_t host_chunk = 32;
static const char* host_reply = nullptr;  // a scripted reply instead
static void send(const char* text);
static void send_data(uint8_t n, uint8_t fill);
static void host_on_data_request() {
    if (host_reply) {
        send(host_reply);
        host_reply = nullptr;
    } else if (host_file > 0) {
        uint8_t n = host_file < host_chunk ? (uint8_t)host_file : host_chunk;
        send_data(n, 0x11);
        host_file -= n;
    } else if (host_file == 0) {
        send("DONE");
    }
}

unsigned long millis() { return fake_ms; }
void delay(unsigned long ms) { fake_ms += ms; }
void op_reset_timeout() {}

uint32_t mem_util_operation_end(const firestarter_handle_t* handle) {
    if (handle->region_end == 0 || handle->region_end > handle->mem_size) {
        return handle->mem_size;
    }
    return handle->region_end;
}

extern "C" {
int rurp_communication_available() { return (int)(rx.size() - rx_pos); }
int rurp_communication_peak() { return rx_pos < rx.size() ? (uint8_t)rx[rx_pos] : -1; }
int rurp_communication_read() { return rx_pos < rx.size() ? (uint8_t)rx[rx_pos++] : -1; }
size_t rurp_communication_read_bytes(char* buf, size_t n) {
    size_t i = 0;
    while (i < n && rx_pos < rx.size()) buf[i++] = rx[rx_pos++];
    return i;
}
// Harness framing after '#': one length byte, then the payload.
int rurp_communication_read_data(char* buf, size_t cap) {
    size_t n = (uint8_t)rx[rx_pos++];
    assert(n <= cap);
    memcpy(buf, rx.data() + rx_pos, n);
    rx_pos += n;
    return (int)n;
}
size_t rurp_communication_write(const char*, size_t n) { return n; }
void rurp_log_id(uint8_t id, const uint8_t*, uint8_t) {
    ids.push_back(id);
    if (id == MSG_OK_REQ_DATA) host_on_data_request();
    // The host acks each finished phase; the first ack starts INIT.
    if (id == MSG_INIT_DONE || id == MSG_MAIN_DONE || id == MSG_END_DONE) send("OK");
}
void rurp_log_id_u8(uint8_t id, uint8_t) { ids.push_back(id); }
void rurp_log_id_u16(uint8_t id, uint16_t) { ids.push_back(id); }
void rurp_log_id_u24(uint8_t id, uint32_t) { ids.push_back(id); }
void rurp_log_id_u32(uint8_t id, uint32_t) { ids.push_back(id); }
void rurp_log_id_wide(uint8_t id, const uint8_t*, uint16_t n) {
    ids.push_back(id);
    if (id == MSG_DATA_CHUNK) {
        chunks.push_back(n);
        send(host_reply ? host_reply : "OK");
    }
}
}

static void write_main(firestarter_handle_t* h) {
    if (written_at == 0xFFFFFFFF) written_at = h->address;
    written.insert(written.end(), h->data_buffer, h->data_buffer + h->data_size);
}

static void read_main(firestarter_handle_t* h) {
    uint32_t remaining = h->mem_size - h->address;
    h->data_size = remaining < 64 ? remaining : 64;  // a 64-byte buffer
    memset(h->data_buffer, 0x5A, h->data_size);
}

static firestarter_handle_t handle;

static void reset(uint8_t cmd, uint32_t address, uint32_t mem_size, uint32_t region_end) {
    memset(&handle, 0, sizeof(handle));
    handle.cmd = cmd;
    handle.address = address;
    handle.mem_size = mem_size;
    handle.region_end = region_end;
    handle.firestarter_operation_main = cmd == CMD_WRITE ? write_main : read_main;
    rx.clear();
    rx_pos = 0;
    ids.clear();
    chunks.clear();
    written.clear();
    written_at = 0xFFFFFFFF;
    fake_ms = 0;
    host_file = -1;
    host_chunk = 32;
    host_reply = nullptr;
    send("OK");
}

static void send(const char* text) { rx += text; }
static void send_data(uint8_t n, uint8_t fill) {
    rx += '#';
    rx += (char)n;
    rx.append(n, (char)fill);
}

// Run the command until it finishes, as loop() does.
static void run(bool (*op)(firestarter_handle_t*)) {
    for (int i = 0; i < 1000; i++) {
        handle.response_code = RESPONSE_CODE_OK;
        if (op(&handle)) return;
    }
    assert(!"command did not finish");
}

static unsigned count(uint8_t id) {
    unsigned n = 0;
    for (uint8_t i : ids) n += i == id;
    return n;
}

static bool has_error() {
    for (uint8_t i : ids) if (i >= MSG_ERR_BAD_JSON && i < MSG_DATA_PROGRESS) return true;
    return false;
}

int main() {
    // 3.2.4 bounded write: region-end = address + file size. The file fills
    // the region, so the firmware ends at op_end and the host sends no DONE.
    reset(CMD_WRITE, 16, 1024, 16 + 48);
    host_file = 48;
    run(eprom_write);
    assert(!has_error());
    assert(count(MSG_MAIN_DONE) == 1);
    assert(written.size() == 48 && written_at == 16);
    assert(count(MSG_OK_REQ_DATA) == 2);
    assert(rx_pos == rx.size());
    puts("PASS bounded write ends at region end");

    // A DONE before region-end is refused: the host sent less than it declared.
    reset(CMD_WRITE, 0, 1024, 48);
    host_file = 32;
    run(eprom_write);
    assert(count(MSG_ERR_CMD) == 1);
    assert(count(MSG_MAIN_DONE) == 0);
    assert(written.size() == 32);
    puts("PASS early DONE refused");

    // Without region-end a DONE ends the write at any cursor. A 3.2.4 host
    // sends no region-end for an empty file, and DONE at the first request.
    reset(CMD_WRITE, 0, 1024, 0);
    host_file = 40;
    run(eprom_write);
    assert(!has_error());
    assert(count(MSG_MAIN_DONE) == 1);
    assert(written.size() == 40);
    reset(CMD_WRITE, 0, 1024, 0);
    host_file = 0;
    run(eprom_write);
    assert(!has_error() && count(MSG_MAIN_DONE) == 1 && written.empty());
    puts("PASS unbounded DONE accepted");

    // Data past region-end is still refused as out of range.
    reset(CMD_WRITE, 0, 1024, 40);
    host_file = 48;
    run(eprom_write);
    assert(count(MSG_ERR_OUT_OF_RANGE) == 1 && written.size() == 32);
    puts("PASS late data refused");

    // A stray ACK in the write loop gets an error frame, not silence.
    reset(CMD_WRITE, 0, 1024, 64);
    host_file = 64;
    host_reply = "OK";
    run(eprom_write);
    assert(count(MSG_ERR_CMD) == 1);
    assert(count(MSG_MAIN_DONE) == 0);
    assert(written.empty());
    puts("PASS stray ACK refused");

    // A read without region-end runs to mem_size in whole chunks.
    reset(CMD_READ, 0, 160, 0);
    run(eprom_read);
    assert(!has_error());
    assert(chunks.size() == 3 && chunks[0] == 64 && chunks[1] == 64 && chunks[2] == 32);
    assert(count(MSG_MAIN_DONE) == 1);
    puts("PASS read to mem_size");

    // A read with region-end stops there and cuts the last chunk.
    reset(CMD_READ, 16, 1024, 16 + 100);
    run(eprom_read);
    assert(!has_error());
    assert(chunks.size() == 2 && chunks[0] == 64 && chunks[1] == 36);
    assert(handle.address == 116);
    puts("PASS read honours region end");

    // DONE where the read expects a chunk ACK ends the command at once.
    reset(CMD_READ, 0, 1024, 0);
    host_reply = "DONE";
    run(eprom_read);
    assert(count(MSG_ERR_CMD) == 1 && count(MSG_ERR_TIMEOUT) == 0);
    assert(fake_ms < 1000);
    puts("PASS DONE in ACK wait");
    return 0;
}
"""


def _compile(argv: list[str]) -> None:
    proc = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    assert proc.returncode == 0, (
        f"compiler failed: {' '.join(argv)}\n{proc.stdout}\n{proc.stderr}"
    )


def test_write_and_read_transfer_loops(tmp_path: Path) -> None:
    cxx = shutil.which(os.environ.get("CXX", "g++"))
    assert cxx, "a host C++ compiler is required (no silent skip)"

    (tmp_path / "Arduino.h").write_text(ARDUINO)
    driver = tmp_path / "driver.cpp"
    driver.write_text(DRIVER)
    includes = ["-I", str(tmp_path), "-I", str(ROOT / "include")]
    exe = tmp_path / "driver"
    _compile([cxx, "-std=c++17", *includes, str(driver),
              str(ROOT / "src/eprom_operations.cpp"),
              str(ROOT / "src/operation_utils.cpp"), "-o", str(exe)])
    result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode == 0, (
        f"transfer loop test failed:\n{result.stdout}\n{result.stderr}"
    )
    assert result.stdout.splitlines() == [
        "PASS bounded write ends at region end",
        "PASS early DONE refused",
        "PASS unbounded DONE accepted",
        "PASS late data refused",
        "PASS stray ACK refused",
        "PASS read to mem_size",
        "PASS read honours region end",
        "PASS DONE in ACK wait",
    ]
