#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

/**
 * Initialize the USB CDC pins and underlying USB stack.
 */
void init_pins_usb_cdc(void);

/**
 * Service the USB CDC task and flush any queued transmit data when the host
 * is ready to receive the next bulk packet.
 */
void task_usb_cdc(void);

/**
 * Return whether the USB CDC data terminal ready (DTR) signal is asserted.
 *
 * Returns 1 when DTR is active, otherwise 0.
 */
int usb_cdc_debug_is_active(void);

/**
 * Queue bytes for transmission over the USB CDC bulk endpoint.
 *
 * buf: bytes to send.
 * len: number of bytes to queue.
 * Returns the number of bytes accepted into the transmit ring.
 */
int usb_cdc_tx_send(const uint8_t *buf, int len);

#endif /* USB_CDC_H */