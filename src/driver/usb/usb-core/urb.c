#include "driver/usb/usb.h"
#include <driver/usb/urb.h>
#include <kernel/list.h>
#include <kernel/memory.h>

LIST_HEAD(urb_lh);

void usb_softirq_handler(void) {
	UsbRequestBlock *urb, *next;
	list_for_each_owner_safe (urb, next, &urb_lh, list) {
		list_del(&urb->list);
		if (urb->complete != NULL) { urb->complete(urb); }
	}
}

UsbRequestBlock *usb_create_urb(
	UsbEndpoint *ep, void *context, uint8_t *buffer, uint32_t len,
	void (*complete)(struct UsbRequestBlock *urb)) {
	UsbRequestBlock *urb = kmalloc(sizeof(UsbRequestBlock));
	urb->ep				 = ep;
	urb->context		 = context;
	urb->buffer			 = buffer;
	urb->len			 = len;
	urb->actual_len		 = 0;
	urb->status			 = USB_STATUS_ACK;
	urb->complete		 = complete;
	return urb;
}
