#ifndef USB_PD_H
#define USB_PD_H

void usb_pd_init(void);

void usb_pd_task(void);

int usb_pd_negotiating(void);
uint8_t usb_pd_get_available_power_w(void);

#endif // USB_PD_H