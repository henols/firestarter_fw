"""Large PY32 USB frames: production COBS/CRC code and backpressure handling.

Uses the same host-compiler / real-compile-database seam as other ARM tests.
The mocked USB controller only supplies packets and completion callbacks.
"""
import pytest

from ._host_build import REPO_ROOT, compile_ok, host_cc, host_cxx, run_binary
from ._py32_build import arm_defines, pinmap_flag, with_define


@pytest.mark.parametrize('stress_size', [None, 4096], ids=['production', '4k-stress'])
@pytest.mark.parametrize('rom_touch', [False, True], ids=['normal', 'rom-touch'])
def test_large_usb_frames_and_stalled_host(tmp_path, stress_size, rom_touch):
    def transport_defines(source):
        defines = arm_defines(source)
        if rom_touch and source == 'platform/py32f071/src/usb_cdc.c':
            if not pinmap_flag(defines, 'RURP_PY32F071_HAS_ROM_TOUCH'):
                pytest.skip('the pin map has no ROM USB touch')
            defines = with_define(defines, 'RURP_MICROPETAL_ROM_TOUCH', 1)
        elif source == 'platform/py32f071/src/usb_cdc.c':
            defines = with_define(defines, 'RURP_MICROPETAL_ROM_TOUCH', 0)
        # The real image stays at its CMake size. This second configuration
        # stresses streaming across a much smaller USB ring, not shipped RAM.
        if stress_size is not None:
            defines = with_define(defines, 'DATA_BUFFER_SIZE', stress_size)
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
#define RURP_DELAY_US(value) ((void)(value))
''')
    (tmp_path / 'usbd_cdc.h').write_text('#include "usbd_core.h"\nstruct cdc_line_coding { uint32_t dwDTERate; };\n')
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
extern bool fake_configured;
static inline bool usb_device_is_configured(void) { return fake_configured; }
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
bool fs_session_changed(void);
static inline void _firestarter_emit_frame(uint8_t id, const uint8_t *params, uint8_t param_count) {
    _firestarter_emit_frame_wide(id, params, param_count);
}
''')
    # The board API with RURP_HAS_HOST_SESSIONS 1: the serial layer calls
    # the session hooks, and the production usb_cdc.c defines them. Both the
    # C backend and the C++ serial layer include it.
    (tmp_path / 'rurp_shield.h').write_text('''
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t rurp_communication_session(void);
int rurp_communication_tx_failed(void);
void rurp_communication_frame_begin(void);
void rurp_communication_frame_end(void);
#ifdef __cplusplus
}
#endif
''')

    backend = tmp_path / 'usb_backend.c'
    backend.write_text(r'''
#include <assert.h>
#include <string.h>
#include "platform/py32f071/src/usb_cdc.c"

/* Capture the independent 3000-byte streaming case as well as frames. */
static uint8_t received[DATA_BUFFER_SIZE + 3000U + 32U];
static uint8_t pending_packet[64];
static size_t received_count;
static size_t packet_count;
static size_t zlp_count;
/* Size of each IN packet, in order. A ZLP is 0. */
static uint8_t packet_log[4096];
static size_t packet_log_count;
bool fake_configured = true;
static bool immediate_host;
static bool bus_reset_after_in;
static size_t event_received, event_pending;
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
static uint32_t slow_period;
static bool measure_cs;
static uint16_t cs_tx_head;
static size_t max_cs_bytes;
static unsigned irq_state, unlock_event, timer_event, event_at;
static unsigned unlock_count_event, unlocks_left;
static bool event_fired;
static void host_read_in(void);
static void fire_event(unsigned kind) {
    event_fired = true;
    event_received = received_count;
    event_pending = pending ? pending_size : 0U;
    if (kind == 3) {
        /* Bus reset: the core clears the configuration and the controller
         * forgets the endpoint transfer. No callback follows. */
        pending = false;
        host_out_armed = false;
        fake_configured = false;
    } else if (kind == 1) {
        pending = false; /* SET_CONFIGURATION cancels endpoint transfers. */
        host_out_armed = false;
        host_rx_count = host_rx_offset = 0;
        usbd_configure_done_callback();
    } else {
        usbd_cdc_acm_set_dtr(0, false);
        usbd_cdc_acm_set_dtr(0, true);
    }
}
static void host_send_packet(const uint8_t *data, size_t n);
static unsigned mask_countdown;
static const uint8_t *mask_packet;
static size_t mask_packet_size;
unsigned test_irq_state(void) { return irq_state; }
void test_disable_irq(void) {
    assert(!irq_state);
    /* The ISR runs just before main masks IRQs: the host reopens the port and
     * its next command completes on the armed OUT buffer. */
    if (mask_countdown && --mask_countdown == 0) {
        event_fired = true;
        usbd_cdc_acm_set_dtr(0, false);
        usbd_cdc_acm_set_dtr(0, true);
        host_send_packet(mask_packet, mask_packet_size);
    }
    irq_state = 1;
    cs_tx_head = tx_head;
}
/* Fire the reopen at the n-th IRQ mask from now. */
void test_reopen_at_mask(unsigned n, const uint8_t *packet, size_t size) {
    mask_countdown = n; mask_packet = packet; mask_packet_size = size;
    event_fired = false;
}
void test_enable_irq(void) {
    assert(irq_state); irq_state = 0;
    if (measure_cs) {
        const size_t queued = (size_t)((tx_head + FIRESTARTER_USB_TX_CAPACITY - cs_tx_head) %
                                       FIRESTARTER_USB_TX_CAPACITY);
        if (queued > max_cs_bytes) max_cs_bytes = queued;
    }
    if (unlock_event) { unsigned kind = unlock_event; unlock_event = 0; fire_event(kind); }
    if (unlock_count_event && --unlocks_left == 0) {
        unsigned kind = unlock_count_event; unlock_count_event = 0; fire_event(kind);
    }
    if (immediate_host) { while (pending && !paused) host_read_in(); }
}
void test_event_during_frame(unsigned kind) {
    timer_event = kind; event_at = now_ms + 2; event_fired = false;
}
void test_event_after(unsigned kind, uint32_t ms) {
    timer_event = kind; event_at = now_ms + ms; event_fired = false;
}
/* Fire the event at the n-th critical section exit from now. */
void test_event_after_unlocks(unsigned kind, unsigned n) {
    unlock_count_event = kind; unlocks_left = n; event_fired = false;
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

/* Issue henols/firestarter#183: the host opens the port (DTR edge) and sends a
 * command at once, before main applies the session reset. */
static void host_send_packet(const uint8_t *data, size_t n) {
    assert(host_out_armed && n <= sizeof(cdc_out_packet));
    memcpy(cdc_out_packet, data, n);
    host_out_armed = false;
    firestarter_cdc_bulk_out(FIRESTARTER_CDC_OUT_EP, (uint32_t)n);
}
static void expect_rx(const uint8_t *data, size_t n) {
    assert(py32_usb_available() == (int)n);
    for (size_t i = 0; i < n; ++i) assert(py32_usb_read() == data[i]);
    assert(py32_usb_available() == 0 && host_out_armed);
}
void test_dtr_edge_then_command(void) {
    const uint8_t stale[] = {0xEE, 0xEE, 0xEE};
    const uint8_t cmd[] = {'#', 0x03, 0x11, 0x22, 0x00};
    const uint8_t next[] = {0x05, 0x06};
    uint8_t full[64];
    for (size_t i = 0; i < sizeof(full); ++i) full[i] = (uint8_t)(0x40 + i);
    (void)py32_usb_available();
    assert(host_out_armed);

    /* Old-session bytes queued, then reopen, then the command arrives on the
     * OUT buffer that was armed before the edge. Main has not run yet. */
    host_send_packet(stale, sizeof(stale));
    (void)py32_usb_available();
    usbd_cdc_acm_set_dtr(0, false);
    usbd_cdc_acm_set_dtr(0, true);
    assert(host_out_armed);
    host_send_packet(cmd, sizeof(cmd));
    /* OUT stays disarmed: the host gets NAK, nothing is overwritten. */
    assert(!host_out_armed && !rx_armed);
    expect_rx(cmd, sizeof(cmd));            /* stale bytes gone, command kept */
    host_send_packet(next, sizeof(next));   /* normal flow resumes */
    expect_rx(next, sizeof(next));

    /* A full 64-byte packet is kept whole. */
    usbd_cdc_acm_set_dtr(0, false);
    usbd_cdc_acm_set_dtr(0, true);
    host_send_packet(full, sizeof(full));
    expect_rx(full, sizeof(full));

    /* A packet held for an earlier edge belongs to the session that the next
     * edge ends: open, packet held, close before main runs -> discarded. */
    usbd_cdc_acm_set_dtr(0, false);
    usbd_cdc_acm_set_dtr(0, true);
    host_send_packet(stale, sizeof(stale));
    assert(!host_out_armed);
    usbd_cdc_acm_set_dtr(0, false);
    assert(py32_usb_available() == 0 && host_out_armed);
    usbd_cdc_acm_set_dtr(0, true);
    host_send_packet(cmd, sizeof(cmd));
    expect_rx(cmd, sizeof(cmd));

    /* SET_CONFIGURATION cancels endpoint ownership: nothing held survives. */
    usbd_cdc_acm_set_dtr(0, false);
    host_send_packet(stale, sizeof(stale));
    usbd_configure_done_callback();
    assert(py32_usb_available() == 0 && host_out_armed);
    usbd_cdc_acm_set_dtr(0, true);
    (void)py32_usb_available();

    /* DTR drop, then a completed OUT packet, then main runs -> expect empty RX queue. */
    usbd_cdc_acm_set_dtr(0, false);
    host_send_packet(stale, sizeof(stale));
    (void)py32_usb_available();
    assert(py32_usb_available() == 0 && host_out_armed);
}

void fake_ep_write(uint8_t ep, const uint8_t *data, size_t count) {
    assert(ep == FIRESTARTER_CDC_IN_EP && count <= sizeof(pending_packet));
    assert(!pending);
    if (count) memcpy(pending_packet, data, count);
    pending_size = count;
    if (count) ++packet_count; else ++zlp_count;
    assert(packet_log_count < sizeof(packet_log));
    packet_log[packet_log_count++] = (uint8_t)count;
    pending = true;
}

/* The host reads the pending IN packet. The controller then calls the
 * completion callback with the packet length, ZLP included (usb_dc_py32.c). */
static void host_read_in(void) {
    if (irq_state || !pending || paused) return;
    const size_t sent = pending_size;
    pending = false;
    assert(received_count + sent <= sizeof(received));
    if (sent) memcpy(received + received_count, pending_packet, sent);
    received_count += sent;
    firestarter_cdc_bulk_in(FIRESTARTER_CDC_IN_EP, (uint32_t)sent);
    if (bus_reset_after_in) { bus_reset_after_in = false; fire_event(3); }
}

uint32_t test_millis(void) {
    ++now_ms;
    pump_host_rx();
    if (timer_event && now_ms >= event_at) {
        unsigned kind = timer_event; timer_event = 0; fire_event(kind);
    }
    if (immediate_host) { while (pending && !paused) host_read_in(); }
    else if (!slow_period || now_ms % slow_period == 0) host_read_in();
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
    slow_period = 0U;
    measure_cs = false;
    max_cs_bytes = 0U;
    pending = false;
    received_count = 0U;
    packet_count = 0U;
    zlp_count = 0U;
    packet_log_count = 0U;
    immediate_host = false;
    bus_reset_after_in = false;
    fake_configured = true;
    timer_event = 0U;
    unlock_count_event = 0U;
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
/* Host drains one IN packet every `period` ms, so the TX ring stays full
 * for far longer than the timeout while bytes keep moving. */
void test_slow_host(uint32_t period) { slow_period = period; }
/* 0: the host reads each IN packet as soon as the ISR can run. */
void test_host_period(uint32_t period) {
    immediate_host = period == 0U;
    slow_period = period;
}
void test_bus_reset_after_next_in(void) { bus_reset_after_in = true; }
void test_bus_reset(void) { fire_event(3); }
void test_dtr_edge(void) { fire_event(2); }
size_t test_packet_log(const uint8_t **sizes) { *sizes = packet_log; return packet_log_count; }
size_t test_event_received(void) { return event_received; }
size_t test_event_pending(void) { return event_pending; }
int test_tx_batching(void) { return tx_batching; }
int test_tx_zlp_due(void) { return tx_zlp_due; }
int test_session_tx_failed(void) { return tx_failed; }
void test_measure_critical_sections(void) { measure_cs = true; max_cs_bytes = 0U; }
size_t test_max_critical_section_bytes(void) { return max_cs_bytes; }
uint32_t test_now(void) { return now_ms; }
size_t test_packet_count(void) { return packet_count; }
size_t test_zlp_count(void) { return zlp_count; }
size_t test_tx_length(void) { return received_count; }
const uint8_t *test_tx_bytes(void) { return received; }
void test_rom_touch(void) {
    assert(!py32_usb_take_rom_request());
#if RURP_MICROPETAL_ROM_TOUCH
    struct cdc_line_coding coding = {250000};
    usbd_cdc_acm_set_dtr(0, false);
    usbd_cdc_acm_set_line_coding(0, &coding);
    usbd_cdc_acm_set_dtr(0, true);
    usbd_cdc_acm_set_dtr(0, false);
    assert(!py32_usb_take_rom_request());
    coding.dwDTERate = 1200;
    usbd_cdc_acm_set_line_coding(1, &coding); // wrong interface
    usbd_cdc_acm_set_dtr(0, true);
    usbd_cdc_acm_set_dtr(0, false);
    assert(!py32_usb_take_rom_request());
    usbd_cdc_acm_set_line_coding(0, &coding);
    assert(!py32_usb_take_rom_request()); // rate alone is not a trigger
    usbd_cdc_acm_set_dtr(0, true);
    usbd_cdc_acm_set_dtr(0, false);
    assert(py32_usb_take_rom_request());
    assert(!py32_usb_take_rom_request()); // consumed exactly once
    coding.dwDTERate = 250000;
    usbd_cdc_acm_set_line_coding(0, &coding);
    (void)py32_usb_available(); // apply DTR session reset before RX tests
#endif
}

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
void test_rom_touch(void);
void test_rx_backpressure(void);
void test_feed_rx(const uint8_t *, size_t);
void test_prepare_tx(int);
void test_slow_host(uint32_t);
void test_host_period(uint32_t);
void test_bus_reset_after_next_in(void);
void test_bus_reset(void);
void test_dtr_edge(void);
size_t test_packet_log(const uint8_t **);
size_t test_event_received(void);
size_t test_event_pending(void);
int test_tx_batching(void);
int test_tx_zlp_due(void);
int test_session_tx_failed(void);
void test_measure_critical_sections(void);
size_t test_max_critical_section_bytes(void);
uint32_t test_now(void);
size_t test_packet_count(void);
size_t test_zlp_count(void);
size_t test_tx_length(void);
const uint8_t *test_tx_bytes(void);
uint32_t test_millis(void);
int py32_usb_available(void);
int py32_usb_peek(void);
int py32_usb_read(void);
size_t py32_usb_read_bytes(char *, size_t);
size_t py32_usb_write(const uint8_t *, size_t);
void py32_usb_flush(void);
void rurp_communication_frame_begin(void);
void rurp_communication_frame_end(void);
uint32_t rurp_communication_session(void);
void test_reopen(void);
void test_event_during_frame(unsigned);
void test_event_after(unsigned, uint32_t);
void test_event_after_unlocks(unsigned, unsigned);
void test_assert_event_fired(void);
void test_ring_reset_ownership(void);
void test_arm_rx_interrupt(void);
void test_dtr_edge_then_command(void);
void test_reopen_at_mask(unsigned, const uint8_t *, size_t);
int rurp_communication_tx_failed(void);
}

// firestarter.cpp keeps the session of the active command. Each scenario that
// stands for a command of the current session calls new_command() first.
static uint32_t command_session;
bool fs_session_changed(void) { return command_session != rurp_communication_session(); }
static void new_command(void) { command_session = rurp_communication_session(); }
static size_t writes_after_failure;
static size_t write_calls;

struct SerialMock {
    void begin(unsigned long) {}
    void end() {}
    explicit operator bool() const { return true; }
    int available() const { return py32_usb_available(); }
    int read() { return py32_usb_read(); }
    int peek() const { return py32_usb_peek(); }
    size_t readBytes(char *out, size_t n) { return py32_usb_read_bytes(out, n); }
    size_t write(uint8_t b) { note_failed_use(); ++write_calls; return py32_usb_write(&b, 1); }
    size_t write(const uint8_t *p, size_t n) { note_failed_use(); ++write_calls; return py32_usb_write(p, n); }
    void flush() { note_failed_use(); py32_usb_flush(); }
    static void note_failed_use() { if (rurp_communication_tx_failed()) ++writes_after_failure; }
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

static std::vector<uint8_t> expected_frame(uint8_t id, const uint8_t *params, size_t count) {
    std::vector<uint8_t> out = {0xAA, 0x55, 0xAA, 0x55};
    const uint16_t length = (uint16_t)(count + 2);
    out.push_back(uint8_t(length >> 8));
    out.push_back(uint8_t(length));
    out.push_back(id);
    out.insert(out.end(), params, params + count);
    std::vector<uint8_t> crc_input = {id};
    crc_input.insert(crc_input.end(), params, params + count);
    out.push_back(ref_crc8(crc_input.data(), crc_input.size()));
    out.push_back(0x0A);
    return out;
}

/* One frame is one USB transfer: full packets, then one short packet. The
 * short packet is a ZLP only when the frame ends on a packet boundary. */
static void assert_one_transfer(size_t total) {
    const uint8_t *sizes;
    const size_t count = test_packet_log(&sizes);
    assert(count > 0);
    size_t bytes = 0, zlps = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i + 1 < count) assert(sizes[i] == 64);
        else assert(sizes[i] < 64);
        if (sizes[i] == 0) ++zlps;
        bytes += sizes[i];
    }
    assert(bytes == total);
    assert(zlps == (total % 64 == 0 ? 1U : 0U));
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
    test_rom_touch();
    test_rx_backpressure();
    test_dtr_edge_then_command();
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
    new_command();
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
    // A ZLP only after a last packet that is full, never between packets.
    assert(test_zlp_count() == (expected.size() % 64 == 0 ? 1U : 0U));
    assert(test_tx_length() == expected.size());
    assert(memcmp(test_tx_bytes(), expected.data(), expected.size()) == 0);

    // A slow but steady host keeps the TX ring full for much longer than the
    // timeout. Each accepted byte or IN completion is progress, so the write
    // must complete. 75 ms is below the timeout for one IN completion, but
    // above it for two. Interrupts are re-enabled after at most one USB
    // packet of copying.
    for (uint32_t period : {20U, 75U}) {
        std::vector<uint8_t> bulk(3000);
        for (size_t i = 0; i < bulk.size(); ++i) bulk[i] = uint8_t(i * 7 + 1);
        test_prepare_tx(0);
        test_slow_host(period);
        test_measure_critical_sections();
        assert(py32_usb_write(bulk.data(), bulk.size()) == bulk.size());
        assert(!rurp_communication_tx_failed());
        assert(test_now() > 2 * 100); // ring stayed full far beyond the timeout
        assert(test_max_critical_section_bytes() <= 64);
        assert(test_max_critical_section_bytes() > 0);
        py32_usb_flush();
        assert(!rurp_communication_tx_failed());
        assert(test_tx_length() == bulk.size());
        assert(memcmp(test_tx_bytes(), bulk.data(), bulk.size()) == 0);
        assert(bulk.size() % 64 != 0 && test_zlp_count() == 0);
    }

    // A frame that ends on a packet boundary needs a ZLP after its last
    // packet. The ZLP completion is progress, so a slow host does not fail
    // the flush.
    {
        test_prepare_tx(0);
        new_command();
        test_slow_host(75);
        const uint16_t params = 3 * 64 - 9;
        rurp_log_id_wide(id, mixed.data(), params);
        assert(!rurp_communication_tx_failed());
        assert(test_now() > 3 * 75);
        assert(test_tx_length() == 3 * 64);
        assert(test_zlp_count() == 1);
        assert(memcmp(test_tx_bytes() + 7, mixed.data(), params) == 0);
    }

    // Every frame size from 0 to 200 parameter bytes (9 to 209 bytes on the
    // wire, so each packet boundary), for an immediate, a 1 ms and a 75 ms
    // host. The frame arrives complete and in order, as one transfer, with
    // no timeout.
    for (uint32_t period : {0U, 1U, 75U}) {
        for (uint16_t params = 0; params <= 200; ++params) {
            test_prepare_tx(0);
            new_command();
            test_host_period(period);
            writes_after_failure = 0;
            write_calls = 0;
            rurp_log_id_wide(id, mixed.data(), params);
            assert(!rurp_communication_tx_failed() && !test_session_tx_failed());
            assert(writes_after_failure == 0);
            // Each write call has a fixed transport cost: header, params
            // and trailer take one call each, whatever the frame size.
            assert(write_calls == (params ? 3U : 2U));
            const auto want = expected_frame(id, mixed.data(), params);
            assert(test_tx_length() == want.size());
            assert(memcmp(test_tx_bytes(), want.data(), want.size()) == 0);
            assert_one_transfer(want.size());
            assert(!test_tx_batching() && !test_tx_zlp_due());
        }
    }

    // A DTR edge in the middle of a frame, during the params write. Only
    // the packet already given to the controller can arrive after the edge.
    // The next frame is complete, it does not time out, and no byte of the
    // old frame follows it.
    for (uint32_t period : {0U, 1U, 75U}) {
        const uint16_t params = 500;
        const std::vector<uint8_t> tiny = {3, 0, 5};
        const auto old_frame = expected_frame(id, mixed.data(), params);
        test_prepare_tx(0);
        new_command();
        test_host_period(period);
        // Critical sections: frame begin, header, first params packet. The
        // edge comes after the first packet is in the controller. A 75 ms
        // host reads it at 75 ms, during the flush of the next frame. That
        // old-session completion must start the new frame.
        test_event_after_unlocks(2, 3);
        writes_after_failure = 0;
        rurp_log_id_wide(id, mixed.data(), params);
        test_assert_event_fired();
        assert(rurp_communication_tx_failed());
        assert(writes_after_failure == 0);
        assert(!test_tx_batching() && !test_tx_zlp_due());
        const size_t old_bytes = test_event_received() + test_event_pending();
        assert(test_event_pending() == 64);
        assert(old_bytes < old_frame.size());
        new_command();
        writes_after_failure = 0;
        rurp_log_id_wide(id, tiny.data(), (uint16_t)tiny.size());
        assert(!rurp_communication_tx_failed() && !test_session_tx_failed());
        assert(writes_after_failure == 0);
        const auto want = expected_frame(id, tiny.data(), tiny.size());
        assert(test_tx_length() == old_bytes + want.size());
        assert(memcmp(test_tx_bytes(), old_frame.data(), old_bytes) == 0);
        assert(memcmp(test_tx_bytes() + old_bytes, want.data(), want.size()) == 0);
        assert(!test_tx_batching() && !test_tx_zlp_due());
    }

    // A bus reset after the last full packet of a frame: all data is
    // delivered, only the ZLP is not. The flush must not wait for the
    // timeout or fail the frame.
    {
        test_prepare_tx(0);
        new_command();
        test_host_period(1);
        test_bus_reset_after_next_in();
        const uint16_t params = 64 - 9;
        rurp_log_id_wide(id, mixed.data(), params);
        assert(!rurp_communication_tx_failed() && !test_session_tx_failed());
        assert(test_tx_length() == 64 && test_zlp_count() == 0);
        assert(!test_tx_zlp_due());
    }

    // A bus reset with bytes still queued: the flush fails at once.
    {
        test_prepare_tx(1);
        rurp_communication_frame_begin();
        assert(py32_usb_write(mixed.data(), 30) == 30);
        test_bus_reset();
        const uint32_t start = test_now();
        py32_usb_flush();
        assert(test_now() - start < 5U);
        rurp_communication_frame_end();
        assert(rurp_communication_tx_failed());
        assert(!test_tx_batching());
    }

    // A DTR edge between a write and the flush outside a frame. The flush
    // discards the old bytes, waits only for the packet in the controller,
    // and does not latch a TX failure.
    {
        test_prepare_tx(0);
        test_host_period(75);
        assert(py32_usb_write(mixed.data(), 100) == 100);
        test_dtr_edge();
        const uint32_t start = test_now();
        py32_usb_flush();
        assert(!test_session_tx_failed());
        assert(test_now() - start <= 75U + 5U);
        assert(test_tx_length() == 64);
        assert(memcmp(test_tx_bytes(), mixed.data(), 64) == 0);
    }

    // Stalled USB IN endpoint: the write is incomplete and fails closed, and
    // the emitter stops writing or flushing as soon as the frame fails.
    test_prepare_tx(1);
    new_command();
    writes_after_failure = 0;
    rurp_log_id_wide(id, mixed.data(), (uint16_t)mixed.size());
    assert(rurp_communication_tx_failed());
    assert(writes_after_failure == 0);
    assert(py32_usb_write(mixed.data(), 1) == 0);

    // Reopening the port, without SET_CONFIGURATION, recovers a stalled link.
    test_reopen();
    new_command();
    const std::vector<uint8_t> small = {3, 0, 5};
    rurp_log_id_wide(id, small.data(), (uint16_t)small.size());
    assert(!rurp_communication_tx_failed());
    assert(test_tx_length() == small.size() + 9);
    assert(test_tx_bytes()[6] == id);

    // Reconfiguration and DTR reopen in the middle of the real frame emitter:
    // no remainder of the old frame may enter the new session's TX queue.
    for (unsigned event : {1U, 2U}) {
        test_prepare_tx(0);
        new_command();
        test_event_during_frame(event);
        rurp_log_id_wide(id, mixed.data(), (uint16_t)mixed.size());
        test_assert_event_fired();
        assert(rurp_communication_tx_failed());
        const size_t before = test_tx_length();
        assert(before <= 1064); // at most pre-event bytes + one hardware packet
        new_command();
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
    // The host reopens the port while a command runs, and main applies the
    // session reset before the command starts its next frame. No frame of
    // the old command may reach the new host: here a read step, the
    // MSG_DATA_SENDING frame and its MSG_DATA_CHUNK.
    test_prepare_tx(0);
    new_command();
    rurp_log_id_wide(id, small.data(), (uint16_t)small.size());
    assert(!rurp_communication_tx_failed() && test_tx_length() == small.size() + 9);
    test_reopen();
    assert(rurp_communication_available() == 0);  // main applied the reset
    rurp_log_id(MSG_DATA_SENDING, nullptr, 0);
    rurp_log_id_wide(id, mixed.data(), (uint16_t)mixed.size());
    for (int i = 0; i < 100; ++i) (void)test_millis();
    assert(test_tx_length() == 0);
    // The new session's command gets its frames.
    new_command();
    rurp_log_id_wide(id, small.data(), (uint16_t)small.size());
    assert(!rurp_communication_tx_failed());
    assert(test_tx_length() == small.size() + 9 && test_tx_bytes()[6] == id);

    // The reopen and the next command arrive just before the frame start
    // masks IRQs. The frame is dropped and the command is kept.
    test_prepare_tx(0);
    new_command();
    const uint8_t next_cmd[] = {'#', 0x02, 0x07, 0x00};
    test_reopen_at_mask(1, next_cmd, sizeof(next_cmd));
    rurp_log_id_wide(id, small.data(), (uint16_t)small.size());
    test_assert_event_fired();
    for (int i = 0; i < 100; ++i) (void)test_millis();
    assert(test_tx_length() == 0);
    assert(rurp_communication_available() == (int)sizeof(next_cmd));

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

    // The reopen and the next command arrive between the decoder's generation
    // check and its read (the 4th mask: available, read, available, read).
    // The read must not consume byte 0 of the kept command.
    std::vector<uint8_t> kept = {'#'};
    kept.insert(kept.end(), encoded.begin(), encoded.end());
    test_feed_rx(encoded.data(), 2);
    test_reopen_at_mask(4, kept.data(), kept.size());
    assert(rurp_communication_read_data(decoded.data(), DATA_BUFFER_SIZE) < 0);
    test_assert_event_fired();
    assert(rurp_communication_available() == (int)kept.size());
    assert(rurp_communication_read() == '#');
    assert(rurp_communication_read_data(decoded.data(), DATA_BUFFER_SIZE) == (int)good.size());
    assert(memcmp(decoded.data(), good.data(), good.size()) == 0);
    return 0;
}
''')

    backend_obj = tmp_path / 'usb_backend.o'
    compile_ok([host_cc(), '-std=c11',
                *transport_defines('platform/py32f071/src/usb_cdc.c'),
                '-UNDEBUG', '-I', tmp_path, '-I', REPO_ROOT,
                '-I', REPO_ROOT / 'include',
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
