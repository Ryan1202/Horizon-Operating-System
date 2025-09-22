#ifndef _USB_URB_H
#define _USB_URB_H

#include <driver/usb/usb.h>
#include <kernel/list.h>

typedef struct UsbRequestBlock {
	list_t		 list;
	UsbEndpoint *ep;
	void		*context;
	uint8_t		*buffer;
	uint32_t	 len;
	uint32_t	 actual_len;
	UsbStatus	 status;

	void (*complete)(struct UsbRequestBlock *urb);
} UsbRequestBlock;

extern list_t urb_lh;

void			 usb_softirq_handler(void);
UsbRequestBlock *usb_create_urb(
	UsbEndpoint *ep, void *context, uint8_t *buffer, uint32_t len,
	void (*complete)(struct UsbRequestBlock *urb));

#endif