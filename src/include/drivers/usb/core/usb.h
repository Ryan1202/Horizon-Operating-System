#ifndef _USB_H
#define _USB_H

#include <bits.h>
#include <drivers/bus/usb/hcd.h>
#include <drivers/usb/core/descriptors.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <stdint.h>

#define USB_REQ_HOST_TO_DEVICE 0
#define USB_REQ_DEVICE_TO_HOST 1

#define USB_REQ_TYPE_STANDARD 0
#define USB_REQ_TYPE_CLASS	  1
#define USB_REQ_TYPE_VENDOR	  2

#define USB_REQ_RECIPIENT_DEVICE	0x00
#define USB_REQ_RECIPIENT_INTERFACE 0x01
#define USB_REQ_RECIPIENT_ENDPOINT	0x02
#define USB_REQ_RECIPIENT_OTHER		0x03

#define USB_REQ_GET_STATUS		  0x00
#define USB_REQ_CLEAR_FEATURE	  0x01
#define USB_REQ_SET_FEATURE		  0x03
#define USB_REQ_SET_ADDRESS		  0x05
#define USB_REQ_GET_DESCRIPTOR	  0x06
#define USB_REQ_SET_DESCRIPTOR	  0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE	  0x0a
#define USB_REQ_SYNC_FRAME		  0x0c

#define USB_PACKET_ID_IN	0x69
#define USB_PACKET_ID_OUT	0xe1
#define USB_PACKET_ID_SETUP 0x2d
#define USB_PACKET_ID_ACK	0xd2
#define USB_PACKET_ID_NAK	0xa1
#define USB_PACKET_ID_STALL 0x54
#define USB_PACKET_ID_NYET	0x96

#define USB_CLASS_AUDIO 0x01
#define USB_CLASS_COMM	0x02
#define USB_CLASS_HID	0x03
#define USB_CLASS_MASS	0x08
#define USB_CLASS_HUB	0x09

#define USB_PORT_STAT_CONNECTION   BIT(0)
#define USB_PORT_STAT_ENABLE	   BIT(1)
#define USB_PORT_STAT_SUSPEND	   BIT(2)
#define USB_PORT_STAT_OVER_CURRENT BIT(3)
#define USB_PORT_STAT_RESET		   BIT(4)
#define USB_PORT_STAT_POWER		   BIT(8)
#define USB_PORT_STAT_LOW_SPEED	   BIT(9)
#define USB_PORT_STAT_HIGH_SPEED   BIT(10)
#define USB_PORT_STAT_TEST		   BIT(11)
#define USB_PORT_STAT_INDICATOR	   BIT(12)

#define USB_BUILD_REQUEST(                                                     \
	direction, type, recipient, request_id, value_hi, value_lo, index, length) \
	{                                                                          \
		.bmRequestType = direction << 7 | type << 5 | recipient,               \
		.bRequest	   = request_id,                                           \
		.wValue		   = HOST2LE_WORD(value_hi << 8 | value_lo),               \
		.wIndex = HOST2LE_WORD(index), .wLength = HOST2LE_WORD(length),        \
	}

typedef enum UsbInterfaceType {
	USB_INTERFACE_TYPE_DEVICE = 0,
	USB_INTERFACE_TYPE_AUDIO,
	USB_INTERFACE_TYPE_COMM,
	USB_INTERFACE_TYPE_HID,
	USB_INTERFACE_TYPE_PHYSICAL,
	USB_INTERFACE_TYPE_IMAGE,
	USB_INTERFACE_TYPE_PRINTER,
	USB_INTERFACE_TYPE_MASS_STORAGE,
	USB_INTERFACE_TYPE_HUB,
	USB_INTERFACE_TYPE_CDC_DATA,
	USB_INTERFACE_TYPE_SMART_CARD,
	USB_INTERFACE_TYPE_CONTENT_SECURITY,
	USB_INTERFACE_TYPE_VIDEO,
	USB_INTERFACE_TYPE_PERSONAL_HEALTHCARE,
	USB_INTERFACE_TYPE_AUDIO_VIDEO,
	USB_INTERFACE_TYPE_BILLBOARD,
	USB_INTERFACE_TYPE_TYPE_C_BRIDGE,
	USB_INTERFACE_TYPE_BULK_DISPLAY_PROTOCOL,
	USB_INTERFACE_TYPE_MTCP,
	USB_INTERFACE_TYPE_I3C,
	USB_INTERFACE_TYPE_DIAGNOSTIC,
	USB_INTERFACE_TYPE_WIRELESS_CONTROLLER,
	USB_INTERFACE_TYPE_MISCELLANEOUS,
	USB_INTERFACE_TYPE_APPLICATION_SPECIFIC,
	USB_INTERFACE_TYPE_VENDOR_SPECIFIC,
	USB_INTERFACE_TYPE_MAX,
} UsbInterfaceType;

