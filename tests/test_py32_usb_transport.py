"""Large PY32 USB frames: production COBS/CRC code and backpressure handling.

Uses the same host-compiler / real-compile-database seam as other ARM tests.
The mocked USB controller only supplies packets and completion callbacks.
"""
from ._host_build import REPO_ROOT, compile_ok, host_cc, host_cxx, run_binary
from ._py32_build import arm_defines


def test_large_usb_frames_and_stalled_host(tmp_path):
    (tmp_path / 'boards').mkdir()
    (tmp_path / 'avr').mkdir()
    (tmp_path / 'boards/py32f071_rurp_shield.h').write_text('''
#pragma once
#define __get_PRIMASK() 0
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
#define __HAL_RCC_SYSCFG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USB_CLK_ENABLE() ((void)0)
#define NVIC_EnableIRQ(x) ((void)0)
''')
    (tmp_path / 'rurp_platform.h').write_text('''
#include <stdint.h>
uint32_t test_millis(void);
#define RURP_MILLIS() test_millis()
''')
    (tmp_path / 'usbd_cdc.h').write_text('#include "usbd_core.h"\n')
    (tmp_path / 'usbd_core.h').write_text('''
#pragma once
#define USB_MEM_ALIGNX
#define USB_DEVICE_DESCRIPTOR_INIT(...) 0
#define USB_CONFIG_DESCRIPTOR_INIT(...) 0
#define CDC_ACM_DESCRIPTOR_INIT(...) 0
#define USB_LANGID_INIT(...) 0
#define USB_DESCRIPTOR_TYPE_STRING 3
struct usbd_interface { int unused; };
struct usbd_endpoint { unsigned ep_addr; void (*ep_cb)(uint8_t, uint32_t); };
void fake_ep_write(uint8_t, const uint8_t *, size_t);
static inline bool usb_device_is_configured(void) { return true; }
static inline void usbd_ep_start_read(uint8_t ep, uint8_t *buf, size_t n) {}
static inline void usbd_ep_start_write(uint8_t ep, const uint8_t *buf, size_t n) {
    fake_ep_write(ep, buf, n);
}
static inline void usbd_desc_register(const uint8_t *desc) {}
static inline struct usbd_interface *usbd_cdc_acm_init_intf(struct usbd_interface *p) { return p; }
static inline void usbd_add_interface(struct usbd_interface *p) {}
static inline void usbd_add_endpoint(struct usbd_endpoint *p) {}
static inline void usbd_initialize(void) {}
static inline void USBD_IRQHandler(void) {}
''')
    (tmp_path / 'avr/pgmspace.h').write_text('''
#pragma once
#define PROGMEM
#define pgm_read_byte(ptr) (*(const uint8_t *)(ptr))
''')
    # Only replace platform wiring / serial adapters, never the code under test.
    (tmp_path / 'rurp_serial_utils.h').write_text('''
#pragma once
#define SERIAL_PORT Serial
#define TIMEOUT_MS 1000
''')
    (tmp_path / 'rurp_shield.h').write_text('#pragma once\n')

    backend = tmp_path / 'usb_backend.c'
    backend.write_text(r'''
#include <assert.h>
#include <string.h>
#include "platform/py32f071/src/usb_cdc.c"

static uint8_t received[DATA_BUFFER_SIZE + 32U];
static uint8_t pending_packet[64];
static size_t received_count;
static size_t pending_size;
static bool pending;
static bool paused;
static uint32_t now_ms;

void fake_ep_write(uint8_t ep, const uint8_t *data, size_t count) {
    assert(ep == FIRESTARTER_CDC_IN_EP && count <= sizeof(pending_packet));
    assert(!pending);
    if (count) memcpy(pending_packet, data, count);
    pending_size = count;
    pending = true;
}

uint32_t test_millis(void) {
    ++now_ms;
    if (pending && !paused) {
        const size_t sent = pending_size;
        pending = false;
        assert(received_count + sent <= sizeof(received));
        if (sent) memcpy(received + received_count, pending_packet, sent);
        received_count += sent;
        firestarter_cdc_bulk_in(FIRESTARTER_CDC_IN_EP, (uint32_t)sent);
    }
    return now_ms;
}

void test_usb_begin(void) {
    rurp_communication_begin();
    usbd_configure_done_callback();
}

void test_feed_rx(const uint8_t *data, size_t count) {
    for (size_t offset = 0; offset < count;) {
        size_t n = count - offset;
        if (n > sizeof(cdc_out_packet)) n = sizeof(cdc_out_packet);
        memcpy(cdc_out_packet, data + offset, n);
        firestarter_cdc_bulk_out(FIRESTARTER_CDC_OUT_EP, (uint32_t)n);
        offset += n;
    }
}

void test_prepare_tx(int hold) {
    pending = false;
    received_count = 0U;
    paused = hold != 0;
    now_ms = 0U;
    usbd_configure_done_callback();
}

size_t test_tx_length(void) { return received_count; }
const uint8_t *test_tx_bytes(void) { return received; }
''')

    driver = tmp_path / 'transport.cpp'
    driver.write_text(r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
void test_usb_begin(void);
void test_feed_rx(const uint8_t *, size_t);
void test_prepare_tx(int);
size_t test_tx_length(void);
const uint8_t *test_tx_bytes(void);
uint32_t test_millis(void);
int py32_usb_available(void);
int py32_usb_peek(void);
int py32_usb_read(void);
size_t py32_usb_read_bytes(char *, size_t);
size_t py32_usb_write(const uint8_t *, size_t);
void py32_usb_flush(void);
int rurp_communication_tx_failed(void);
}

