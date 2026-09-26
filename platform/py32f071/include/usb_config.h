#ifndef FIRESTARTER_PY32F071_USB_CONFIG_H
#define FIRESTARTER_PY32F071_USB_CONFIG_H

#include <stdlib.h>

#include "py32f0xx_hal.h"

#define CONFIG_USB_PRINTF(...)
#define usb_malloc(size) malloc(size)
#define usb_free(pointer) free(pointer)

#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_ERROR
#endif

#ifndef CONFIG_USB_ALIGN_SIZE
#define CONFIG_USB_ALIGN_SIZE 4
#endif

#define USB_NOCACHE_RAM_SECTION
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 256

#define USBD_IRQn USB_IRQn
#define USBD_IRQHandler USBD_IRQHandler

#ifdef __cplusplus
extern "C" {
#endif
void USBD_IRQHandler(void);
#ifdef __cplusplus
}
#endif

#endif
