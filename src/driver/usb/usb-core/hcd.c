#include "driver/bus_dm.h"
#include "driver/usb/usb_dm.h"
#include "kernel/bus_driver.h"
#include "kernel/device_driver.h"
#include "kernel/spinlock.h"
#include <driver/usb/hcd.h>
#include <driver/usb/usb.h>
#include <drivers/bus/usb.h>
#include <drivers/usb/uhci.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <stdint.h>
#include <string.h>

LIST_HEAD(hcd_list);

extern BusOps usb_bus_ops;

UsbHcd *usb_hcd_register(
	DeviceDriver *device_driver, Device *device, char *name, int name_len,
	uint32_t port_cnt, UsbHcdOps *ops) {
	BusControllerDevice *bus_controller_device =
		kmalloc(sizeof(BusControllerDevice));
	string_new(&bus_controller_device->short_name, name, name_len);
	bus_controller_device->bus_controller_ops = NULL;

	ObjectAttr attr = driver_object_attr;
	register_bus_controller_device(
		device_driver, &usb_bus_driver, device, bus_controller_device, &attr);

	uint8_t bus_num;
	spin_lock(&usb_dm_ext.hcd_count_lock);
	bus_num = usb_dm_ext.hcd_count++;
	spin_unlock(&usb_dm_ext.hcd_count_lock);

	char  bus_name[4];
	char *next	 = itoa(bus_name, bus_num, 10);
	*next		 = '\0';
	Bus *bus	 = kmalloc(sizeof(Bus));
	bus->bus_num = bus_num;
	bus->ops	 = &usb_bus_ops;
	string_new(&bus->name, bus_name, next - bus_name);
	register_bus(&usb_bus_driver, device, bus, &attr);

	UsbHcd *hcd = kmalloc(sizeof(UsbHcd));
	if (hcd == NULL) return NULL;

	list_add_tail(&hcd->list, &hcd_list);
	hcd->bus_ctrlr_device = bus_controller_device;
	hcd->device			  = bus_controller_device->device;
	hcd->device_count	  = 0;
	hcd->bus			  = bus;
	hcd->ops			  = ops;
	string_new(&hcd->name, name, name_len);

	hcd->ports = kmalloc(sizeof(UsbHcdPort) * port_cnt);
	for (int i = 0; i < port_cnt; i++) {
		hcd->ports[i].port		= i;
		hcd->ports[i].hcd		= hcd;
		hcd->ports[i].connected = 0;
	}

	return hcd;
}