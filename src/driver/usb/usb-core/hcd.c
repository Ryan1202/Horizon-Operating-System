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
SPINLOCK(bus_num_lock);
uint8_t new_bus_num = 0;

UsbHcd *usb_hcd_register(
	Device *device, char *name, uint32_t port_cnt, UsbHcdOps *interface) {
	UsbHcd *hcd = kmalloc(sizeof(UsbHcd));
	if (hcd == NULL) return NULL;

	list_add_tail(&hcd->list, &hcd_list);
	hcd->device		  = device;
	hcd->device_count = 0;

	hcd->ops = interface;

	string_t *string = kmalloc(sizeof(string_t));
	string_init(string);
	string_new(string, name, strlen(name));
	hcd->name = string;

	hcd->ports = kmalloc(sizeof(UsbHcdPort) * port_cnt);

	for (int i = 0; i < port_cnt; i++) {
		hcd->ports[i].port		= i;
		hcd->ports[i].hcd		= hcd;
		hcd->ports[i].connected = 0;
	}

	return hcd;
}