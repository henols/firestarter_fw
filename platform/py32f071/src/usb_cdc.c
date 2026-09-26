#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boards/py32f071_rurp_shield.h"
#include "rurp_platform.h"
#include "usbd_cdc.h"
#include "usbd_core.h"

#define FIRESTARTER_CDC_IN_EP 0x81U
#define FIRESTARTER_CDC_OUT_EP 0x02U
#define FIRESTARTER_CDC_INT_EP 0x83U
#define FIRESTARTER_CDC_MAX_PACKET_SIZE 64U
#define FIRESTARTER_USB_RX_CAPACITY 1024U
#define FIRESTARTER_USB_TX_CAPACITY 1024U
#define FIRESTARTER_USB_TIMEOUT_MS 100U

#ifndef FIRESTARTER_USB_VID
#define FIRESTARTER_USB_VID 0x1209U
#endif

#ifndef FIRESTARTER_USB_PID
#define FIRESTARTER_USB_PID 0x0001U
#endif

/*
 * FIRESTARTER_USB_VID/FIRESTARTER_USB_PID present pid.codes' documented
 * private-testing pair 1209:0001 under pid.codes' community VID 0x1209.
 * This is NOT an allocated PID.
 *
 * pid.codes' terms ask that any source referencing this id carry a warning
 * that the PID is not universally unique and must not be relied on outside
 * a test environment -- this firmware carries that warning here.
 *
 * This replaces the previous pair 0x36B7/0xFFFF, which is registered to
 * Puya Semiconductor and was copied verbatim from the pinned SDK's own USB
 * CDC example. Before this change, this board presented another company's
 * registered vendor identity on a product they did not make.
 *
 * The decision this pair implements lives in
 * platform/py32f071/FLASH-PATH-AND-PCB.md section 5. Its ship gate stands
 * unchanged: no board ships and no release advertises a USB identity until
 * a PID allocated under VID 0x1209 exists.
 */

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
static bool usb_initialized;

static struct usbd_interface cdc_control_interface;
static struct usbd_interface cdc_data_interface;

static uint16_t ring_next(uint16_t value, uint16_t capacity)
{
    ++value;
    return value == capacity ? 0U : value;
}

static void usb_start_next_transmit(void)
{
    if (tx_busy || tx_head == tx_tail || !usb_device_is_configured())
    {
        return;
    }

    size_t count = 0U;
    while (count < sizeof(cdc_in_packet) && tx_tail != tx_head)
    {
        cdc_in_packet[count++] = tx_buffer[tx_tail];
        tx_tail = ring_next(tx_tail, FIRESTARTER_USB_TX_CAPACITY);
    }

    tx_busy = true;
    usbd_ep_start_write(FIRESTARTER_CDC_IN_EP, cdc_in_packet, count);
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
    usbd_ep_start_read(
        FIRESTARTER_CDC_OUT_EP,
        cdc_out_packet,
        sizeof(cdc_out_packet));
    usb_start_next_transmit();
}

static void firestarter_cdc_bulk_out(uint8_t endpoint, uint32_t byte_count)
{
    (void)endpoint;

    for (uint32_t index = 0U; index < byte_count; ++index)
    {
        const uint16_t next = ring_next(rx_head, FIRESTARTER_USB_RX_CAPACITY);
        if (next == rx_tail)
        {
            break;
        }

        rx_buffer[rx_head] = cdc_out_packet[index];
        rx_head = next;
    }

    usbd_ep_start_read(
        FIRESTARTER_CDC_OUT_EP,
        cdc_out_packet,
        sizeof(cdc_out_packet));
}

static void firestarter_cdc_bulk_in(uint8_t endpoint, uint32_t byte_count)
{
    (void)endpoint;

    if (byte_count != 0U &&
        (byte_count % FIRESTARTER_CDC_MAX_PACKET_SIZE) == 0U)
    {
        usbd_ep_start_write(FIRESTARTER_CDC_IN_EP, NULL, 0U);
        return;
    }

    tx_busy = false;
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
    (void)interface_number;
    (void)enabled;
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

    rx_head = 0U;
    rx_tail = 0U;
    tx_head = 0U;
    tx_tail = 0U;
    tx_busy = false;

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

int py32_usb_available(void)
{
    const uint16_t head = rx_head;
    const uint16_t tail = rx_tail;

    return head >= tail
        ? (int)(head - tail)
        : (int)(FIRESTARTER_USB_RX_CAPACITY - tail + head);
}

int py32_usb_peek(void)
{
    return rx_tail == rx_head ? -1 : (int)rx_buffer[rx_tail];
}

int py32_usb_read(void)
{
    if (rx_tail == rx_head)
    {
        return -1;
    }

    const uint8_t value = rx_buffer[rx_tail];
    rx_tail = ring_next(rx_tail, FIRESTARTER_USB_RX_CAPACITY);
    return value;
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
    if (!usb_initialized || !usb_device_is_configured())
    {
        return 0U;
    }

    size_t count = 0U;
    uint32_t deadline = RURP_MILLIS() + FIRESTARTER_USB_TIMEOUT_MS;

    while (count < length)
    {
        const uint16_t next = ring_next(tx_head, FIRESTARTER_USB_TX_CAPACITY);
        if (next == tx_tail)
        {
            usb_start_next_transmit_safe();
            if ((int32_t)(RURP_MILLIS() - deadline) >= 0)
            {
                break;
            }
            continue;
        }

        tx_buffer[tx_head] = buffer[count++];
        tx_head = next;
        deadline = RURP_MILLIS() + FIRESTARTER_USB_TIMEOUT_MS;
    }

    usb_start_next_transmit_safe();
    return count;
}

void py32_usb_flush(void)
{
    const uint32_t deadline = RURP_MILLIS() + FIRESTARTER_USB_TIMEOUT_MS;

    usb_start_next_transmit_safe();
    while ((tx_head != tx_tail || tx_busy) &&
           (int32_t)(RURP_MILLIS() - deadline) < 0)
    {
    }
}

void USB_IRQHandler(void)
{
    USBD_IRQHandler();
}
