#include "driver/timer_dm.h"
#include <bits.h>
#include <drivers/pit.h>
#include <drivers/usb/hcd.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/usb.h>
#include <kernel/console.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <stdint.h>

#define DEFAULT_TD_COUNT 4

void *uhci_create_sched(void);

UsbHcdOps uhci_interface = {
	.create_sched	   = uhci_create_sched,
	.ctrl_transfer_in  = uhci_ctrl_transfer_in,
	.ctrl_transfer_out = uhci_ctrl_transfer_out,
};

void *uhci_create_sched(void) {
	UhciSched *sched	  = kmalloc(sizeof(UhciSched));
	sched->qh.qh_addr_phy = vir2phy((uint32_t)&sched->qh);
	sched->qh.qe_link	  = UHCI_TERMINATE;
	sched->qh.qh_link	  = UHCI_TERMINATE;
	sched->td_count		  = DEFAULT_TD_COUNT;
	sched->td_index		  = 0;
	sched->tds			  = kmalloc(sizeof(UhciTd) * sched->td_count);

	for (int i = 0; i < sched->td_count; i++) {
		sched->tds[i].link		  = UHCI_TERMINATE;
		sched->tds[i].td_addr_phy = vir2phy((uint32_t)&sched->tds[i]);
	}
	return sched;
}

void uhci_send_token_packet(
	UsbDevice *device, UsbEndpoint *ep, UhciQh *qh, uint8_t data_toggle,
	void *buffer, uint8_t packet_id, int length) {
	UsbDevice *usb_device = device;
	UhciSched *sched	  = ep->sched;
	if (sched->td_index >= sched->td_count) {
		printk("[UHCI]UhciSched td is full.\n");
		return;
	}
	UhciTd *td = &sched->tds[sched->td_index++];

	td->packet_id	= packet_id;
	td->device_addr = usb_device->address & 0x7f;
	td->endpoint	= ep->endpoint & 0x0f;
	td->data_toggle = data_toggle;
	td->max_length	= length > 0 ? (length - 1) : 0;

	td->active = 1;
	td->actlen = 0;

	if (buffer != NULL) td->buf_addr_phy = vir2phy((uint32_t)buffer);
	else td->buf_addr_phy = 0;

	td->prev_ptr	= qh->last_ptr;
	UhciTd *last_td = (UhciTd *)qh->last_ptr;
	if (last_td != NULL) {
		last_td->link = BIN_EN(
			BIN_DIS(td->td_addr_phy, UHCI_QH_TD_SELECT), UHCI_VERTICAL_FIRST);
	} else {
		qh->qe_link = BIN_DIS(td->td_addr_phy, UHCI_QH_TD_SELECT);
	}
	td->link = UHCI_TERMINATE;

	qh->last_ptr = (uint32_t)td;
}

static inline int uhci_setup_transcation(
	UsbDevice *device, UsbEndpoint *ep, UhciQh *qh, void *buffer,
	uint32_t length) {
	uhci_send_token_packet(
		device, ep, qh, 0, buffer, USB_PACKET_ID_SETUP, length);
	return 0;
}

static inline int uhci_in_transcation(
	UsbDevice *device, UsbEndpoint *ep, UhciQh *qh, int data_toggle,
	void *buffer, uint32_t length) {
	uhci_send_token_packet(
		device, ep, qh, data_toggle, buffer, USB_PACKET_ID_IN, length);
	return 0;
}

static inline int uhci_out_transcation(
	UsbDevice *device, UsbEndpoint *ep, UhciQh *qh, int data_toggle,
	void *buffer, uint32_t length) {
	uhci_send_token_packet(
		device, ep, qh, data_toggle, buffer, USB_PACKET_ID_OUT, length);
	return 0;
}

