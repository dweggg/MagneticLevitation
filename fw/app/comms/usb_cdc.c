#include "usb_cdc.h"
#include "usb_config.h"
#include "fsusb.h"
#include <string.h>

static volatile uint8_t usb_cdc_dtr;

void init_pins_usb_cdc(void){
	USBFSSetup();
}

void task_usb_cdc(void){
	if (USB_TX_Pending() > 0) {
        USBFS_SendEndpoint(3, 0);
    }

}

int usb_cdc_debug_is_active(void){
	return usb_cdc_dtr;
}


/*-----------------------------*/
/* USB FS-TTY using fsusb.c/.h */
/*-----------------------------*/

// Stash whatever the host tells us via SET_LINE_CODING so GET_LINE_CODING
// can echo it back. CDC over USB doesn't need a "real" UART underneath 
// this is just bookkeeping to keep host-side tools happy.
static uint8_t line_coding[7] = {
	0x00, 0xC2, 0x01, 0x00, // 115200 baud, little-endian
	0x00,                   // stop bits: 1
	0x00,                   // parity: none
	0x08                    // data bits: 8
};
 
// ---------------------------------------------------------------------
// RX: bytes arriving from the host land in HandleDataOut(). We're in IRQ
// context there, so just copy into a ring buffer and get out do any
// real processing in your main loop.
// ---------------------------------------------------------------------
 
#define RX_RING_SIZE 256 // power of 2
 
static volatile uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head = 0; // written by ISR
static volatile uint16_t rx_tail = 0; // read by main loop
 
void HandleDataOut( struct _USBState *ctx, int endp, uint8_t *data, int len )
{
	if( endp == 0 )
	{
		// EP0 data stage this is where SET_LINE_CODING's 7-byte payload
		// actually lands (CTRL0BUFF), not inside HandleSetupCustom itself.
		ctx->USBFS_SetupReqLen = 0; // ACK
		if( ctx->USBFS_SetupReqCode == CDC_SET_LINE_CODING )
		{
			memcpy( line_coding, CTRL0BUFF, sizeof(line_coding) );
		}
		return;
	}
 
	if( endp != 2 ) return; // only care about the bulk OUT endpoint
 
	for( int i = 0; i < len; i++ )
	{
		uint16_t next = (rx_head + 1) & (RX_RING_SIZE - 1);
		if( next == rx_tail )
			break; // ring full drop remaining bytes rather than corrupt state
		rx_ring[rx_head] = data[i];
		rx_head = next;
	}
}
 
// Call from main loop. Returns 1 and fills *out_byte if a byte was available.
int USB_RX_ReadByte( uint8_t *out_byte )
{
	if( rx_tail == rx_head )
		return 0; // empty
 
	*out_byte = rx_ring[rx_tail];
	rx_tail = (rx_tail + 1) & (RX_RING_SIZE - 1);
	return 1;
}
 
int USB_RX_Available( void )
{
	return (rx_head - rx_tail) & (RX_RING_SIZE - 1);
}
 
// ---------------------------------------------------------------------
// TX: two supported patterns.
//
// 1) Push model (recommended for streaming) call USB_TX_Send() from
//    your main loop whenever you have data ready; it copies into a TX
//    ring and HandleInRequest() drains it whenever the host asks for
//    the next IN packet.
// 2) If you'd rather feed data lazily instead of buffering it yourself,
//    write your own producer directly inside HandleInRequest().
// ---------------------------------------------------------------------
 
#define TX_RING_SIZE 256 // power of 2
 
static volatile uint8_t  tx_ring[TX_RING_SIZE];
static volatile uint16_t tx_head = 0; // written by main loop (producer)
static volatile uint16_t tx_tail = 0; // read by ISR (consumer)
 
// Queue bytes for transmission. Returns number of bytes actually queued
// (less than len if the ring is full caller should retry/backpressure).
int USB_TX_Send( const uint8_t *buf, int len )
{
	int queued = 0;
	for( int i = 0; i < len; i++ )
	{
		uint16_t next = (tx_head + 1) & (TX_RING_SIZE - 1);
		if( next == tx_tail )
			break; // full, stop. caller can retry the rest later
		tx_ring[tx_head] = buf[i];
		tx_head = next;
		queued++;
	}
	return queued;
}
 
int USB_TX_Pending( void )
{
	return (tx_head - tx_tail) & (TX_RING_SIZE - 1);
}
 
// Called by fsusb's ISR whenever the host issues an IN token on `endp` and
// wants data. NOTE: the `len` argument fsusb passes here is always 0 it
// is NOT the buffer capacity. `data` points directly at the endpoint's
// fixed USBFS_PACKET_SIZE (64-byte) hardware buffer, so that's the real
// capacity to respect. Fill it and return the number of bytes written;
// return 0 if you have nothing to send (fsusb will NAK and the host will
// just retry the IN token later).
int HandleInRequest( struct _USBState *ctx, int endp, uint8_t *data, int len )
{
	if( endp != 3 ) return 0; // only the bulk IN endpoint carries our stream
 
	int n = 0;
	while( n < USBFS_PACKET_SIZE && tx_tail != tx_head )
	{
		data[n++] = tx_ring[tx_tail];
		tx_tail = (tx_tail + 1) & (TX_RING_SIZE - 1);
	}
	return n;
}
 
// ---------------------------------------------------------------------
// EP0 class requests. Required even though we ignore line coding
// without ACKing these, pyserial / most terminal apps will hang on open()
// waiting for SET_LINE_CODING / SET_CONTROL_LINE_STATE to complete.
// ---------------------------------------------------------------------
 
// CDC_SET_LINE_CODING's 7-byte payload arrives via the EP0 data stage, i.e.
// inside HandleDataOut(ctx, 0, ...) above not here. HandleSetupCustom only
// sees the setup packet itself and decides how to respond.
int HandleSetupCustom( struct _USBState *ctx, int setup_code )
{
	int ret = -1;
 
	if( ctx->USBFS_SetupReqType & USB_REQ_TYP_CLASS )
	{
		switch( setup_code )
		{
			case CDC_SET_LINE_CODING:
				// Just ACK; the actual bytes are captured in HandleDataOut.
				ret = ( ctx->USBFS_SetupReqLen ) ? ctx->USBFS_SetupReqLen : -1;
				break;
 
			case CDC_GET_LINE_CODING:
				// Point the control-transfer payload pointer at our stored
				// line coding; fsusb sends it back to the host from there.
				ctx->pCtrlPayloadPtr = line_coding;
				ret = ctx->USBFS_SetupReqLen;
				break;
 
			case CDC_SET_LINE_CTLSTE:
				usb_cdc_dtr = (ctx->USBFS_IndexValue & 1) != 0;
				ret = ( ctx->USBFS_SetupReqLen ) ? ctx->USBFS_SetupReqLen : -1;
				break;
 
			case CDC_SEND_BREAK:
				ret = ( ctx->USBFS_SetupReqLen ) ? ctx->USBFS_SetupReqLen : -1;
				break;
 
			default:
				ret = 0;
				break;
		}
	}
	else
	{
		ret = 0; // not a class request we handle -> STALL
	}
 
	return ret;
}
 
