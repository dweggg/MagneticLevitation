#ifndef USB_PD_H
#define USB_PD_H

void init_pins_usb_pd(void);

void task_usb_pd(void);

int usb_pd_negotiating(void);
uint8_t usb_pd_get_available_power_w(void);

#endif // USB_PD_H