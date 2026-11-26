#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boards/py32f071_rurp_shield.h"
#include "rurp_platform.h"
#include "rurp_shield.h"
#include "usbd_cdc.h"
#include "usbd_core.h"

#define FIRESTARTER_CDC_IN_EP 0x81U
#define FIRESTARTER_CDC_OUT_EP 0x02U
#define FIRESTARTER_CDC_INT_EP 0x83U
#define FIRESTARTER_CDC_MAX_PACKET_SIZE 64U
/* OUT is armed only when a complete USB packet fits. The host receives NAK
 * backpressure instead of losing bytes while main drains the fixed-size ring. */
#define FIRESTARTER_USB_RX_CAPACITY 1024U
#define FIRESTARTER_USB_TX_CAPACITY 1024U
#define FIRESTARTER_USB_TIMEOUT_MS 100U

#ifndef FIRESTARTER_USB_VID
#define FIRESTARTER_USB_VID 0x1209U
#endif

#ifndef FIRESTARTER_USB_PID
#define FIRESTARTER_USB_PID 0x0001U
#endif

/* 1209:0001 is a pid.codes private-testing pair, NOT an allocated PID.
 * It is not universally unique and must not be used outside a test
 * environment. Do not ship a board under this identity until a PID is
 * allocated under VID 0x1209 (see DESIGN.md, "USB identity"). */

#define FIRESTARTER_USB_CONFIG_SIZE (9U + CDC_ACM_DESCRIPTOR_LEN)

static const uint8_t firestarter_cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(
        USB_2_0,
        0xEF,
        0x02,
        0x01,
        FIRESTARTER_USB_VID,
        FIRESTARTER_USB_PID,
        0x0100,
        0x01),
    USB_CONFIG_DESCRIPTOR_INIT(
        FIRESTARTER_USB_CONFIG_SIZE,
        0x02,
        0x01,
        USB_CONFIG_BUS_POWERED,
        100),
    CDC_ACM_DESCRIPTOR_INIT(
        0x00,
        FIRESTARTER_CDC_INT_EP,
        FIRESTARTER_CDC_OUT_EP,
        FIRESTARTER_CDC_IN_EP,
        0x02),

    USB_LANGID_INIT(0x0409),

    24, USB_DESCRIPTOR_TYPE_STRING,
    'F', 0, 'i', 0, 'r', 0, 'e', 0, 's', 0, 't', 0,
    'a', 0, 'r', 0, 't', 0, 'e', 0, 'r', 0,

    42, USB_DESCRIPTOR_TYPE_STRING,
    'F', 0, 'i', 0, 'r', 0, 'e', 0, 's', 0, 't', 0,
    'a', 0, 'r', 0, 't', 0, 'e', 0, 'r', 0, ' ', 0,
    'P', 0, 'Y', 0, '3', 0, '2', 0, 'F', 0, '0', 0, '7', 0, '1', 0,

    18, USB_DESCRIPTOR_TYPE_STRING,
    '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '1', 0,

    0x00
};

USB_MEM_ALIGNX static uint8_t cdc_out_packet[FIRESTARTER_CDC_MAX_PACKET_SIZE];
USB_MEM_ALIGNX static uint8_t cdc_in_packet[FIRESTARTER_CDC_MAX_PACKET_SIZE];

static uint8_t rx_buffer[FIRESTARTER_USB_RX_CAPACITY];
static uint8_t tx_buffer[FIRESTARTER_USB_TX_CAPACITY];

static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;
static volatile bool tx_busy;
/* Counts IN completions, ZLPs included. A change means the host is reading. */
static volatile uint32_t tx_in_completions;
/* Set while a frame is emitted: send full packets only. Flush and frame end
 * clear it and send the rest. */
static volatile bool tx_batching;
/* The last IN packet was full. The transfer needs a ZLP if no data follows. */
static volatile bool tx_zlp_due;
static volatile bool rx_armed;
/* ISR publishes session changes, main owns all ring resets. A frame keeps
 * its generation across every Serial.write call in the frame. */
static volatile uint32_t usb_generation;
static uint32_t applied_generation;
static uint32_t frame_generation;
static uint32_t packet_generation;
static volatile bool reset_pending;
static volatile bool endpoint_reset_pending;
/* Bytes of the first OUT packet that completed after a DTR edge that asserts
 * DTR, before main applied the session reset (0: none). A packet that completes
 * after a DTR drop is discarded. The packet stays in cdc_out_packet and
 * OUT stays disarmed, so the host gets NAK until main discards the old queue
 * and appends this packet. Once OUT is armed, this controller accepts one
 * packet; it cannot cancel that buffer without losing the packet. */
