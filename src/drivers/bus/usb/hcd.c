#include <drivers/bus/usb/hcd.h>
#include <drivers/bus/usb/usb.h>
#include <drivers/usb/core/usb.h>
#include <drivers/usb/uhci.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/spinlock.h>
#include <multiple_return.h>
#include <objects/object.h>
#include <stdint.h>
#include <string.h>

LIST_HEAD(hcd_list);

extern BusOps usb_bus_ops;

DriverResult usb_create_hcd(
	DEF_MRET(UsbHcd *, hcd), uint32_t port_cnt, UsbHcdOps *hcd_ops,
	DeviceOps *ops, PhysicalDevice *physical_device,
	DeviceDriver *device_driver) {
	DriverResult ret = DRIVER_OK;

	if (usb_bus_driver.state != DRIVER_STATE_REGISTERED)
		return DRIVER_ERROR_WAITING;

	LogicalDevice *logical_device = NULL;
	DRIVER_RESULT_PASS(create_logical_device(
		&logical_device, physical_device, device_driver, ops,
		DEVICE_TYPE_BUS_CONTROLLER));

	UsbHcd *hcd = kzalloc(sizeof(UsbHcd));
	if (hcd == NULL) {
		ret = DRIVER_ERROR_OUT_OF_MEMORY;
		goto failed_create_hcd;
	}
	MRET(hcd)			   = hcd;
	hcd->device			   = logical_device;
	hcd->ops			   = hcd_ops;
	hcd->new_device_num	   = 0;
	hcd->device_count	   = 0;
	hcd->device			   = logical_device;
	logical_device->dm_ext = hcd;
	spinlock_init(&hcd->lock);

	ret = create_bus(&hcd->bus, &usb_bus_driver, &usb_bus_ops);
	if (ret != DRIVER_OK) goto failed_create_bus;

	hcd->ports = kzalloc(sizeof(UsbHcdPort) * port_cnt);
	if (hcd->ports == NULL) {
		ret = DRIVER_ERROR_OUT_OF_MEMORY;
		goto failed_create_ports;
	}
	for (int i = 0; i < port_cnt; i++) {
		hcd->ports[i].port		= i;
		hcd->ports[i].hcd		= hcd;
		hcd->ports[i].connected = 0;
	}

	list_add_tail(&hcd->list, &hcd_list);
	list_add_tail(&hcd->bus->bus_check_list, &bus_check_lh);
	list_init(&hcd->usb_device_lh);

	return DRIVER_OK;
failed_create_ports:
	delete_bus(hcd->bus);

failed_create_bus:
	kfree(hcd);

failed_create_hcd:
	delete_logical_device(logical_device);

	return ret;
}
