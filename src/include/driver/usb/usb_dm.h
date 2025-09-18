#ifndef _USB_DM_H
#define _USB_DM_H

#include <driver/usb/descriptors.h>
#include <driver/usb/usb.h>
#include <kernel/device_manager.h>
#include <kernel/list.h>
#include <stdint.h>

typedef struct UsbDevice {
	list_t	list;
	uint8_t address;

	list_t ep_lh;
	list_t interface_lh;

	UsbDeviceState state;
	Device		  *device;

	struct UsbDeviceDescriptor *desc;
	UsbDeviceSpeed				speed;
	struct UsbEndpoint		   *ep0;

	void *private_data;
} UsbDevice;

typedef struct {
	uint8_t	   hcd_count;
	spinlock_t hcd_count_lock;
} UsbDeviceManager;

extern UsbDeviceManager usb_dm_ext;
extern DeviceManager	usb_dm;

DriverResult register_usb_device(
	DeviceDriver *driver, Device *device, UsbDevice *usb_device,
	ObjectAttr *attr);

#endif