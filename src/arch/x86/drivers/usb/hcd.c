#include <drivers/usb/hcd.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/usb.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <stdint.h>
#include <string.h>

LIST_HEAD(hcd_list);

usb_hcd_t *usb_hcd_register(
	device_t *device, char *name, uint32_t port_cnt,
	usb_hcd_interface_t *interface) {
	usb_hcd_t *hcd = kmalloc(sizeof(usb_hcd_t));
	if (hcd == NULL) return NULL;

	list_add_tail(&hcd->list, &hcd_list);
	hcd->device		  = device;
	hcd->device_count = 0;

	hcd->interface = interface;

	string_t *string = kmalloc(sizeof(string_t));
	string_init(string);
	string_new(string, name, strlen(name));
	hcd->name = string;

	hcd->ports = kmalloc(sizeof(usb_hcd_port_t) * port_cnt);

	for (int i = 0; i < port_cnt; i++) {
		hcd->ports[i].port		= i;
		hcd->ports[i].hcd		= hcd;
		hcd->ports[i].connected = 0;
	}

	return hcd;
}