static volatile uint8_t held_out_count;
static volatile bool dtr_enabled;
static bool frame_active;
static bool frame_failed;
static volatile bool tx_failed;
static bool usb_initialized;

static struct usbd_interface cdc_control_interface;
static struct usbd_interface cdc_data_interface;

static uint16_t ring_next(uint16_t value, uint16_t capacity)
{
    ++value;
    return value == capacity ? 0U : value;
}

/* Called in the USB ISR or with IRQs disabled in main. Drops bytes that do
 * not fit. */
static void usb_rx_append(const uint8_t *data, uint32_t n)
{
    for (uint32_t index = 0U; index < n; ++index)
    {
        const uint16_t next = ring_next(rx_head, FIRESTARTER_USB_RX_CAPACITY);
        if (next == rx_tail)
        {
            break;
        }

        rx_buffer[rx_head] = data[index];
        rx_head = next;
    }
}

/* Called in the USB ISR or with IRQs disabled in main. */
static void usb_arm_receive(void)
{
    const uint16_t used = rx_head >= rx_tail ? rx_head - rx_tail
        : FIRESTARTER_USB_RX_CAPACITY - rx_tail + rx_head;
    if (!reset_pending && !rx_armed && usb_device_is_configured() &&
        FIRESTARTER_USB_RX_CAPACITY - 1U - used >= sizeof(cdc_out_packet))
    {
        rx_armed = true;
        if (usbd_ep_start_read(FIRESTARTER_CDC_OUT_EP, cdc_out_packet, sizeof(cdc_out_packet)) != 0)
            rx_armed = false;
    }
}

/* Called with IRQs disabled. Do not re-arm OUT in the ISR: no new-session
 * bytes may be accepted before the old RX queue has been discarded. */
static void usb_apply_session_reset(void)
{
    if (!reset_pending) return;
    rx_head = rx_tail = 0U;
    tx_head = tx_tail = 0U;
    /* SET_CONFIGURATION cancels hardware transfers. DTR does not: retain the
     * in-flight packet/buffer until its completion (and never append a ZLP). */
    if (endpoint_reset_pending) {
        tx_busy = false;
        rx_armed = false;
    }
    endpoint_reset_pending = false;
    tx_zlp_due = false;
    reset_pending = false;
    applied_generation = usb_generation;
    tx_failed = false;
    /* The new session's first command, received while the reset was pending. */
    if (held_out_count != 0U)
    {
        usb_rx_append(cdc_out_packet, held_out_count);
        held_out_count = 0U;
    }
    usb_arm_receive();
}

static void usb_restore_irq(uint32_t state)
{
    if (state == 0U) __enable_irq();
}

static void usb_start_next_transmit(void)
{
    if (reset_pending || tx_failed || tx_busy || !usb_device_is_configured())
    {
        return;
    }
    const uint16_t queued = (uint16_t)((tx_head + FIRESTARTER_USB_TX_CAPACITY - tx_tail) %
                                       FIRESTARTER_USB_TX_CAPACITY);
    if (tx_batching ? queued < FIRESTARTER_CDC_MAX_PACKET_SIZE
                    : queued == 0U && !tx_zlp_due)
    {
        return;
    }

    /* With no data queued, count stays 0 and this sends the ZLP. */
    size_t count = 0U;
    uint16_t tail = tx_tail;
    const uint16_t head = tx_head;
    while (count < sizeof(cdc_in_packet) && tail != head)
    {
        cdc_in_packet[count++] = tx_buffer[tail];
        tail = ring_next(tail, FIRESTARTER_USB_TX_CAPACITY);
    }
    tx_tail = tail;

    tx_zlp_due = false;
    tx_busy = true;
    packet_generation = usb_generation;
    if (usbd_ep_start_write(FIRESTARTER_CDC_IN_EP, cdc_in_packet, count) != 0)
    {
        tx_busy = false;
        tx_failed = true;
    }
}

static void usb_start_next_transmit_safe(void)
{
    const uint32_t interrupt_state = __get_PRIMASK();
    __disable_irq();
    usb_start_next_transmit();
    if (interrupt_state == 0U)
    {
        __enable_irq();
    }
}

void usbd_configure_done_callback(void)
{
    ++usb_generation;
    reset_pending = true;
    endpoint_reset_pending = true;
    held_out_count = 0U;
}

