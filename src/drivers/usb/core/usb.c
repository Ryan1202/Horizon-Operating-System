#include <bits.h>
#include <driver/timer/timer_dm.h>
#include <drivers/bus/usb/usb.h>
#include <drivers/pit.h>
#include <drivers/usb/core/descriptors.h>
#include <drivers/usb/core/func.h>
#include <drivers/usb/core/hub.h>
#include <drivers/usb/core/usb.h>
#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <objects/attr.h>
#include <stdint.h>

DeviceOps usb_device_ops = {
	.init	 = NULL,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
};

struct UsbEndpointDescriptor ep0_desc = {
	.bLength		  = sizeof(struct UsbEndpointDescriptor),
	.bDescriptorType  = USB_DESC_TYPE_ENDPOINT,
	.bEndpointAddress = USB_EP_OUT << 7 | 0, // ep0 out
	.bmAttributes	  = USB_EP_CONTROL,
	.wMaxPacketSize	  = HOST2LE_WORD(64),
	.bInterval		  = 0,
};

UsbDevice *usb_create_device(
	UsbHcd *hcd, UsbHub *hub, UsbDeviceSpeed speed, uint8_t address) {
	ObjectAttr		attr = device_object_attr;
	DriverResult	result;
	PhysicalDevice *physical_device;

	result = create_physical_device(&physical_device, hcd->bus, &attr);
	if (result != DRIVER_OK) return NULL;

	UsbDevice *usb_device = (UsbDevice *)kzalloc(sizeof(UsbDevice));
	list_init(&usb_device->ep_lh);
	list_init(&usb_device->interface_lh);
	usb_device->desc		 = kmalloc(sizeof(struct UsbDeviceDescriptor));
	usb_device->speed		 = speed;
	usb_device->address		 = address;
	usb_device->state		 = USB_STATE_UNINITED;
	usb_device->device		 = physical_device;
	usb_device->hcd			 = hcd;
	usb_device->hub			 = hub;
	physical_device->bus_ext = usb_device;

	usb_device->ep0 = kzalloc(sizeof(UsbEndpoint));
	usb_init_endpoint(usb_device, usb_device->ep0, &ep0_desc);

	register_physical_device(physical_device, &usb_device_ops);

	return usb_device;
}

int usb_destroy_device(UsbDevice *device) {
	return kfree(device);
}

UsbControlRequest *usb_create_request(
	uint8_t direction, uint8_t type, uint8_t recipient, uint8_t request_id,
	uint8_t value_hi, uint8_t value_lo, uint16_t index, uint16_t length) {
	UsbControlRequest *request =
		(UsbControlRequest *)kzalloc(sizeof(UsbControlRequest));
	request->bmRequestType = direction << 7 | type << 5 | recipient;
	request->bRequest	   = request_id;
	request->wValue		   = HOST2LE_WORD(value_hi << 8 | value_lo);
	request->wIndex		   = HOST2LE_WORD(index);
	request->wLength	   = HOST2LE_WORD(length);
	return request;
}

void usb_init_endpoint(
	UsbDevice *usb_device, UsbEndpoint *ep,
	struct UsbEndpointDescriptor *desc) {
	ep->desc = desc;

	ep->pipe = usb_device->hcd->ops->create_pipeline(usb_device, ep);
}

int usb_probe_device(UsbHcd *hcd, UsbHub *hub, UsbDeviceSpeed speed) {
	UsbDevice *usb_device = usb_create_device(hcd, hub, speed, 0);

	struct UsbDeviceDescriptor *desc =
		usb_get_device_descriptor(hcd, usb_device);
	// usb_show_device_descriptor(hcd, device, desc);

	uint8_t address;
	spin_lock(&hcd->lock);
	hcd->new_device_num++;
	hcd->device_count++;
	address = hcd->new_device_num;
	spin_unlock(&hcd->lock);
	usb_set_address(hcd, usb_device, address);

	Timer timer;
	timer_init(&timer);
	delay_ms(&timer, 2);

	usb_device->address = address;
	usb_get_config_descriptor(hcd, usb_device);

	usb_set_config(hcd, usb_device, 1);

	if (desc->bDeviceClass == USB_CLASS_HUB) {
		UsbHub *new_hub		= kzalloc(sizeof(UsbHub));
		new_hub->usb_device = usb_device;
		new_hub->ops		= &usb_hub_ops;
		new_hub->hcd		= hcd;

		new_hub->desc = usb_get_hub_descriptor(new_hub);
		// usb_show_hub_descriptor(desc);

		usb_init_hub(hcd, new_hub, usb_device);
	}

	usb_device->state = USB_STATE_INITED;
	return 0;
}