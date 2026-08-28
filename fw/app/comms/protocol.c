#include "protocol.h"
#include "usb_cdc.h"

int protocol_log_write(const uint8_t *buf, uint16_t len)
{
	return USB_TX_Send(buf, len);
}