struct SerialMock {
    void begin(unsigned long) {}
    void end() {}
    explicit operator bool() const { return true; }
    int available() const { return py32_usb_available(); }
    int read() { return py32_usb_read(); }
    int peek() const { return py32_usb_peek(); }
    size_t readBytes(char *out, size_t n) { return py32_usb_read_bytes(out, n); }
    size_t write(uint8_t b) { return py32_usb_write(&b, 1); }
    size_t write(const uint8_t *p, size_t n) { return py32_usb_write(p, n); }
    void flush() { py32_usb_flush(); }
} Serial;

unsigned long millis() { return test_millis(); }
void delay(unsigned long) {}
void delayMicroseconds(unsigned long) {}

#include "src/boards/rurp_serial_utils.cpp"

static uint8_t reference_crc(const std::vector<uint8_t>& data) {
    uint8_t crc = 0;
    for (const uint8_t b : data) {
        crc ^= b;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x07) : uint8_t(crc << 1);
    }
    return crc;
}

static std::vector<uint8_t> frame(const std::vector<uint8_t>& payload, bool wrong_crc = false) {
    std::vector<uint8_t> raw = payload;
    raw.push_back(uint8_t(reference_crc(payload) ^ (wrong_crc ? 0x5A : 0)));
    std::vector<uint8_t> encoded;
    encoded.push_back(0);
    size_t code_index = 0;
    uint8_t code = 1;
    for (const uint8_t b : raw) {
        if (b == 0) {
            encoded[code_index] = code;
            code_index = encoded.size();
            encoded.push_back(0);
            code = 1;
        } else {
            encoded.push_back(b);
            ++code;
            if (code == 0xFF) {
                encoded[code_index] = code;
                code_index = encoded.size();
                encoded.push_back(0);
                code = 1;
            }
        }
    }
    encoded[code_index] = code;
    encoded.push_back(0);
    return encoded;
}

static int decode_from_usb(const std::vector<uint8_t>& encoded, std::vector<char>& out) {
    const uint8_t marker = '#';
    test_feed_rx(&marker, 1);
    test_feed_rx(encoded.data(), encoded.size());
    assert(rurp_communication_peak() == '#');
    assert(rurp_communication_read() == '#');
    const int result = rurp_communication_read_data(out.data(), DATA_BUFFER_SIZE);
    assert(rurp_communication_available() == 0);
    return result;
}

