#include "usb_cdc.h"
#include "usb_config.h"
#include "fsusb.h"
#include <string.h>

#define USB_CDC_RX_RING_SIZE 256
#define USB_CDC_TX_RING_SIZE 256

static volatile uint8_t usb_cdc_dtr;

static uint8_t usb_cdc_line_coding[7] = {
	0x00, 0xC2, 0x01, 0x00,
	0x00,
	0x00,
	0x08
};

static volatile uint8_t usb_cdc_rx_ring[USB_CDC_RX_RING_SIZE];
static volatile uint16_t usb_cdc_rx_head = 0;
static volatile uint16_t usb_cdc_rx_tail = 0;

static volatile uint8_t usb_cdc_tx_ring[USB_CDC_TX_RING_SIZE];
static volatile uint16_t usb_cdc_tx_head = 0;
static volatile uint16_t usb_cdc_tx_tail = 0;

static int usb_cdc_tx_pending(void);

void init_pins_usb_cdc(void)
{
	USBFSSetup();
}

void task_usb_cdc(void)
{
	if (usb_cdc_tx_pending() > 0) {
		USBFS_SendEndpoint(3, 0);
	}
}

int usb_cdc_debug_is_active(void)
{
	return usb_cdc_dtr;
}

/*-----------------------------*/
/* USB FS-TTY using fsusb.c/.h */
/*-----------------------------*/

// Stash whatever the host tells us via SET_LINE_CODING so GET_LINE_CODING
// can echo it back. CDC over USB doesn't need a "real" UART underneath.
// This is bookkeeping to keep host-side tools happy.

void HandleDataOut(struct _USBState *ctx, int endp, uint8_t *data, int len)
{
	if (endp == 0) {
		ctx->USBFS_SetupReqLen = 0;
		if (ctx->USBFS_SetupReqCode == CDC_SET_LINE_CODING) {
			memcpy(usb_cdc_line_coding, CTRL0BUFF, sizeof(usb_cdc_line_coding));
		}
		return;
	}

	if (endp != 2) {
		return;
	}

	for (int i = 0; i < len; i++) {
		uint16_t next = (usb_cdc_rx_head + 1) & (USB_CDC_RX_RING_SIZE - 1);
		if (next == usb_cdc_rx_tail) {
			break;
		}
		usb_cdc_rx_ring[usb_cdc_rx_head] = data[i];
		usb_cdc_rx_head = next;
	}
}

// TX: keep a small ring buffer so the device can accept host-driven writes
// without blocking, and drain it when the host asks for the next IN packet.
int usb_cdc_tx_send(const uint8_t *buf, int len)
{
	int queued = 0;

	for (int i = 0; i < len; i++) {
		uint16_t next = (usb_cdc_tx_head + 1) & (USB_CDC_TX_RING_SIZE - 1);
		if (next == usb_cdc_tx_tail) {
			break;
		}
		usb_cdc_tx_ring[usb_cdc_tx_head] = buf[i];
		usb_cdc_tx_head = next;
		queued++;
	}

	return queued;
}

static int usb_cdc_tx_pending(void)
{
	return (usb_cdc_tx_head - usb_cdc_tx_tail) & (USB_CDC_TX_RING_SIZE - 1);
}

int HandleInRequest(struct _USBState *ctx, int endp, uint8_t *data, int len)
{
	if (endp != 3) {
		return 0;
	}

	int count = 0;
	while (count < USBFS_PACKET_SIZE && usb_cdc_tx_tail != usb_cdc_tx_head) {
		data[count++] = usb_cdc_tx_ring[usb_cdc_tx_tail];
		usb_cdc_tx_tail = (usb_cdc_tx_tail + 1) & (USB_CDC_TX_RING_SIZE - 1);
	}
	return count;
}

// CDC_SET_LINE_CODING's 7-byte payload arrives via the EP0 data stage, inside
// HandleDataOut. HandleSetupCustom only sees the setup packet itself and decides
// how to respond.
int HandleSetupCustom(struct _USBState *ctx, int setup_code)
{
	int ret = -1;

	if (ctx->USBFS_SetupReqType & USB_REQ_TYP_CLASS) {
		switch (setup_code) {
		case CDC_SET_LINE_CODING:
			ret = (ctx->USBFS_SetupReqLen) ? ctx->USBFS_SetupReqLen : -1;
			break;

		case CDC_GET_LINE_CODING:
			ctx->pCtrlPayloadPtr = usb_cdc_line_coding;
			ret = ctx->USBFS_SetupReqLen;
			break;

		case CDC_SET_LINE_CTLSTE:
			usb_cdc_dtr = (ctx->USBFS_IndexValue & 1) != 0;
			ret = (ctx->USBFS_SetupReqLen) ? ctx->USBFS_SetupReqLen : -1;
			break;

		case CDC_SEND_BREAK:
			ret = (ctx->USBFS_SetupReqLen) ? ctx->USBFS_SetupReqLen : -1;
			break;

		default:
			ret = 0;
			break;
		}
	} else {
		ret = 0;
	}

	return ret;
}
 
