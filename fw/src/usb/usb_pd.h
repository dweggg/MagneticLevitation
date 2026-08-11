#ifndef USB_PD_H
#define USB_PD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize USB Power Delivery stack. */
void usb_pd_init(void);

/** Handle PD state machine; must be called periodically. */
void usb_pd_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_PD_H */
