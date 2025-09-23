#ifndef _USB_HUB_H
#define _USB_HUB_H

#include <driver/usb/usb.h>
#include <driver/usb/usb_dm.h>
#include <stdint.h>

#define HUB_FEAT_C_LOCAL_POWER	0
#define HUB_FEAT_C_OVER_CURRENT 1

#define HUB_FEAT_PORT_CONNECTION   0
#define HUB_FEAT_PORT_ENABLE	   1
#define HUB_FEAT_PORT_SUSPEND	   2
#define HUB_FEAT_PORT_OVER_CURRENT 3
#define HUB_FEAT_PORT_RESET		   4
#define HUB_FEAT_PORT_POWER		   8
#define HUB_FEAT_PORT_LOW_SPEED	   9

#define HUB_FEAT_C_PORT_CONNECTION	 16
#define HUB_FEAT_C_PORT_ENABLE		 17
#define HUB_FEAT_C_PORT_SUSPEND		 18
#define HUB_FEAT_C_PORT_OVER_CURRENT 19
#define HUB_FEAT_C_PORT_RESET		 20

#define HUB_FEAT_PORT_TEST		21
#define HUB_FEAT_PORT_INDICATOR 22

typedef struct UsbHub {
	UsbDevice *usb_device;
	UsbHcd	  *hcd;

	struct UsbHubDescriptor *desc;
	struct UsbHubOps		*ops;
} UsbHub;

typedef struct UsbHubOps {
	void (*init)(struct UsbHub *hub);
	UsbSetupStatus (*clear_port_feature)(
		struct UsbHub *hub, uint8_t port, uint16_t feature);
	UsbSetupStatus (*set_port_feature)(
		struct UsbHub *hub, uint8_t port, uint16_t feature);
	uint32_t (*get_hub_status)(struct UsbHub *hub);
	uint32_t (*get_port_status)(struct UsbHub *hub, uint8_t port);
} UsbHubOps;

extern UsbHubOps usb_hub_ops;

void usb_init_hub(
	UsbHcd *hcd, UsbHub *hub, UsbEndpoint *ep0, struct UsbDevice *usb_device);

#endif