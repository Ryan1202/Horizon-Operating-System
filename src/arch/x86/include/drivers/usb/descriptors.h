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

#endif