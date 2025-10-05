#ifndef _USB_HID_MOUSE_H
#define _USB_HID_MOUSE_H

#include <driver/input/input_dm.h>
#include <drivers/bus/usb/usb.h>
#include <drivers/usb/core/urb.h>
#include <drivers/usb/core/usb.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <stdint.h>

typedef struct {
	uint8_t buttons;
	int8_t	x;
	int8_t	y;
} __attribute__((packed)) UsbHidMouseReport;

typedef struct {
	InputDevice		*device;
	UsbDevice		*usb_device;
	UsbInterface	*interface;
	UsbRequestBlock *urb;

	uint8_t *buffer;

	uint8_t last_buttons;
} UsbHidMouse;

extern DeviceOps	usb_hid_mouse_device_ops;
extern DeviceDriver usb_hid_mouse_device_driver;

DriverResult usb_hid_mouse_probe(
	UsbDevice *usb_device, UsbInterface *interface);

#endif