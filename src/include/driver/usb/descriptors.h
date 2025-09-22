#ifndef _USB_DESCRIPTORS_H
#define _USB_DESCRIPTORS_H

#include <stdint.h>

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

#define USB_DESCRIPTOR_TYPE_STANDARD 0
#define USB_DESCRIPTOR_TYPE_CLASS	 1
#define USB_DESCRIPTOR_TYPE_VENDOR	 2

#define USB_DESC_TYPE_HUB 0x29

struct UsbDeviceDescriptor {
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

struct UsbConfigDescriptor {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint16_t wTotalLength;
	uint8_t	 bNumInterfaces;
	uint8_t	 bConfigurationValue;
	uint8_t	 iConfiguration;
	uint8_t	 bmAttributes;
	uint8_t	 bMaxPower;
} __attribute__((packed));

struct UsbInterfaceDescriptor {
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

struct UsbEndpointDescriptor {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint8_t	 bEndpointAddress;
	uint8_t	 bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t	 bInterval;
} __attribute__((packed));

struct UsbStringDescriptor {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint16_t wData[];
} __attribute__((packed));

struct UsbHubDescriptor {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint8_t	 bNbrPorts;
	uint16_t wHubCharacteristics;
	uint8_t	 bPwrOn2PwrGood;
	uint8_t	 bHubContrCurrent;
	uint8_t	 DeviceRemovable;
	uint8_t	 PortPwrCtrlMask;
} __attribute__((packed));

// USB HID Descriptors

typedef enum {
	USB_HID_LAYOUT_NOT_SUPPORTED	  = 0x00,
	USB_HID_LAYOUT_ARABIC			  = 0x01,
	USB_HID_LAYOUT_BELGIAN			  = 0x02,
	USB_HID_LAYOUT_CANADIAN_BILINGUAL = 0x03,
	USB_HID_LAYOUT_CANADIAN_FRENCH	  = 0x04,
	USB_HID_LAYOUT_CZECH			  = 0x05,
	USB_HID_LAYOUT_DANISH			  = 0x06,
	USB_HID_LAYOUT_FINNISH			  = 0x07,
	USB_HID_LAYOUT_FRENCH			  = 0x08,
	USB_HID_LAYOUT_GERMAN			  = 0x09,
	USB_HID_LAYOUT_GREEK			  = 0x0A,
	USB_HID_LAYOUT_HEBREW			  = 0x0B,
	USB_HID_LAYOUT_HUNGARIAN		  = 0x0C,
	USB_HID_LAYOUT_INTERNATIONAL	  = 0x0D,
	USB_HID_LAYOUT_ITALIAN			  = 0x0E,
	USB_HID_LAYOUT_JAPANESE			  = 0x0F,
	USB_HID_LAYOUT_KOREAN			  = 0x10,
	USB_HID_LAYOUT_LATIN_AMERICAN	  = 0x11,
	USB_HID_LAYOUT_NETHERLANDS_DUTCH  = 0x12,
	USB_HID_LAYOUT_NORWEGIAN		  = 0x13,
	USB_HID_LAYOUT_PERSIAN_FARSI	  = 0x14,
	USB_HID_LAYOUT_POLISH			  = 0x15,
	USB_HID_LAYOUT_PORTUGUESE		  = 0x16,
	USB_HID_LAYOUT_RUSSIAN			  = 0x17,
	USB_HID_LAYOUT_SLOVAK			  = 0x18,
	USB_HID_LAYOUT_SPANISH			  = 0x19,
	USB_HID_LAYOUT_SWEDISH			  = 0x1A,
	USB_HID_LAYOUT_SWISS_FRENCH		  = 0x1B,
	USB_HID_LAYOUT_SWISS_GERMAN		  = 0x1C,
	USB_HID_LAYOUT_SWITZERLAND		  = 0x1D,
	USB_HID_LAYOUT_CHINESE_BOPOMOFO	  = 0x1E,
	USB_HID_LAYOUT_TURKISH_Q		  = 0x1F,
	USB_HID_LAYOUT_TURKISH_F		  = 0x20,
	USB_HID_LAYOUT_ENGLISH_UK		  = 0x21,
	USB_HID_LAYOUT_ENGLISH_US		  = 0x22,
	USB_HID_LAYOUT_YUGOSLAVIA		  = 0x23,
	USB_HID_LAYOUT_TURKISH_Q2		  = 0x24,
	USB_HID_LAYOUT_MAX				  = 0x25,
} UsbHidKeyboardLayout;

struct UsbHidDescriptorHeader {
	uint8_t	 bLength;
	uint8_t	 bDescriptorType;
	uint16_t bcdHID;
	uint8_t	 bCountryCode;
	uint8_t	 bNumDescriptors;
	struct {
		uint8_t	 bReportDescriptorType;
		uint16_t wReportDescriptorLength;
	} reports[0];
} __attribute__((packed));

#endif