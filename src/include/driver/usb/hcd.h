#ifndef HCD_H
#define HCD_H

#include "driver/bus_dm.h"
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <stdint.h>
#include <string.h>

struct UsbDevice;
struct UsbTransfer;
struct UsbRequest;

typedef struct UsbHcdPort {
	uint32_t	   port;
	struct UsbHcd *hcd;

	uint8_t suspend;
	uint8_t enable;
	uint8_t connected;
} UsbHcdPort;

typedef struct UsbHcdOps {
	void *(*create_sched)(void);
	enum UsbSetupStatus (*ctrl_transfer_in)(
		struct UsbHcd *hcd, struct UsbDevice *device, void *buffer,
		uint32_t data_length, struct UsbRequest *usb_req);
	enum UsbSetupStatus (*ctrl_transfer_out)(
		struct UsbHcd *hcd, struct UsbDevice *device, void *buffer,
		uint32_t data_length, struct UsbRequest *usb_req);
} UsbHcdOps;

typedef struct UsbHcd {
	list_t		list;
	UsbHcdPort *ports;

	UsbHcdOps *ops;

	string_t name;

	BusControllerDevice *bus_ctrlr_device;
	Device				*device;
	Bus					*bus;

	uint8_t device_count;

	list_t usb_devices;
} UsbHcd;

UsbHcd *usb_hcd_register(
	DeviceDriver *device_driver, Device *device, char *name, int name_len,
	uint32_t port_cnt, UsbHcdOps *ops);

#endif // HCD_H