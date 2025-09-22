#include <driver/usb/usb_dm.h>
#include <driver/usb/usb_driver.h>
#include <kernel/driver.h>

DriverResult register_usb_driver(UsbDriver *usb_driver) {
	list_add_tail(
		&usb_driver->list, &usb_driver_lh[usb_driver->interface_type]);
	return DRIVER_RESULT_OK;
}