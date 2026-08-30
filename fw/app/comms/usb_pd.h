#ifndef USB_PD_H
#define USB_PD_H

void init_pins_usb_pd(void);

void task_usb_pd(void);

int usb_pd_negotiating(void);

#endif // USB_PD_H