#include <driver/usb/descriptors.h>
#include <driver/usb/func.h>
#include <driver/usb/hub.h>
#include <driver/usb/usb.h>
#include <driver/usb/usb_dm.h>
#include <kernel/console.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <stdint.h>
#include <string.h>

void *usb_get_descriptor(
	UsbHcd *hcd, UsbDevice *device, uint8_t type, uint8_t recipient,
	uint8_t index, UsbEndpoint *ep, uint8_t value_hi, uint8_t value_lo,
	uint16_t desc_size) {
	void *desc = kmalloc(desc_size);

	UsbControlRequest usb_req = USB_BUILD_REQUEST(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, recipient,
		USB_REQ_GET_DESCRIPTOR, value_hi, value_lo, index, desc_size);

	hcd->ops->ctrl_transfer_in(hcd, device, desc, desc_size, &usb_req);
	return desc;
}

void *usb_set_descriptor(
	UsbHcd *hcd, UsbDevice *device, uint8_t type, uint8_t recipient,
	uint8_t index, UsbEndpoint *ep, uint8_t value_hi, uint8_t value_lo,
	uint16_t desc_size) {
	void *desc = kmalloc(desc_size);

	UsbControlRequest usb_req = USB_BUILD_REQUEST(
		USB_REQ_HOST_TO_DEVICE, USB_REQ_TYPE_STANDARD, recipient,
		USB_REQ_SET_DESCRIPTOR, value_hi, value_lo, index, desc_size);

	hcd->ops->ctrl_transfer_out(hcd, device, desc, desc_size, &usb_req);
	return desc;
}

struct UsbConfigDescriptor *usb_get_config_descriptor(
	UsbHcd *hcd, UsbDevice *usb_device, UsbEndpoint *ep) {
	struct UsbConfigDescriptor *desc =
		kmalloc(sizeof(struct UsbConfigDescriptor));

	UsbControlRequest usb_req = USB_BUILD_REQUEST(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_CONFIG, 0, 0,
		USB_DESC_TYPE_CONFIG_SIZE);
	hcd->ops->ctrl_transfer_in(
		hcd, usb_device, desc, USB_DESC_TYPE_CONFIG_SIZE, &usb_req);

	uint8_t *buffer = kmalloc(desc->wTotalLength);
	usb_req.wLength = desc->wTotalLength;
	hcd->ops->ctrl_transfer_in(
		hcd, usb_device, buffer, usb_req.wLength, &usb_req);

	UsbInterface *interface = NULL;
	int			  ep_num;
	int			  length = desc->wTotalLength;
	while (length > 0) {
		uint8_t type = buffer[1];
		if (type == USB_DESC_TYPE_INTERFACE) {
			struct UsbInterfaceDescriptor *interface_desc =
				(struct UsbInterfaceDescriptor *)buffer;
			interface = kmalloc(
				sizeof(UsbInterface) +
				sizeof(UsbEndpoint *) * interface_desc->bNumEndpoints);

			interface->usb_driver = NULL;
			interface->desc		  = interface_desc;

			list_add_tail(&interface->list, &usb_device->interface_lh);

			ep_num = 0;
		} else if (type == USB_DESC_TYPE_ENDPOINT) {
			struct UsbEndpointDescriptor *endpoint_desc =
				(struct UsbEndpointDescriptor *)buffer;

			UsbEndpoint *ep = kmalloc(sizeof(UsbEndpoint));
			usb_init_endpoint(usb_device, ep, endpoint_desc);

			if (interface) {
				interface->endpoints[ep_num] = ep;
				ep_num++;
			}
			list_add_tail(&ep->list, &usb_device->ep_lh);
		}
		length -= buffer[0];
		buffer += buffer[0];
	}
	return desc;
}

uint8_t usb_get_config(UsbHcd *hcd, UsbDevice *device, UsbEndpoint *ep) {
	uint8_t config;

	UsbControlRequest usb_req = USB_BUILD_REQUEST(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_CONFIGURATION, 0, 0, 0, 1);
	hcd->ops->ctrl_transfer_in(hcd, device, &config, 1, &usb_req);

	return config;
}

UsbSetupStatus usb_set_config(
	UsbHcd *hcd, UsbDevice *device, UsbEndpoint *ep, uint8_t config) {

	UsbControlRequest usb_req = USB_BUILD_REQUEST(
		USB_REQ_HOST_TO_DEVICE, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_SET_CONFIGURATION, config, 0, 0, 0);
	return hcd->ops->ctrl_transfer_out(hcd, device, NULL, 0, &usb_req);
}

struct UsbHubDescriptor *usb_get_hub_descriptor(UsbHub *hub) {
	UsbDevice				*device = hub->usb_device;
	UsbHcd					*hcd	= device->hcd;
	struct UsbHubDescriptor *desc	= kmalloc(sizeof(struct UsbHubDescriptor));

	UsbControlRequest usb_req = USB_BUILD_REQUEST(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_CLASS, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_HUB, 0, 0,
		USB_DESC_TYPE_HUB_SIZE);
	hcd->ops->ctrl_transfer_in(
		hcd, device, desc, USB_DESC_TYPE_HUB_SIZE, &usb_req);
	return desc;
}

uint32_t usb_get_hub_status(UsbHub *hub) {
	uint32_t   stat;
	UsbDevice *device = hub->usb_device;
	UsbHcd	  *hcd	  = device->hcd;

	UsbControlRequest usb_req = USB_BUILD_REQUEST(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_STATUS, 0, 0, 0, 4);
	hcd->ops->ctrl_transfer_in(hcd, device, &stat, 4, &usb_req);
	return stat;
}