typedef enum UsbDeviceSpeed {
	USB_SPEED_LOW,
	USB_SPEED_FULL,
	USB_SPEED_HIGH,
} UsbDeviceSpeed;

typedef enum {
	USB_STATE_UNINITED, // 未被枚举
	USB_STATE_INITED,	// 已被枚举
	USB_STATE_ACTIVE,	// 正常工作
} UsbDeviceState;

typedef enum {
	USB_EP_CONTROL = 0,
	USB_EP_ISOCHRONOUS,
	USB_EP_BULK,
	USB_EP_INTERRUPT,
} UsbEpTransferType;

typedef enum {
	USB_EP_OUT,
	USB_EP_IN,
} UsbEpDirection;

typedef struct UsbEndpoint {
	list_t list;

	struct UsbEndpointDescriptor *desc;

	void *pipe;

	uint8_t data_toggle;
} UsbEndpoint;

typedef struct UsbInterface {
	list_t list;

	struct UsbDriver *usb_driver;

	struct UsbInterfaceDescriptor *desc;

	UsbEndpoint *endpoints[0];
} UsbInterface;

typedef enum UsbStatus {
	USB_STATUS_ACK,
	USB_STATUS_NAK,
	USB_STATUS_STALL,
	USB_STATUS_NYET,
	USB_STATUS_ERR,
} UsbStatus;

typedef enum UsbSetupStatus {
	USB_SETUP_SUCCESS,
	USB_SETUP_NAK_RECV,
	USB_SETUP_STALLED,
	USB_SETUP_CRC_TIMEOUT_ERR,
	USB_SETUP_BITSTUFF_ERR,
	USB_SETUP_DATABUFFER_ERR,
} UsbSetupStatus;

typedef struct UsbControlRequest {
	uint8_t	 bmRequestType;
	uint8_t	 bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
} __attribute__((packed)) UsbControlRequest;

static const uint8_t usb_interface_map[] = {
	[USB_INTERFACE_TYPE_DEVICE]				   = 0x00,
	[USB_INTERFACE_TYPE_AUDIO]				   = 0x01,
	[USB_INTERFACE_TYPE_COMM]				   = 0x02,
	[USB_INTERFACE_TYPE_HID]				   = 0x03,
	[USB_INTERFACE_TYPE_PHYSICAL]			   = 0x04,
	[USB_INTERFACE_TYPE_IMAGE]				   = 0x05,
	[USB_INTERFACE_TYPE_PRINTER]			   = 0x06,
	[USB_INTERFACE_TYPE_MASS_STORAGE]		   = 0x07,
	[USB_INTERFACE_TYPE_HUB]				   = 0x08,
	[USB_INTERFACE_TYPE_CDC_DATA]			   = 0x09,
	[USB_INTERFACE_TYPE_SMART_CARD]			   = 0x0a,
	[USB_INTERFACE_TYPE_CONTENT_SECURITY]	   = 0x0b,
	[USB_INTERFACE_TYPE_VIDEO]				   = 0x0c,
	[USB_INTERFACE_TYPE_PERSONAL_HEALTHCARE]   = 0x0d,
	[USB_INTERFACE_TYPE_AUDIO_VIDEO]		   = 0x0e,
	[USB_INTERFACE_TYPE_BILLBOARD]			   = 0x0f,
	[USB_INTERFACE_TYPE_TYPE_C_BRIDGE]		   = 0x10,
	[USB_INTERFACE_TYPE_BULK_DISPLAY_PROTOCOL] = 0x11,
	[USB_INTERFACE_TYPE_MTCP]				   = 0x12,
	[USB_INTERFACE_TYPE_I3C]				   = 0x13,
	[USB_INTERFACE_TYPE_DIAGNOSTIC]			   = 0x14,
	[USB_INTERFACE_TYPE_WIRELESS_CONTROLLER]   = 0x3c,
	[USB_INTERFACE_TYPE_MISCELLANEOUS]		   = 0xef,
	[USB_INTERFACE_TYPE_APPLICATION_SPECIFIC]  = 0xfe,
	[USB_INTERFACE_TYPE_VENDOR_SPECIFIC]	   = 0xff,
};

extern UsbEndpoint usb_ep0;

struct UsbHub;
struct UsbDevice *usb_create_device(
	UsbHcd *hcd, struct UsbHub *hub, UsbDeviceSpeed speed, uint8_t address);
int usb_destroy_device(struct UsbDevice *device);

UsbControlRequest *usb_create_request(
	uint8_t direction, uint8_t type, uint8_t recipient, uint8_t request_id,
	uint8_t value_hi, uint8_t value_lo, uint16_t index, uint16_t length);
void usb_init_endpoint(
	struct UsbDevice *usb_device, UsbEndpoint *ep,
	struct UsbEndpointDescriptor *desc);

int usb_probe_device(UsbHcd *hcd, struct UsbHub *hub, UsbDeviceSpeed speed);

#endif