#ifndef USB_CDC_H
#define USB_CDC_H

void init_pins_usb_cdc(void);

void task_usb_cdc(void);

int usb_cdc_debug_is_active(void);

#endif // USB_CDC_H