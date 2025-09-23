#ifndef _USB_HID_KEYBOARD_H
#define _USB_HID_KEYBOARD_H

#include "driver/usb/usb.h"
#include "driver/usb/usb_dm.h"
#include <driver/usb/urb.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <stdint.h>

typedef struct {
	uint8_t modifier_keys;
	uint8_t reserved;
	uint8_t keycodes[6];
} __attribute__((packed)) UsbHidKeyboardReport;

typedef struct {
	Device			*device;
	UsbDevice		*usb_device;
	UsbInterface	*interface;
	UsbRequestBlock *urb;

	uint8_t *buffer;

	uint8_t last_keys[7];
} UsbHidKeyboard;

extern DeviceOps	usb_hid_keyboard_device_ops;
extern DeviceDriver usb_hid_keyboard_device_driver;

DriverResult usb_hid_keyboard_probe(
	UsbDevice *usb_device, UsbInterface *interface);

#endif