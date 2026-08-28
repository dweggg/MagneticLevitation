#ifndef _USB_CONFIG_H
#define _USB_CONFIG_H

#include "funconfig.h"
#include "ch32fun.h"

#define FUSB_BUFFERS_NUMBER   4 // Number of EP buffers (one for EP0, one per each IN/OUT, two for double)
#define FUSB_EP1_MODE         USBFS_EP_MODE_TX // IN  (CDC notify, mostly unused)
#define FUSB_EP2_MODE         USBFS_EP_MODE_RX // OUT (bulk, host -> device)
#define FUSB_EP3_MODE         USBFS_EP_MODE_TX // IN  (bulk, device -> host)
#define FUSB_SUPPORTS_SLEEP   0
#define FUSB_HID_INTERFACES   0
#define FUSB_CURSED_TURBO_DMA 0 // Hacky, but seems fine, shaves 2.5us off filling 64-byte buffers.
#define FUSB_HID_USER_REPORTS 0
#define FUSB_IO_PROFILE       0
#define FUSB_USE_HPE          FUNCONF_ENABLE_HPE
#define FUSB_USER_HANDLERS    1 // We implement HandleInRequest / HandleDataOut / HandleSetupCustom ourselves
#define FUSB_USE_DMA7_COPY    0
#define FUSB_VDD_5V           FUNCONF_USE_5V_VDD

#include "usb_defines.h"

// TODO: swap in VID/PID, 0x1209/0xd035 is the
// shared ch32fun example PID (from pid.codes)
#define FUSB_USB_VID 0x1209
#define FUSB_USB_PID 0xd035
#define FUSB_USB_REV 0x0007
#define FUSB_STR_MANUFACTURER u"ch32fun"
#define FUSB_STR_PRODUCT      u"USB TTY"
#define FUSB_STR_SERIAL       u"007"

// Taken from http://www.usbmadesimple.co.uk/ums_ms_desc_dev.htm
static const uint8_t device_descriptor[] = {
	18, //bLength - Length of this descriptor
	1,  //bDescriptorType - Type (Device)
	0x10, 0x01, //bcdUSB - The highest USB spec version this device supports (USB1.1)
	0x02, //bDeviceClass - Device Class
	0x0, //bDeviceSubClass - Device Subclass
	0x0, //bDeviceProtocol  (000 = use config descriptor)
	64, //bMaxPacketSize - Max packet size for EP0
	(uint8_t)(FUSB_USB_VID), (uint8_t)(FUSB_USB_VID >> 8), //idVendor
	(uint8_t)(FUSB_USB_PID), (uint8_t)(FUSB_USB_PID >> 8), //idProduct
	(uint8_t)(FUSB_USB_REV), (uint8_t)(FUSB_USB_REV >> 8), //bcdDevice
	1, //iManufacturer - Index of Manufacturer string
	2, //iProduct - Index of Product string
	3, //iSerialNumber - Index of Serial string
	1, //bNumConfigurations
};

/* Configuration Descriptor Set */
static const uint8_t config_descriptor[] =
{
  0x09,        // bLength
  0x02,        // bDescriptorType (Configuration)
  0x43, 0x00,  // wTotalLength = 67
  0x02,        // bNumInterfaces = 2
  0x01,        // bConfigurationValue
  0x00,        // iConfiguration (String Index)
  0x80,        // bmAttributes
  0x32,        // bMaxPower = 100mA

  // Interface 0 — CDC Communications (Control) interface, EP1
  0x09,        // bLength
  0x04,        // bDescriptorType - Interface
  0x00,        // bInterfaceNumber - 0
  0x00,        // bAlternateSetting
  0x01,        // bNumEndpoints - 1
  0x02,        // bInterfaceClass - CDC
  0x02,        // bInterfaceSubClass - Abstract Control Model
  0x01,        // bInterfaceProtocol - AT Commands: V.250 etc
  0x00,        // iInterface

  // CDC Header Functional Descriptor
  0x05, 0x24, 0x00, 0x10, 0x01,
  // Call Management Functional Descriptor
  0x05, 0x24, 0x01, 0x00, 0x01,
  // Abstract Control Management Functional Descriptor
  0x04, 0x24, 0x02, 0x02,
  // Union Functional Descriptor (control iface 0, data iface 1)
  0x05, 0x24, 0x06, 0x00, 0x01,

  // EP1 — CDC notification endpoint (interrupt IN)
  0x07,        // bLength
  0x05,        // bDescriptorType (Endpoint)
  0x81,        // bEndpointAddress (EP1 IN)
  0x03,        // bmAttributes (Interrupt)
  0x40, 0x00,  // wMaxPacketSize = 64
  0x01,        // bInterval

  // Interface 1 — CDC Data interface, EP2/EP3
  0x09,        // bLength
  0x04,        // bDescriptorType (Interface)
  0x01,        // bInterfaceNumber - 1
  0x00,        // bAlternateSetting
  0x02,        // bNumEndpoints - 2
  0x0A,        // bInterfaceClass - CDC Data
  0x00,        // bInterfaceSubClass
  0x00,        // bInterfaceProtocol - Transparent
  0x00,        // iInterface

  // EP2 — bulk OUT (host -> device, your RX)
  0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
  // EP3 — bulk IN (device -> host, your TX)
  0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00,

  // 67 bytes total
};

struct usb_string_descriptor_struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wString[];
};

const static struct usb_string_descriptor_struct language __attribute__((section(".rodata"))) = {
	4,
	3,
	{0x0409}  // Language ID - English (US)
};
const static struct usb_string_descriptor_struct string1 __attribute__((section(".rodata"))) = {
	sizeof(FUSB_STR_MANUFACTURER),
	3,
	FUSB_STR_MANUFACTURER
};
const static struct usb_string_descriptor_struct string2 __attribute__((section(".rodata"))) = {
	sizeof(FUSB_STR_PRODUCT),
	3,
	FUSB_STR_PRODUCT
};
const static struct usb_string_descriptor_struct string3 __attribute__((section(".rodata"))) = {
	sizeof(FUSB_STR_SERIAL),
	3,
	FUSB_STR_SERIAL
};

// This table defines which descriptor data is sent for each specific
// request from the host (in wValue and wIndex).
const static struct descriptor_list_struct {
	uint32_t	lIndexValue;
	const uint8_t	*addr;
	uint8_t		length;
} descriptor_list[] = {
	{0x00000100, device_descriptor, sizeof(device_descriptor)},
	{0x00000200, config_descriptor, sizeof(config_descriptor)},

	{0x00000300, (const uint8_t *)&language, 4},
	{0x04090301, (const uint8_t *)&string1, string1.bLength},
	{0x04090302, (const uint8_t *)&string2, string2.bLength},
	{0x04090303, (const uint8_t *)&string3, string3.bLength}
};
#define DESCRIPTOR_LIST_ENTRIES ((sizeof(descriptor_list))/(sizeof(struct descriptor_list_struct)))

#endif