int uhci_wait_transfer(UhciQh *qh) {
	Timer timer;
	timer_init(&timer);
	int		timeout = 150;
	UhciTd *td		= (UhciTd *)qh->last_ptr;
	while (timeout > 0) {
		if (td->active == 0) { return 1; }

		delay_ms(&timer, 10);
		timeout--;
	}
	td			  = &(((UhciSched *)qh)->tds[0]);
	uint32_t *raw = (uint32_t *)td; // TD 在内存首地址
	printk(
		"TD raw: w0=%08x w1=%08x w2=%08x w3=%08x\n", raw[0], raw[1], raw[2],
		raw[3]);
	printk(
		" decoded: pid=%02x dev=%u ep=%u toggle=%u maxlen=%u active=%u\n",
		td->packet_id, td->device_addr, td->endpoint, td->data_toggle,
		td->max_length, td->active);
	printk("[UHCI]td %#08x(phy %#08x) timeout.", td, td->td_addr_phy);
	return -1;
}

UsbSetupStatus uhci_ctrl_transfer_in(
	UsbHcd *hcd, UsbDevice *device, void *buffer, uint32_t data_length,
	UsbRequest *usb_req) {
	UhciSched *sched = device->ep0->sched;
	UhciQh	  *qh	 = &sched->qh;
	qh->qe_link		 = UHCI_TERMINATE;
	qh->qh_link		 = UHCI_TERMINATE;
	uhci_setup_transcation(device, device->ep0, qh, usb_req, 8);
	uhci_in_transcation(device, device->ep0, qh, 0, buffer, data_length);
	uhci_out_transcation(device, device->ep0, qh, 1, NULL, 0);

	uhci_skel_add_qh(hcd->device->private_data, qh, LOW_SPEED);
	uhci_wait_transfer(qh);

	UhciTd		  *last_td = (UhciTd *)qh->last_ptr;
	UsbSetupStatus result;

	if (last_td->stalled) {
		result = USB_SETUP_STALLED;
	} else if (last_td->crc_timeout_Error) {
		result = USB_SETUP_CRC_TIMEOUT_ERR;
	} else if (last_td->bitstuff_Error) {
		result = USB_SETUP_BITSTUFF_ERR;
	} else if (last_td->databuffer_Error) {
		result = USB_SETUP_DATABUFFER_ERR;
	} else if (last_td->NAK_received) {
		result = USB_SETUP_NAK_RECV;
	} else {
		result = USB_SETUP_SUCCESS;
	}
	uhci_skel_del_qh(hcd->device->private_data, qh, LOW_SPEED);
	sched->td_index = 0;

	return result;
}

UsbSetupStatus uhci_ctrl_transfer_out(
	UsbHcd *hcd, UsbDevice *device, void *buffer, uint32_t data_length,
	UsbRequest *usb_req) {
	UhciSched *sched = device->ep0->sched;
	UhciQh	  *qh	 = &sched->qh;
	qh->qe_link		 = UHCI_TERMINATE;
	qh->qh_link		 = UHCI_TERMINATE;
	uhci_setup_transcation(device, device->ep0, qh, usb_req, 8);
	if (data_length != 0) {
		uhci_out_transcation(device, device->ep0, qh, 0, buffer, data_length);
	}
	uhci_in_transcation(device, device->ep0, qh, 1, NULL, 0);

	uhci_skel_add_qh(hcd->device->private_data, qh, LOW_SPEED);
	uhci_wait_transfer(qh);

	UhciTd		  *last_td = (UhciTd *)qh->last_ptr;
	UsbSetupStatus result;

	if (last_td->stalled) {
		result = USB_SETUP_STALLED;
	} else if (last_td->crc_timeout_Error) {
		result = USB_SETUP_CRC_TIMEOUT_ERR;
	} else if (last_td->bitstuff_Error) {
		result = USB_SETUP_BITSTUFF_ERR;
	} else if (last_td->databuffer_Error) {
		result = USB_SETUP_DATABUFFER_ERR;
	} else if (last_td->NAK_received) {
		result = USB_SETUP_NAK_RECV;
	} else {
		result = USB_SETUP_SUCCESS;
	}
	uhci_skel_del_qh(hcd->device->private_data, qh, LOW_SPEED);
	sched->td_index = 0;

	return result;
}
