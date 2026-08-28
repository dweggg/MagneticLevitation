#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

void init_pins_usb_cdc(void);

void task_usb_cdc(void);

int usb_cdc_debug_is_active(void);

int USB_TX_Pending(void);
int USB_TX_Send(const uint8_t *buf, int len);

#endif // USB_CDC_H