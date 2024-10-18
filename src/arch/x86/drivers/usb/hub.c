#include "bits.h"
#include "drivers/usb/uhci.h"
#include <drivers/pit.h>
#include <drivers/usb/func.h>
#include <drivers/usb/hcd.h>
#include <drivers/usb/hub.h>
#include <drivers/usb/usb.h>
#include <stdint.h>

void usb_init_hub(usb_hcd_t *hcd, usb_device_t *device) {
	usb_endpoint_t ep = usb_default_ep;
	hcd->device_count++;
	usb_set_address(hcd, device, &ep, hcd->device_count);
	delay(2);
	device->address					= hcd->device_count;
	struct usb_hub_descriptor *desc = usb_get_hub_descriptor(hcd, device, &ep);
	usb_show_hub_descriptor(desc);

	uint32_t status = usb_get_hub_status(hcd, device, &ep);

	int i;
	for (i = 1; i <= desc->bNbrPorts; i++) {

		usb_set_port_feature(hcd, device, &ep, i, HUB_FEAT_PORT_POWER);
		delay(50 / 10);
		usb_set_port_feature(hcd, device, &ep, i, HUB_FEAT_PORT_RESET);
		delay(200 / 10);

		status = usb_get_port_status(hcd, device, &ep, i);
		if (BIN_IS_EN(status, USB_PORT_STAT_CONNECTION)) {
			usb_device_t *dev = usb_create_device(
				(BIN_IS_EN(status, USB_PORT_STAT_LOW_SPEED) ? USB_SPEED_LOW
															: USB_SPEED_FULL),
				0);
			usb_init_device(hcd, dev);
			delay(100 / 10);
		}
	}
}