static void firestarter_cdc_bulk_out(uint8_t endpoint, uint32_t byte_count)
{
    (void)endpoint;
    rx_armed = false;
    if (reset_pending)
    {
        /* Keep the packet and leave OUT disarmed; see held_out_count. */
        if (!endpoint_reset_pending && dtr_enabled && byte_count <= sizeof(cdc_out_packet))
            held_out_count = (uint8_t)byte_count;
        return;
    }

    usb_rx_append(cdc_out_packet, byte_count);
    usb_arm_receive();
}

static void firestarter_cdc_bulk_in(uint8_t endpoint, uint32_t byte_count)
{
    (void)endpoint;
    ++tx_in_completions;

    /* A transfer that ends on a full packet needs a ZLP. Send it only when
     * no more data follows: queued data continues the transfer. An old
     * session packet gets no ZLP. */
    tx_zlp_due = !reset_pending && packet_generation == usb_generation &&
                 byte_count == FIRESTARTER_CDC_MAX_PACKET_SIZE;
    tx_busy = false;
    /* After a reset, the queue holds only new session bytes. Start them
     * also when the completed packet is from the old session. */
    usb_start_next_transmit();
}

static struct usbd_endpoint cdc_out_endpoint = {
    .ep_addr = FIRESTARTER_CDC_OUT_EP,
    .ep_cb = firestarter_cdc_bulk_out
};

static struct usbd_endpoint cdc_in_endpoint = {
    .ep_addr = FIRESTARTER_CDC_IN_EP,
    .ep_cb = firestarter_cdc_bulk_in
};

void usbd_cdc_acm_set_dtr(uint8_t interface_number, bool enabled)
{
    if (interface_number != 0U || dtr_enabled == enabled) return;
    dtr_enabled = enabled;
    ++usb_generation;
    reset_pending = true;
    /* A packet held for an earlier edge belongs to the session that this edge
     * ends. OUT stays disarmed until main applies the reset. */
    held_out_count = 0U;
}

void usbd_cdc_acm_set_rts(uint8_t interface_number, bool enabled)
{
    (void)interface_number;
    (void)enabled;
}

void rurp_communication_begin(void)
{
    if (usb_initialized)
    {
        return;
    }

    /* Static state starts empty. All later queue resets use
     * usb_apply_session_reset(), never a second initialization list. */

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_USB_CLK_ENABLE();

    usbd_desc_register(firestarter_cdc_descriptor);
    usbd_add_interface(usbd_cdc_acm_init_intf(&cdc_control_interface));
    usbd_add_interface(usbd_cdc_acm_init_intf(&cdc_data_interface));
    usbd_add_endpoint(&cdc_out_endpoint);
    usbd_add_endpoint(&cdc_in_endpoint);
    usbd_initialize();

    NVIC_EnableIRQ(USBD_IRQn);
    usb_initialized = true;
}

uint32_t rurp_communication_session(void)
{
    return usb_generation;
}

int py32_usb_available(void)
{
    const uint32_t state = __get_PRIMASK();
    __disable_irq();
    usb_apply_session_reset();
    usb_arm_receive();
    const uint16_t head = rx_head, tail = rx_tail;
    const int count = head >= tail ? (int)(head - tail)
        : (int)(FIRESTARTER_USB_RX_CAPACITY - tail + head);
    usb_restore_irq(state);
    return count;
}

int py32_usb_peek(void)
{
    const uint32_t state = __get_PRIMASK();
    __disable_irq();
    usb_apply_session_reset();
    const int value = rx_tail == rx_head ? -1 : (int)rx_buffer[rx_tail];
    usb_arm_receive();
    usb_restore_irq(state);
    return value;
}

/* A read that applies a session reset returns -1 and consumes nothing. The
 * caller then sees the new generation before it takes a new-session byte. */
int py32_usb_read(void)
{
    const uint32_t state = __get_PRIMASK();
    __disable_irq();
    const bool session_reset = reset_pending;
    usb_apply_session_reset();
    int value = -1;
    if (!session_reset && rx_tail != rx_head)
    {
        value = rx_buffer[rx_tail];
        rx_tail = ring_next(rx_tail, FIRESTARTER_USB_RX_CAPACITY);
    }
    usb_arm_receive();
    usb_restore_irq(state);
    return value;
}

/* Each log frame owns one generation; a reconnect cannot resume its tail. */
void rurp_communication_frame_begin(void)
{
    const uint32_t state = __get_PRIMASK();
    __disable_irq();
    usb_apply_session_reset();
    frame_active = true;
    frame_generation = applied_generation;
    frame_failed = tx_failed || !usb_device_is_configured();
    tx_batching = true;
    usb_restore_irq(state);
}