uint32_t usb_get_port_status(UsbHub *hub, uint8_t port) {
	uint32_t   stat;
	UsbDevice *device = hub->usb_device;
	UsbHcd	  *hcd	  = device->hcd;

	UsbControlRequest usb_req = USB_BUILD_REQUEST(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_CLASS, USB_REQ_RECIPIENT_OTHER,
		USB_REQ_GET_STATUS, 0, 0, port, REQ_GET_PORT_STATUS_SIZE);
	hcd->ops->ctrl_transfer_in(
		hcd, device, &stat, REQ_GET_PORT_STATUS_SIZE, &usb_req);
	return stat;
}

struct UsbStringDescriptor *usb_get_string_descriptor(
	UsbHcd *hcd, UsbDevice *device, uint8_t index, UsbEndpoint *ep) {
	uint8_t buffer[2];

	UsbControlRequest usb_req = USB_BUILD_REQUEST(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_STRING, index, 0x0409, 2);

	hcd->ops->ctrl_transfer_in(hcd, device, buffer, 2, &usb_req);

	struct UsbStringDescriptor *desc =
		kmalloc(sizeof(struct UsbStringDescriptor) + buffer[0]);
	usb_req.wLength = buffer[0];
	hcd->ops->ctrl_transfer_in(hcd, device, desc, buffer[0], &usb_req);

	return desc;
}

UsbSetupStatus usb_set_address(
	UsbHcd *hcd, UsbDevice *device, UsbEndpoint *ep, uint32_t address) {

	UsbControlRequest req = USB_BUILD_REQUEST(
		USB_REQ_HOST_TO_DEVICE, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_SET_ADDRESS, address >> 8, address & 0xff, 0, 0);

	return hcd->ops->ctrl_transfer_out(hcd, device, NULL, 0, &req);
}

UsbSetupStatus usb_clear_port_feature(
	UsbHub *hub, uint8_t port, uint16_t feature) {
	UsbDevice *device = hub->usb_device;
	UsbHcd	  *hcd	  = device->hcd;

	UsbControlRequest req = USB_BUILD_REQUEST(
		USB_REQ_HOST_TO_DEVICE, USB_REQ_TYPE_CLASS, USB_REQ_RECIPIENT_OTHER,
		USB_REQ_CLEAR_FEATURE, feature >> 8, feature & 0xff, port, 0);

	return hcd->ops->ctrl_transfer_out(hcd, device, NULL, 0, &req);
}

UsbSetupStatus usb_set_port_feature(
	UsbHub *hub, uint8_t port, uint16_t feature) {
	UsbDevice *device = hub->usb_device;
	UsbHcd	  *hcd	  = device->hcd;

	UsbControlRequest req = USB_BUILD_REQUEST(
		USB_REQ_HOST_TO_DEVICE, USB_REQ_TYPE_CLASS, USB_REQ_RECIPIENT_OTHER,
		USB_REQ_SET_FEATURE, feature >> 8, feature & 0xff, port, 0);

	return hcd->ops->ctrl_transfer_out(hcd, device, NULL, 0, &req);
}

void usb_show_device_descriptor(
	UsbHcd *hcd, UsbDevice *device, UsbEndpoint *ep,
	struct UsbDeviceDescriptor *desc) {
	printk("\nUSB Device Descriptor:\n");
	printk("Length: %d\n", desc->bLength);
	printk("DescriptorType: %d\n", desc->bDescriptorType);
	printk(
		"Support USB Version: %x.%02x\n", desc->bcdUSB >> 8,
		desc->bcdUSB & 0xff);
	printk("Device Class: %d\n", desc->bDeviceClass);
	printk("Device SubClass: %d\n", desc->bDeviceSubClass);
	printk("Device Protocol: %d\n", desc->bDeviceProtocol);
	printk("Max Packet Size: %d\n", desc->bMaxPacketSize0);
	printk("Vendor ID: %d\n", desc->idVendor);
	printk("Product ID: %d\n", desc->idProduct);
	printk(
		"Device Version: %x.%02x\n", desc->bcdDevice >> 8,
		desc->bcdDevice & 0xff);

	printk("Manufacturer: ");
	struct UsbStringDescriptor *str1 =
		usb_get_string_descriptor(hcd, device, desc->iManufacturer, ep);
	for (int i = 0; i < (str1->bLength - 2) / 2; i++) {
		printk("%c", str1->wData[i]);
	}
	printk("\n");

	printk("Product: ");
	struct UsbStringDescriptor *str2 =
		usb_get_string_descriptor(hcd, device, desc->iProduct, ep);
	for (int i = 0; i < (str2->bLength - 2) / 2; i++) {
		printk("%c", str2->wData[i]);
	}
	printk("\n");

	printk("Serial Number: ");
	struct UsbStringDescriptor *str3 =
		usb_get_string_descriptor(hcd, device, desc->iSerialNumber, ep);
	for (int i = 0; i < (str3->bLength - 2) / 2; i++) {
		printk("%c", str3->wData[i]);
	}
	printk("\n");

	printk("Number of Configurations: %d\n", desc->bNumConfigurations);
}

void usb_show_hub_descriptor(struct UsbHubDescriptor *desc) {
	printk("\nUSB Hub Descriptor:\n");
	printk("Length: %d\n", desc->bLength);
	printk("DescriptorType: %d\n", desc->bDescriptorType);
	printk("Number of Ports: %d\n", desc->bNbrPorts);
	printk("Characteristics: %d\n", desc->wHubCharacteristics);
	printk("Power On To Power Good Time: %d\n", desc->bPwrOn2PwrGood);
	printk("Hub Controller Current: %d\n", desc->bHubContrCurrent);
	printk("Device Removable: %d\n", desc->DeviceRemovable);
	printk("Port Power Control Mask: %d\n", desc->PortPwrCtrlMask);
}
