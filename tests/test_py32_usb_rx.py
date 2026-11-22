"""A full ARM data block must survive queued USB packets, including wraparound."""
from ._host_build import REPO_ROOT, compile_ok, host_cc, run_binary
from ._py32_build import arm_defines


def test_complete_encoded_block_survives_usb_rx(tmp_path):
    defines = arm_defines('platform/py32f071/src/usb_cdc.c')
    (tmp_path / 'boards').mkdir()
    (tmp_path / 'boards/py32f071_rurp_shield.h').write_text('''
#pragma once
#define __get_PRIMASK() 0
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
#define __HAL_RCC_SYSCFG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USB_CLK_ENABLE() ((void)0)
#define NVIC_EnableIRQ(x) ((void)0)
''')
    (tmp_path / 'rurp_platform.h').write_text('#define RURP_MILLIS() 0U\n')
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
static inline bool usb_device_is_configured(void) { return true; }
static inline void usbd_ep_start_read(uint8_t ep, uint8_t *buf, size_t n) {}
static inline void usbd_ep_start_write(uint8_t ep, const uint8_t *buf, size_t n) {}
static inline void usbd_desc_register(const uint8_t *desc) {}
static inline struct usbd_interface *usbd_cdc_acm_init_intf(struct usbd_interface *p) { return p; }
static inline void usbd_add_interface(struct usbd_interface *p) {}
static inline void usbd_add_endpoint(struct usbd_endpoint *p) {}
static inline void usbd_initialize(void) {}
static inline void USBD_IRQHandler(void) {}
''')
    driver = tmp_path / 'rx.c'
    driver.write_text('''
#include <assert.h>
#include "platform/py32f071/src/usb_cdc.c"
int main(void) {
    rurp_communication_begin();
    /* Worst-case nonzero payload + CRC, code bytes, '#' and delimiter. */
    const unsigned length = DATA_BUFFER_SIZE + 1 + (DATA_BUFFER_SIZE + 1) / 254 + 3;
    for (unsigned pass = 0; pass < 4; ++pass) {
        for (unsigned offset = 0; offset < length;) {
            unsigned n = length - offset;
            if (n > 64) n = 64;
            for (unsigned i = 0; i < n; ++i) cdc_out_packet[i] = (uint8_t)(offset + i);
            firestarter_cdc_bulk_out(FIRESTARTER_CDC_OUT_EP, n);
            offset += n;
        }
        assert(py32_usb_available() == (int)length);
        for (unsigned i = 0; i < length; ++i) {
            assert(py32_usb_peek() == (uint8_t)i);
            assert(py32_usb_read() == (uint8_t)i);
        }
        assert(py32_usb_available() == 0 && py32_usb_read() == -1);
    }
}
''')
    binary = tmp_path / 'rx'
    compile_ok([host_cc(), '-std=c11', *defines, '-UNDEBUG', '-I', tmp_path,
                '-I', REPO_ROOT, driver, '-o', binary])
    result = run_binary(binary)
    assert result.returncode == 0, result.stderr
