#ifndef _USB_HUB_H
#define _USB_HUB_H

#include <drivers/usb/usb.h>

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

void usb_init_hub(usb_hcd_t *hcd, usb_device_t *device);

#endif