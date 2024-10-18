#include "kernel/list.h"
#include <drivers/usb/func.h>
#include <drivers/usb/usb.h>
#include <kernel/console.h>
#include <kernel/memory.h>
#include <stdint.h>

struct usb_device_descriptor *usb_get_device_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep) {
	struct usb_device_descriptor *desc =
		kmalloc(sizeof(struct usb_device_descriptor));

	usb_request_t *usb_req = usb_create_request(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_DEVICE, 0, 0,
		USB_DESC_TYPE_DEVICE_SIZE);
	usb_control_transaction_in(
		hcd, device, ep, desc, usb_req, USB_DESC_TYPE_DEVICE_SIZE);
	return desc;
}

struct usb_config_descriptor *usb_get_config_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep) {
	struct usb_config_descriptor *desc =
		kmalloc(sizeof(struct usb_config_descriptor));

	usb_request_t *usb_req = usb_create_request(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_CONFIG, 0, 0,
		USB_DESC_TYPE_CONFIG_SIZE);
	usb_control_transaction_in(
		hcd, device, ep, desc, usb_req, USB_DESC_TYPE_CONFIG_SIZE);

	uint8_t *buffer	 = kmalloc(desc->wTotalLength);
	usb_req->wLength = desc->wTotalLength;
	usb_control_transaction_in(
		hcd, device, ep, buffer, usb_req, desc->wTotalLength);

	int length = desc->wTotalLength;
	while (length > 0) {
		uint8_t type = buffer[1];
		if (type == USB_DESC_TYPE_INTERFACE) {
			struct usb_interface_descriptor *interface_desc =
				(struct usb_interface_descriptor *)buffer;
			usb_interface_t *interface = kmalloc(sizeof(usb_interface_t));

			interface->interface = interface_desc->bInterfaceNumber;
			interface->class	 = interface_desc->bInterfaceClass;
			interface->subclass	 = interface_desc->bInterfaceSubClass;
			interface->protocol	 = interface_desc->bInterfaceProtocol;

			list_add_tail(&interface->list, &device->interface_lh);

		} else if (type == USB_DESC_TYPE_ENDPOINT) {
			struct usb_endpoint_descriptor *endpoint_desc =
				(struct usb_endpoint_descriptor *)buffer;

			usb_endpoint_t *ep = usb_create_endpoint(
				endpoint_desc->bEndpointAddress & 0x0f,
				endpoint_desc->bmAttributes & 0x03,
				endpoint_desc->bEndpointAddress >> 7,
				endpoint_desc->wMaxPacketSize & 0x07ff);

			list_add_tail(&ep->list, &device->ep_lh);
		}
		length -= buffer[0];
		buffer += buffer[0];
	}
	return desc;
}

uint8_t usb_get_config(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep) {
	uint8_t config;

	usb_request_t *usb_req = usb_create_request(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_CONFIGURATION, 0, 0, 0, 1);
	usb_control_transaction_in(hcd, device, ep, &config, usb_req, 1);

	return config;
}

usb_setup_status_t usb_set_config(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, uint8_t config) {

	usb_request_t *usb_req = usb_create_request(
		USB_REQ_HOST_TO_DEVICE, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_SET_CONFIGURATION, config, 0, 0, 0);

	return usb_control_transaction_out(hcd, device, ep, NULL, usb_req, 0);
}

struct usb_hub_descriptor *usb_get_hub_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep) {
	struct usb_hub_descriptor *desc =
		kmalloc(sizeof(struct usb_hub_descriptor));

	usb_request_t *usb_req = usb_create_request(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_CLASS, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_HUB, 0, 0,
		USB_DESC_TYPE_HUB_SIZE);
	usb_control_transaction_in(
		hcd, device, ep, desc, usb_req, USB_DESC_TYPE_HUB_SIZE);
	return desc;
}

uint32_t usb_get_hub_status(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep) {
	uint32_t stat;

	usb_request_t *usb_req = usb_create_request(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_STATUS, 0, 0, 0, 4);
	usb_control_transaction_in(hcd, device, ep, &stat, usb_req, 4);
	return stat;
}

