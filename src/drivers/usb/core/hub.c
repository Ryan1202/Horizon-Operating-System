#include "kernel/console.h"
#include <bits.h>
#include <driver/timer/timer_dm.h>
#include <drivers/bus/usb/hcd.h>
#include <drivers/bus/usb/usb.h>
#include <drivers/pit.h>
#include <drivers/usb/core/descriptors.h>
#include <drivers/usb/core/func.h>
#include <drivers/usb/core/hub.h>
#include <drivers/usb/core/usb.h>
#include <kernel/memory.h>
#include <stdint.h>

UsbHubOps usb_hub_ops = {
	.init				= NULL,
	.clear_port_feature = usb_clear_port_feature,
	.set_port_feature	= usb_set_port_feature,
	.get_hub_status		= usb_get_hub_status,
	.get_port_status	= usb_get_port_status,
};

void usb_init_hub(UsbHcd *hcd, UsbHub *hub, UsbDevice *usb_device) {
	Timer timer;
	timer_init(&timer);

	uint32_t status = hub->ops->get_hub_status(hub);

	int			   i;
	UsbSetupStatus status2;

	uint32_t port_status[hub->desc->bNbrPorts];
	for (i = 0; i < hub->desc->bNbrPorts; i++) {
		port_status[i] = hub->ops->get_port_status(hub, i + 1);
		if (BIN_IS_EN(port_status[i], USB_PORT_STAT_CONNECTION)) {
			status2 =
				hub->ops->set_port_feature(hub, i + 1, HUB_FEAT_PORT_POWER);
			if (status2 == USB_SETUP_CRC_TIMEOUT_ERR) continue;
		}
	}
	delay_ms(&timer, hub->desc->bPwrOn2PwrGood * 2);

	for (i = 0; i < hub->desc->bNbrPorts; i++) {
		if (BIN_IS_EN(port_status[i], USB_PORT_STAT_CONNECTION)) {
			status2 =
				hub->ops->set_port_feature(hub, i + 1, HUB_FEAT_PORT_RESET);
			if (status2 == USB_SETUP_CRC_TIMEOUT_ERR) continue;
		}
	}
	printk("%d ", timer_get_counter());
	delay_ms(&timer, 200);
	printk("%d ", timer_get_counter());

	for (i = 0; i < hub->desc->bNbrPorts; i++) {
		status = port_status[i];
		if (BIN_IS_EN(status, USB_PORT_STAT_CONNECTION)) {
			UsbDeviceSpeed speed =
				BIN_IS_EN(status, USB_PORT_STAT_LOW_SPEED)	  ? USB_SPEED_LOW
				: BIN_IS_EN(status, USB_PORT_STAT_HIGH_SPEED) ? USB_SPEED_HIGH
															  : USB_SPEED_FULL;

			usb_probe_device(hcd, hub, speed);
		}
	}
	delay_ms(&timer, 100);
}