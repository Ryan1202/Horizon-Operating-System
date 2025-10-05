#ifndef HCD_H
#define HCD_H

#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <stdint.h>

struct UsbDevice;
struct UsbRequestBlock;
struct UsbControlRequest;
struct UsbEndpoint;

typedef struct UsbHcdPort {
	uint32_t	   port;
	struct UsbHcd *hcd;

	uint8_t suspend;
	uint8_t enable;
	uint8_t connected;
} UsbHcdPort;

typedef struct UsbHcdOps {
	void *(*create_pipeline)(
		struct UsbDevice *usb_device, struct UsbEndpoint *endpoint);
	enum UsbSetupStatus (*ctrl_transfer_in)(
		struct UsbHcd *hcd, struct UsbDevice *device, void *buffer,
		uint32_t data_length, struct UsbControlRequest *usb_req);
	enum UsbSetupStatus (*ctrl_transfer_out)(
		struct UsbHcd *hcd, struct UsbDevice *device, void *buffer,
		uint32_t data_length, struct UsbControlRequest *usb_req);
	void (*add_interrupt_transfer)(
		struct UsbHcd *hcd, struct UsbDevice *device, struct UsbEndpoint *ep,
		struct UsbRequestBlock *urb);
	void (*interrupt_transfer)(struct UsbHcd *hcd, struct UsbEndpoint *ep);
} UsbHcdOps;

typedef struct UsbHcd {
	list_t		list;
	UsbHcdPort *ports;

	UsbHcdOps *ops;

	// BusControllerDevice *bus_ctrlr_device;
	LogicalDevice *device;
	Bus			  *bus;

	uint8_t	   new_device_num;
	uint8_t	   device_count;
	spinlock_t lock;

	list_t usb_device_lh;
} UsbHcd;

DriverResult usb_create_hcd(
	DEF_MRET(UsbHcd *, hcd), uint32_t port_cnt, UsbHcdOps *hcd_ops,
	DeviceOps *ops, PhysicalDevice *physical_device,
	DeviceDriver *device_driver);

#endif // HCD_H