uint32_t usb_get_port_status(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, uint8_t port) {
	uint32_t stat;

	usb_request_t *usb_req = usb_create_request(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_CLASS, USB_REQ_RECIPIENT_OTHER,
		USB_REQ_GET_STATUS, 0, 0, port, REQ_GET_PORT_STATUS_SIZE);
	usb_control_transaction_in(
		hcd, device, ep, &stat, usb_req, REQ_GET_PORT_STATUS_SIZE);
	return stat;
}

struct usb_string_descriptor *usb_get_string_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, uint8_t index, usb_endpoint_t *ep) {
	uint8_t buffer[2];

	usb_request_t *usb_req = usb_create_request(
		USB_REQ_DEVICE_TO_HOST, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_STRING, index, 0x0409, 2);

	usb_control_transaction_in(hcd, device, ep, buffer, usb_req, 2);

	struct usb_string_descriptor *desc =
		kmalloc(sizeof(struct usb_string_descriptor) + buffer[0]);
	usb_req->wLength = buffer[0];
	usb_control_transaction_in(hcd, device, ep, desc, usb_req, buffer[0]);

	return desc;
}

usb_setup_status_t usb_set_address(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep,
	uint32_t address) {

	usb_request_t *req = usb_create_request(
		USB_REQ_HOST_TO_DEVICE, USB_REQ_TYPE_STANDARD, USB_REQ_RECIPIENT_DEVICE,
		USB_REQ_SET_ADDRESS, address >> 8, address & 0xff, 0, 0);

	return usb_control_transaction_out(hcd, device, ep, NULL, req, 0);
}

usb_setup_status_t usb_set_port_feature(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep, uint8_t port,
	uint16_t feature) {

	usb_request_t *req = usb_create_request(
		USB_REQ_HOST_TO_DEVICE, USB_REQ_TYPE_CLASS, USB_REQ_RECIPIENT_OTHER,
		USB_REQ_SET_FEATURE, feature >> 8, feature & 0xff, port, 0);

	return usb_control_transaction_out(hcd, device, ep, NULL, req, 0);
}

void usb_show_device_descriptor(
	usb_hcd_t *hcd, usb_device_t *device, usb_endpoint_t *ep,
	struct usb_device_descriptor *desc) {
	printk("USB Device Descriptor:\n");
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
	struct usb_string_descriptor *str1 =
		usb_get_string_descriptor(hcd, device, desc->iManufacturer, ep);
	for (int i = 0; i < (str1->bLength - 2) / 2; i++) {
		printk("%c", str1->wData[i]);
	}
	printk("\n");

	printk("Product: ");
	struct usb_string_descriptor *str2 =
		usb_get_string_descriptor(hcd, device, desc->iProduct, ep);
	for (int i = 0; i < (str2->bLength - 2) / 2; i++) {
		printk("%c", str2->wData[i]);
	}
	printk("\n");

	printk("Serial Number: ");
	struct usb_string_descriptor *str3 =
		usb_get_string_descriptor(hcd, device, desc->iSerialNumber, ep);
	for (int i = 0; i < (str3->bLength - 2) / 2; i++) {
		printk("%c", str3->wData[i]);
	}
	printk("\n");

	printk("Number of Configurations: %d\n", desc->bNumConfigurations);
}

void usb_show_hub_descriptor(struct usb_hub_descriptor *desc) {
	printk("USB Hub Descriptor:\n");
	printk("Length: %d\n", desc->bLength);
	printk("DescriptorType: %d\n", desc->bDescriptorType);
	printk("Number of Ports: %d\n", desc->bNbrPorts);
	printk("Characteristics: %d\n", desc->wHubCharacteristics);
	printk("Power On To Power Good Time: %d\n", desc->bPwrOn2PwrGood);
	printk("Hub Controller Current: %d\n", desc->bHubContrCurrent);
	printk("Device Removable: %d\n", desc->DeviceRemovable);
	printk("Port Power Control Mask: %d\n", desc->PortPwrCtrlMask);
}
