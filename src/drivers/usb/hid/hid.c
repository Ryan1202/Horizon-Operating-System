#include <drivers/bus/usb/usb_driver.h>
#include <drivers/usb/hid.h>
#include <drivers/usb/keyboard.h>
#include <drivers/usb/mouse.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/initcall.h>
#include <string.h>

DriverResult usb_hid_probe(UsbDevice *device, UsbInterface *interface);

DriverDependency usb_hid_dependencies[] = {
	{
		.in_type		   = DRIVER_DEPENDENCY_TYPE_BUS,
		.dependency_in_bus = {BUS_TYPE_USB, 0},
		.out_bus		   = NULL,
	 },
};

Driver usb_hid_driver = {
	.short_name = STRING_INIT("HID"),
};
UsbDriver usb_hid_usb_driver = {
	.driver			= &usb_hid_driver,
	.interface_type = USB_INTERFACE_TYPE_HID,
	.probe			= usb_hid_probe,
	.remove			= NULL,
};

DriverResult usb_hid_probe(UsbDevice *usb_device, UsbInterface *interface) {
	switch (interface->desc->bInterfaceSubClass) {
	case USB_HID_SUBCLASS_NO:
		// printk("HID Interface Found\n");
		return DRIVER_ERROR_UNSUPPORT_DEVICE;
	case USB_HID_SUBCLASS_BOOT:
		// printk("HID Boot Interface Found\n");
		break;
	}

	switch (interface->desc->bInterfaceProtocol) {
	case USB_HID_PROTOCOL_NONE:
		printk("HID Protocol: None\n");
		break;
	case USB_HID_PROTOCOL_KEYBOARD:
		// printk("HID Protocol: Keyboard\n");
		if (interface->desc->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT) {
			usb_hid_keyboard_probe(usb_device, interface);
		}
		break;
	case USB_HID_PROTOCOL_MOUSE:
		// printk("HID Protocol: Mouse\n");
		if (interface->desc->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT) {
			// 先用基础功能，TODO: Report Descriptor
			usb_hid_mouse_probe(usb_device, interface);
		}
		break;
	default:
		printk(
			"HID Protocol: Unknown (%d)\n",
			interface->desc->bInterfaceProtocol);
		break;
	}
	return DRIVER_OK;
}

static __init void usb_hid_driver_entry(void) {
	register_driver(&usb_hid_driver);
	register_usb_driver(&usb_hid_usb_driver);
}

driver_initcall(usb_hid_driver_entry);
