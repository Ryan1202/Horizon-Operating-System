#include <bits.h>
#include <driver/timer_dm.h>
#include <driver/usb/descriptors.h>
#include <driver/usb/func.h>
#include <driver/usb/hcd.h>
#include <driver/usb/hub.h>
#include <driver/usb/usb.h>
#include <driver/usb/usb_dm.h>
#include <drivers/pit.h>
#include <kernel/memory.h>
#include <stdint.h>

UsbHubOps usb_hub_ops = {
	.init				= NULL,
	.clear_port_feature = usb_clear_port_feature,
	.set_port_feature	= usb_set_port_feature,
	.get_hub_status		= usb_get_hub_status,
	.get_port_status	= usb_get_port_status,
};

void usb_init_hub(
	UsbHcd *hcd, UsbHub *hub, UsbEndpoint *ep0, UsbDevice *usb_device) {
	Timer timer;
	timer_init(&timer);

	uint32_t status = hub->ops->get_hub_status(hub);

	int			   i;
	UsbSetupStatus status2;
	UsbEndpoint	  *endpoints =
		kmalloc(sizeof(UsbEndpoint) * hub->desc->bNbrPorts);
	for (i = 1; i <= hub->desc->bNbrPorts; i++) {
		status = hub->ops->get_port_status(hub, i);
		if (BIN_IS_EN(status, USB_PORT_STAT_CONNECTION)) {
			status2 = hub->ops->set_port_feature(hub, i, HUB_FEAT_PORT_POWER);
			if (status2 == USB_SETUP_CRC_TIMEOUT_ERR) continue;
			delay_ms(&timer, hub->desc->bPwrOn2PwrGood * 2);
			status2 = hub->ops->set_port_feature(hub, i, HUB_FEAT_PORT_RESET);
			if (status2 == USB_SETUP_CRC_TIMEOUT_ERR) continue;
			delay_ms(&timer, 200);

			UsbDeviceSpeed speed =
				BIN_IS_EN(status, USB_PORT_STAT_LOW_SPEED)	  ? USB_SPEED_LOW
				: BIN_IS_EN(status, USB_PORT_STAT_HIGH_SPEED) ? USB_SPEED_HIGH
															  : USB_SPEED_FULL;
			UsbDevice *dev = usb_create_device(hcd, hub, speed, 0);
			struct UsbEndpointDescriptor *endpoint_desc =
				kmalloc(sizeof(struct UsbEndpointDescriptor));
			endpoint_desc->bLength = sizeof(struct UsbEndpointDescriptor);
			endpoint_desc->bDescriptorType	= USB_DESC_TYPE_ENDPOINT;
			endpoint_desc->bEndpointAddress = USB_EP_OUT << 7 | 0; // ep0 out
			endpoint_desc->bmAttributes		= USB_EP_CONTROL;
			endpoint_desc->wMaxPacketSize	= HOST2LE_WORD(64);
			endpoint_desc->bInterval		= 0;
			usb_init_device(hcd, &endpoints[i - 1], endpoint_desc, dev);
			delay_ms(&timer, 100);
		}
	}
}