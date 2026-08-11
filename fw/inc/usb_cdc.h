#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize USB CDC (virtual COM) interface. */
void usb_cdc_init(void);

/** Send data over CDC. Returns number of bytes sent. */
int usb_cdc_send(const uint8_t *buf, unsigned len);

#ifdef __cplusplus
}
#endif

#endif /* USB_CDC_H */