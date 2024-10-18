#ifndef _USB_H
#define _USB_H

#include <drivers/usb/hcd.h>
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

#define USB_DESC_TYPE_DEVICE			 0x01
#define USB_DESC_TYPE_CONFIG			 0x02
#define USB_DESC_TYPE_STRING			 0x03
#define USB_DESC_TYPE_INTERFACE			 0x04
#define USB_DESC_TYPE_ENDPOINT			 0x05
#define USB_DESC_TYPE_DEVICE_QUALIFIER	 0x06
#define USB_DESC_TYPE_OTHER_SPEED_CONFIG 0x07
#define USB_DESC_TYPE_INTERFACE_POWER	 0x08

#define USB_DESC_TYPE_DEVICE_SIZE 18
#define USB_DESC_TYPE_CONFIG_SIZE 9
#define USB_DESC_TYPE_HUB_SIZE	  9

#define USB_DESC_TYPE_HUB 0x29

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

struct usb_device_descriptor {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint16_t bcdUSB;
	uint8_t	 bDeviceClass;
	uint8_t	 bDeviceSubClass;
	uint8_t	 bDeviceProtocol;
	uint8_t	 bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t	 iManufacturer;
	uint8_t	 iProduct;
	uint8_t	 iSerialNumber;
	uint8_t	 bNumConfigurations;
} __attribute__((packed));

struct usb_config_descriptor {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint16_t wTotalLength;
	uint8_t	 bNumInterfaces;
	uint8_t	 bConfigurationValue;
	uint8_t	 iConfiguration;
	uint8_t	 bmAttributes;
	uint8_t	 bMaxPower;
} __attribute__((packed));

struct usb_interface_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint8_t	 bEndpointAddress;
	uint8_t	 bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t	 bInterval;
} __attribute__((packed));

struct usb_string_descriptor {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint16_t wData[];
} __attribute__((packed));

struct usb_hub_descriptor {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint8_t	 bNbrPorts;
	uint16_t wHubCharacteristics;
	uint8_t	 bPwrOn2PwrGood;
	uint8_t	 bHubContrCurrent;
	uint8_t	 DeviceRemovable;
	uint8_t	 PortPwrCtrlMask;
} __attribute__((packed));

typedef enum usb_device_speed {
	USB_SPEED_LOW,
	USB_SPEED_FULL,
	USB_SPEED_HIGH,
} usb_device_speed_t;

typedef struct usb_device {
	list_t	list;
	uint8_t address;

	list_t ep_lh;
	list_t interface_lh;

	struct usb_device_descriptor *desc;
	usb_device_speed_t			  speed;
} usb_device_t;

typedef struct usb_endpoint {
	list_t list;

	uint8_t endpoint;
	enum ep_transfer_type {
		USB_EP_CONTROL,
		USB_EP_ISOCHRONOUS,
		USB_EP_BULK,
		USB_EP_INTERRUPT,
	} transfer_type;
	enum ep_direction {
		USB_EP_OUT,
		USB_EP_IN,
	} direction;
	uint16_t max_packet_size;
} usb_endpoint_t;

typedef struct usb_interface {
	list_t list;

	uint8_t interface;
	uint8_t class;
	uint8_t subclass;
	uint8_t protocol;
} usb_interface_t;

typedef struct usb_transfer {
	usb_device_t   *device;
	usb_endpoint_t *ep;
} usb_transfer_t;

typedef enum usb_status {
	USB_STATUS_ACK,
	USB_STATUS_NAK,
	USB_STATUS_STALL,
	USB_STATUS_NYET,
	USB_STATUS_ERR,
} usb_status_t;

typedef enum usb_setup_status {
	USB_SETUP_SUCCESS,
	USB_SETUP_NAK_RECV,
	USB_SETUP_STALLED,
	USB_SETUP_CRC_TIMEOUT_ERR,
	USB_SETUP_BITSTUFF_ERR,
	USB_SETUP_DATABUFFER_ERR,
} usb_setup_status_t;

typedef struct usb_request {
	uint8_t	 bmRequestType;
	uint8_t	 bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
} usb_request_t;

extern usb_endpoint_t usb_default_ep;

usb_device_t *usb_create_device(usb_device_speed_t speed, uint8_t address);
int			  usb_destroy_device(usb_device_t *device);

usb_request_t *usb_create_request(
	uint8_t direction, uint8_t type, uint8_t recipient, uint8_t request_id,
	uint8_t value_hi, uint8_t value_lo, uint16_t index, uint16_t length);
usb_endpoint_t *usb_create_endpoint(
	uint8_t endpoint, enum ep_transfer_type transfer_type,
	enum ep_direction direction, uint16_t max_packet_size);

usb_transfer_t *usb_create_transfer(
	usb_device_t *device, usb_endpoint_t *endpoint);

int usb_init_device(usb_hcd_t *hcd, usb_device_t *device);

usb_setup_status_t usb_control_transaction_in(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, void *buffer,
	usb_request_t *usb_req, uint32_t length);
usb_setup_status_t usb_control_transaction_out(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, void *buffer,
	usb_request_t *usb_req, uint32_t length);

#endif