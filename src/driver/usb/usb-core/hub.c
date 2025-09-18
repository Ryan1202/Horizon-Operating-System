#include <bits.h>
#include <driver/timer_dm.h>
#include <driver/usb/descriptors.h>
#include <driver/usb/func.h>
#include <driver/usb/hcd.h>
#include <driver/usb/hub.h>
#include <driver/usb/usb.h>
#include <driver/usb/usb_dm.h>
#include <drivers/pit.h>
#include <stdint.h>

void usb_init_hub(UsbHcd *hcd, UsbDevice *device) {
	Timer		 timer;
	UsbEndpoint *ep0 =
		usb_create_endpoint(hcd, 0, USB_EP_CONTROL, USB_EP_OUT, 64);
	device->ep0 = ep0;
	timer_init(&timer);

	hcd->device_count++;
	usb_set_address(hcd, device, ep0, hcd->device_count);
	delay_ms(&timer, 2);
	device->address = hcd->device_count;

	struct UsbHubDescriptor *desc	= usb_get_hub_descriptor(hcd, device, ep0);
	// usb_show_hub_descriptor(desc);
	uint32_t				 status = usb_get_hub_status(hcd, device, ep0);

	int i;
	for (i = 1; i <= desc->bNbrPorts; i++) {

		usb_set_port_feature(hcd, device, ep0, i, HUB_FEAT_PORT_POWER);
		delay_ms(&timer, 50);
		usb_set_port_feature(hcd, device, ep0, i, HUB_FEAT_PORT_RESET);
		delay_ms(&timer, 200);

		status = usb_get_port_status(hcd, device, ep0, i);
		if (BIN_IS_EN(status, USB_PORT_STAT_CONNECTION)) {
			UsbDevice *dev = usb_create_device(
				hcd,
				(BIN_IS_EN(status, USB_PORT_STAT_LOW_SPEED) ? USB_SPEED_LOW
															: USB_SPEED_FULL),
				0);
			usb_init_device(hcd, dev);
			delay_ms(&timer, 100);
		}
	}
}