int main() {
    test_usb_begin();
    std::vector<char> decoded(DATA_BUFFER_SIZE + 1);

    // Full-size payload, embedded zeros, nonzero runs and a real CRC.
    std::vector<uint8_t> mixed(DATA_BUFFER_SIZE);
    for (size_t i = 0; i < mixed.size(); ++i)
        mixed[i] = (i % 251 == 0) ? 0 : uint8_t(i % 254 + 1);
    assert(decode_from_usb(frame(mixed), decoded) == DATA_BUFFER_SIZE);
    assert(memcmp(decoded.data(), mixed.data(), mixed.size()) == 0);

    // COBS runs with code 0xFF, at the full advertised payload length.
    std::vector<uint8_t> solid(DATA_BUFFER_SIZE, 0xA5);
    assert(decode_from_usb(frame(solid), decoded) == DATA_BUFFER_SIZE);
    assert(memcmp(decoded.data(), solid.data(), solid.size()) == 0);

    // Oversize and CRC failures must leave the next frame decodable.
    std::vector<uint8_t> oversize(DATA_BUFFER_SIZE + 1, 0xA5);
    assert(decode_from_usb(frame(oversize), decoded) == -2);
    assert(decode_from_usb(frame(std::vector<uint8_t>{1, 2, 3}, true), decoded) == -4);
    const std::vector<uint8_t> good = {0, 11, 0, 19};
    assert(decode_from_usb(frame(good), decoded) == (int)good.size());
    assert(memcmp(decoded.data(), good.data(), good.size()) == 0);

    // Real shared MSG_DATA_CHUNK emitter -> USB backend -> fake host.
    const uint8_t id = 0x42;
    test_prepare_tx(0);
    rurp_log_id_wide(id, mixed.data(), (uint16_t)mixed.size());
    assert(!rurp_communication_tx_failed());
    std::vector<uint8_t> expected = {0xAA, 0x55, 0xAA, 0x55};
    const uint16_t length = (uint16_t)(mixed.size() + 2);
    expected.push_back(uint8_t(length >> 8));
    expected.push_back(uint8_t(length));
    expected.push_back(id);
    expected.insert(expected.end(), mixed.begin(), mixed.end());
    std::vector<uint8_t> checksum_input = {id};
    checksum_input.insert(checksum_input.end(), mixed.begin(), mixed.end());
    expected.push_back(reference_crc(checksum_input));
    expected.push_back(0x0A);
    assert(test_tx_length() == expected.size());
    assert(memcmp(test_tx_bytes(), expected.data(), expected.size()) == 0);

    // Stalled USB IN endpoint: the write is incomplete and fails closed.
    test_prepare_tx(1);
    rurp_log_id_wide(id, mixed.data(), (uint16_t)mixed.size());
    assert(rurp_communication_tx_failed());
    assert(py32_usb_write(mixed.data(), 1) == 0);

    // Only a new USB configuration clears the latched error.
    test_prepare_tx(0);
    assert(!rurp_communication_tx_failed());
    return 0;
}
''')

    backend_obj = tmp_path / 'usb_backend.o'
    compile_ok([host_cc(), '-std=c11',
                *arm_defines('platform/py32f071/src/usb_cdc.c'),
                '-UNDEBUG', '-I', tmp_path, '-I', REPO_ROOT,
                '-c', backend, '-o', backend_obj])
    binary = tmp_path / 'transport'
    compile_ok([host_cxx(), '-std=c++17',
                *arm_defines('src/boards/rurp_serial_utils.cpp'),
                '-UNDEBUG', '-I', tmp_path, '-I', REPO_ROOT,
                driver, backend_obj, '-o', binary])
    result = run_binary(binary)
    assert result.returncode == 0, result.stderr