void rurp_communication_frame_end(void)
{
    /* A frame that stopped before its flush must not hold back queued bytes. */
    tx_batching = false;
    usb_start_next_transmit_safe();
    frame_failed = frame_failed || tx_failed || frame_generation != usb_generation;
    frame_active = false;
}

size_t py32_usb_read_bytes(char *buffer, size_t length)
{
    size_t count = 0U;

    while (count < length)
    {
        const int value = py32_usb_read();
        if (value < 0)
        {
            break;
        }

        buffer[count++] = (char)value;
    }

    return count;
}

size_t py32_usb_write(const uint8_t *buffer, size_t length)
{
    const uint32_t generation = frame_active ? frame_generation : usb_generation;
    size_t count = 0U;
    /* The deadline starts at the first pass without progress. A pass that
     * makes progress reads no clock. */
    bool stalled = false;
    uint32_t deadline = 0U;
    uint32_t completions = tx_in_completions;
    while (count < length)
    {
        const uint32_t state = __get_PRIMASK();
        __disable_irq();
        usb_apply_session_reset();
        if (generation != usb_generation)
        {
            frame_failed = true;
            usb_restore_irq(state);
            break;
        }
        if (tx_failed || !usb_initialized || !usb_device_is_configured())
        {
            frame_failed = tx_failed = true;
            usb_restore_irq(state);
            break;
        }
        /* Copy at most one USB packet per critical section, so the USB ISR
         * and millis() are never held off for a whole caller buffer. */
        const size_t before = count;
        const size_t limit = length - count > FIRESTARTER_CDC_MAX_PACKET_SIZE
            ? count + FIRESTARTER_CDC_MAX_PACKET_SIZE : length;
        uint16_t head = tx_head;
        const uint16_t tail = tx_tail;
        while (count < limit)
        {
            const uint16_t next = ring_next(head, FIRESTARTER_USB_TX_CAPACITY);
            if (next == tail)
            {
                break;
            }
            tx_buffer[head] = buffer[count++];
            head = next;
        }
        tx_head = head;
        usb_start_next_transmit();
        usb_restore_irq(state);
        /* An accepted byte or an IN completion is progress. Time out only
         * after FIRESTARTER_USB_TIMEOUT_MS without progress. */
        if (count != before || completions != tx_in_completions)
        {
            completions = tx_in_completions;
            stalled = false;
            continue;
        }
        const uint32_t now = RURP_MILLIS();
        if (!stalled)
        {
            stalled = true;
            deadline = now + FIRESTARTER_USB_TIMEOUT_MS;
        }
        else if ((int32_t)(now - deadline) >= 0)
        {
            frame_failed = tx_failed = true;
            break;
        }
        RURP_DELAY_US(10);
    }
    return count;
}

void py32_usb_flush(void)
{
    const uint32_t generation = frame_active ? frame_generation : usb_generation;
    uint32_t deadline = RURP_MILLIS() + FIRESTARTER_USB_TIMEOUT_MS;
    uint32_t completions = tx_in_completions;
    const uint32_t state = __get_PRIMASK();
    __disable_irq();
    /* Bytes queued before a session change are not sent. Discard them now,
     * so that they do not cause a timeout. */
    usb_apply_session_reset();
    tx_batching = false;
    usb_start_next_transmit();
    usb_restore_irq(state);
    while (tx_head != tx_tail || tx_busy || tx_zlp_due)
    {
        if (generation != usb_generation)
        {
            frame_failed = true;
            return;
        }
        if (!usb_device_is_configured())
        {
            /* A bus reset stopped the endpoint. A lost ZLP loses no data, so
             * the frame fails only when data stays unsent. */
            tx_zlp_due = false;
            if (tx_head != tx_tail || tx_busy) frame_failed = tx_failed = true;
            return;
        }
        const uint32_t now = RURP_MILLIS();
        /* Each IN completion, ZLP included, is progress. A slow host that
         * keeps reading must not time out on the total frame duration. */
        if (completions != tx_in_completions)
        {
            completions = tx_in_completions;
            deadline = now + FIRESTARTER_USB_TIMEOUT_MS;
        }
        if (tx_failed || (int32_t)(now - deadline) >= 0)
        {
            frame_failed = tx_failed = true;
            return;
        }
    }
    if (generation != usb_generation) frame_failed = true;
}

int rurp_communication_tx_failed(void)
{
    /* Result of this frame, not an earlier frame's error cleared by a reconnect. */
    return frame_failed;
}

void USB_IRQHandler(void)
{
    USBD_IRQHandler();
}
