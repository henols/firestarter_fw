"""Large PY32 USB frames: production COBS/CRC code and backpressure handling.

Uses the same host-compiler / real-compile-database seam as other ARM tests.
The mocked USB controller only supplies packets and completion callbacks.
"""
import pytest

from ._host_build import REPO_ROOT, compile_ok, host_cc, host_cxx, run_binary
from ._py32_build import arm_defines


@pytest.mark.parametrize('stress_size', [None, 4096], ids=['production', '4k-stress'])
def test_large_usb_frames_and_stalled_host(tmp_path, stress_size):
    def transport_defines(source):
        defines = arm_defines(source)
        # The real image stays at its CMake size. This second configuration
        # stresses streaming across a much smaller USB ring, not shipped RAM.
        if stress_size is not None:
            defines = [d for d in defines if not d.startswith('-DDATA_BUFFER_SIZE=')]
            defines.append(f'-DDATA_BUFFER_SIZE={stress_size}')
        return defines
    (tmp_path / 'boards').mkdir()
    (tmp_path / 'avr').mkdir()
    (tmp_path / 'boards/py32f071_rurp_shield.h').write_text('''
#pragma once
unsigned test_irq_state(void);
void test_disable_irq(void);
void test_enable_irq(void);
#define __get_PRIMASK() test_irq_state()
#define __disable_irq() test_disable_irq()
#define __enable_irq() test_enable_irq()
#define __NOP() ((void)0)
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
void fake_ep_read(void);
static inline int usbd_ep_start_read(uint8_t ep, uint8_t *buf, size_t n) { fake_ep_read(); return 0; }
static inline int usbd_ep_start_write(uint8_t ep, const uint8_t *buf, size_t n) {
    fake_ep_write(ep, buf, n);
    return 0;
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
void _firestarter_emit_frame_wide(uint8_t id, const uint8_t *params, uint16_t param_count);
static inline void _firestarter_emit_frame(uint8_t id, const uint8_t *params, uint8_t param_count) {
    _firestarter_emit_frame_wide(id, params, param_count);
}
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
static size_t packet_count;
static size_t pending_size;
static bool pending;
static bool paused;
static bool host_out_armed;
static uint8_t host_rx[DATA_BUFFER_SIZE + 64U];
static size_t host_rx_count, host_rx_offset;
void fake_ep_read(void) { assert(!host_out_armed); host_out_armed = true; }
static void pump_host_rx(void) {
    if (!host_out_armed || host_rx_offset == host_rx_count) return;
    size_t n = host_rx_count - host_rx_offset;
    if (n > sizeof(cdc_out_packet)) n = sizeof(cdc_out_packet);
    memcpy(cdc_out_packet, host_rx + host_rx_offset, n);
    host_rx_offset += n;
    host_out_armed = false;
    firestarter_cdc_bulk_out(FIRESTARTER_CDC_OUT_EP, n);
}
static uint32_t now_ms;
static unsigned irq_state, unlock_event, timer_event, event_at;
static bool event_fired;
static void fire_event(unsigned kind) {
    event_fired = true;
    if (kind == 1) {
        pending = false; /* SET_CONFIGURATION cancels endpoint transfers. */
        host_out_armed = false;
        host_rx_count = host_rx_offset = 0;
        usbd_configure_done_callback();
    } else {
        usbd_cdc_acm_set_dtr(0, false);
        usbd_cdc_acm_set_dtr(0, true);
    }
}
unsigned test_irq_state(void) { return irq_state; }
void test_disable_irq(void) { assert(!irq_state); irq_state = 1; }
void test_enable_irq(void) {
    assert(irq_state); irq_state = 0;
    if (unlock_event) { unsigned kind = unlock_event; unlock_event = 0; fire_event(kind); }
}
void test_event_during_frame(unsigned kind) {
    timer_event = kind; event_at = now_ms + 2; event_fired = false;
}
void test_assert_event_fired(void) { assert(event_fired && !irq_state); }
void test_ring_reset_ownership(void) {
    assert(!pending);
    rx_head = 70; rx_tail = 65; tx_head = 30; tx_tail = 25;
    host_out_armed = false;
    usbd_configure_done_callback();
    /* The ISR must not mutate either ring while main might hold an index. */
    assert(rx_head == 70 && rx_tail == 65 && tx_head == 30 && tx_tail == 25);
    assert(py32_usb_available() == 0);
    assert(rx_head == 0 && rx_tail == 0 && tx_head == 0 && tx_tail == 0);
    rx_head = 2; rx_tail = 0; rx_buffer[0] = 12;
    unlock_event = 1; /* interrupt immediately after a protected RX consume */
    assert(py32_usb_read() == 12);
    assert(py32_usb_available() == 0 && py32_usb_read() == -1);
}
void test_arm_rx_interrupt(void) { unlock_event = 1; event_fired = false; }

void fake_ep_write(uint8_t ep, const uint8_t *data, size_t count) {
    assert(ep == FIRESTARTER_CDC_IN_EP && count <= sizeof(pending_packet));
    assert(!pending);
    if (count) memcpy(pending_packet, data, count);
    pending_size = count;
    if (count) ++packet_count;
    pending = true;
}

uint32_t test_millis(void) {
    ++now_ms;
    pump_host_rx();
    if (timer_event && now_ms >= event_at) {
        unsigned kind = timer_event; timer_event = 0; fire_event(kind);
    }
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
    host_out_armed = false; /* SET_CONFIGURATION cancels the pending OUT read. */
    usbd_configure_done_callback();
    (void)py32_usb_available();
}

void test_feed_rx(const uint8_t *data, size_t count) {
    (void)py32_usb_available();
    assert(host_rx_offset == host_rx_count);
    assert(count <= sizeof(host_rx));
    memcpy(host_rx, data, count);
    host_rx_count = count;
    host_rx_offset = 0;
    while (host_out_armed && host_rx_offset < host_rx_count) pump_host_rx();
}

void test_rx_backpressure(void) {
    uint8_t block[64U];
    for (size_t i = 0; i < sizeof(block); ++i) block[i] = (uint8_t)i;
    /* Fill beyond ring capacity. The host must stop while bytes stay queued;
     * draining the ring must re-arm OUT, with no loss and repeated wraparound. */
    for (unsigned pass = 0; pass < 4; ++pass) {
        size_t total = 0;
        while (host_out_armed) {
            assert(total < 2 * sizeof(rx_buffer));
            memcpy(cdc_out_packet, block, 64);
            host_out_armed = false;
            firestarter_cdc_bulk_out(FIRESTARTER_CDC_OUT_EP, 64);
            total += 64;
        }
        assert(total > 0 && !rx_armed);
        for (size_t i = 0; i < total; ++i) {
            assert(py32_usb_peek() == (int)(uint8_t)(i % 64));
            assert(py32_usb_read() == (int)(uint8_t)(i % 64));
        }
        assert(host_out_armed && py32_usb_available() == 0);
    }
}

void test_prepare_tx(int hold) {
    pending = false;
    received_count = 0U;
    packet_count = 0U;
    paused = hold != 0;
    now_ms = 0U;
    host_out_armed = false; /* SET_CONFIGURATION cancels the pending OUT read. */
    usbd_configure_done_callback();
    (void)py32_usb_available();
}

void test_reopen(void) {
    paused = false;
    /* DTR does not cancel an in-flight USB packet. Let its completion occur;
     * the new frame must then follow it, with no old queued frame tail. */
    fire_event(2);
    (void)py32_usb_available();
    (void)test_millis();
    received_count = 0;
}
size_t test_packet_count(void) { return packet_count; }
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

#include "messages.h"
#include "frame_ref.h"

extern "C" {
void test_usb_begin(void);
void test_rx_backpressure(void);
void test_feed_rx(const uint8_t *, size_t);
void test_prepare_tx(int);
size_t test_packet_count(void);
size_t test_tx_length(void);
const uint8_t *test_tx_bytes(void);
uint32_t test_millis(void);
int py32_usb_available(void);
int py32_usb_peek(void);
int py32_usb_read(void);
size_t py32_usb_read_bytes(char *, size_t);
size_t py32_usb_write(const uint8_t *, size_t);
void py32_usb_flush(void);
void py32_usb_frame_begin(void);
void py32_usb_frame_end(void);
uint32_t py32_usb_generation(void);
void test_reopen(void);
void test_event_during_frame(unsigned);
void test_assert_event_fired(void);
void test_ring_reset_ownership(void);
void test_arm_rx_interrupt(void);
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

static std::vector<uint8_t> frame(const std::vector<uint8_t>& payload, bool wrong_crc = false) {
    std::vector<uint8_t> encoded;
    if (!wrong_crc) build_cobs_frame_bytes(payload.data(), payload.size(), encoded);
    else {
        std::vector<uint8_t> raw = payload;
        raw.push_back(uint8_t(ref_crc8(payload.data(), payload.size()) ^ 0x5A));
        encoded.resize(raw.size() + raw.size() / 254 + 2);
        encoded.resize(test_cobs_encode(raw.data(), raw.size(), encoded.data()));
        encoded.push_back(0);
    }
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
    test_rx_backpressure();
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
    const uint8_t id = MSG_DATA_CHUNK;
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
    expected.push_back(ref_crc8(checksum_input.data(), checksum_input.size()));
    expected.push_back(0x0A);
    assert(test_packet_count() == (expected.size() + 63) / 64);
    assert(test_tx_length() == expected.size());
    assert(memcmp(test_tx_bytes(), expected.data(), expected.size()) == 0);

    // Stalled USB IN endpoint: the write is incomplete and fails closed.
    test_prepare_tx(1);
    rurp_log_id_wide(id, mixed.data(), (uint16_t)mixed.size());
    assert(rurp_communication_tx_failed());
    assert(py32_usb_write(mixed.data(), 1) == 0);

    // Reopening the port, without SET_CONFIGURATION, recovers a stalled link.
    test_reopen();
    const std::vector<uint8_t> small = {3, 0, 5};
    rurp_log_id_wide(id, small.data(), (uint16_t)small.size());
    assert(!rurp_communication_tx_failed());
    assert(test_tx_length() == small.size() + 9);
    assert(test_tx_bytes()[6] == id);

    // Reconfiguration and DTR reopen in the middle of the real frame emitter:
    // no remainder of the old frame may enter the new session's TX queue.
    for (unsigned event : {1U, 2U}) {
        test_prepare_tx(0);
        test_event_during_frame(event);
        rurp_log_id_wide(id, mixed.data(), (uint16_t)mixed.size());
        test_assert_event_fired();
        assert(rurp_communication_tx_failed());
        const size_t before = test_tx_length();
        assert(before <= 1064); // at most pre-event bytes + one hardware packet
        rurp_log_id_wide(id, small.data(), (uint16_t)small.size());
        assert(!rurp_communication_tx_failed());
        // DTR may deliver one packet already handed to the USB controller.
        // It must not deliver any further old-frame software-queue bytes.
        const size_t after = test_tx_length();
        assert(after >= before + small.size() + 9);
        const uint8_t *tail = test_tx_bytes() + after - small.size() - 9;
        assert(tail[0] == 0xAA && tail[1] == 0x55 && tail[6] == id);
        assert(memcmp(tail + 7, small.data(), small.size()) == 0);
    }
    test_prepare_tx(0);
    test_ring_reset_ownership();

    // A reconnect while decoding must abort the old COBS frame without
    // draining/stitching a new session's input into it.
    const auto encoded = frame(good);
    test_feed_rx(encoded.data(), encoded.size());
    test_arm_rx_interrupt();
    assert(rurp_communication_read_data(decoded.data(), DATA_BUFFER_SIZE) < 0);
    test_assert_event_fired();
    assert(decode_from_usb(frame(good), decoded) == (int)good.size());
    return 0;
}
''')

    backend_obj = tmp_path / 'usb_backend.o'
    compile_ok([host_cc(), '-std=c11',
                *transport_defines('platform/py32f071/src/usb_cdc.c'),
                '-UNDEBUG', '-I', tmp_path, '-I', REPO_ROOT,
                '-c', backend, '-o', backend_obj])
    binary = tmp_path / 'transport'
    compile_ok([host_cxx(), '-std=c++17',
                *transport_defines('src/boards/rurp_serial_utils.cpp'),
                '-UNDEBUG', '-I', tmp_path, '-I', REPO_ROOT,
                '-I', REPO_ROOT / 'include',
                '-I', REPO_ROOT / 'test/native/avr/_shared',
                driver, backend_obj, '-o', binary])
    result = run_binary(binary)
    assert result.returncode == 0, result.stderr
