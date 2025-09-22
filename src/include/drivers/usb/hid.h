#ifndef _USB_HID_H
#define _USB_HID_H

#include <driver/usb/usb_driver.h>
#include <kernel/driver.h>

extern Driver usb_hid_driver;

typedef enum {
	USB_HID_SUBCLASS_NO	  = 0x00,
	USB_HID_SUBCLASS_BOOT = 0x01,
} UsbHidSubClass;

typedef enum {
	USB_HID_PROTOCOL_NONE	  = 0x00,
	USB_HID_PROTOCOL_KEYBOARD = 0x01,
	USB_HID_PROTOCOL_MOUSE	  = 0x02,
} UsbHidProtocol;

extern Driver	 usb_hid_driver;
extern UsbDriver usb_hid_usb_driver;

#endif