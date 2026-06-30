#include <bits.h>
#include <driver/timer/timer_dm.h>
#include <drivers/bus/usb/hcd.h>
#include <drivers/bus/usb/usb.h>
#include <drivers/pit.h>
#include <drivers/usb/core/urb.h>
#include <drivers/usb/core/usb.h>
#include <drivers/usb/uhci.h>
#include <kernel/barrier.h>
#include <kernel/console.h>
#include <kernel/dma.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

void *uhci_create_pipeline(UsbDevice *usb_device, UsbEndpoint *endpoint);

void uhci_add_interrupt_transfer(
	UsbHcd *hcd, UsbDevice *device, UsbEndpoint *ep,
	struct UsbRequestBlock *urb);
void uhci_interrupt_transfer(UsbHcd *hcd, UsbEndpoint *ep);

UsbHcdOps uhci_hcd_ops = {
	.create_pipeline		= uhci_create_pipeline,
	.ctrl_transfer_in		= uhci_ctrl_transfer_in,
	.ctrl_transfer_out		= uhci_ctrl_transfer_out,
	.add_interrupt_transfer = uhci_add_interrupt_transfer,
	.interrupt_transfer		= uhci_interrupt_transfer,
};

void *uhci_create_pipeline(UsbDevice *usb_device, UsbEndpoint *endpoint) {
	Uhci *uhci = usb_device->hcd->device->private_data;

	// Pipeline 用普通 kmalloc
	UhciPipeline *pipe = kzalloc(sizeof(UhciPipeline));
	if (pipe == NULL) return NULL;

	// QH 从 pool 分配
	size_t qh_dma;
	pipe->qh = dma_pool_alloc(uhci->td_qh_pool, &qh_dma);
	if (pipe->qh == NULL) {
		kfree(pipe);
		return NULL;
	}
	pipe->qh->dma_addr = qh_dma;
	pipe->qh->qe_link  = UHCI_TERMINATE;
	pipe->qh->qh_link  = UHCI_TERMINATE;
	pipe->qh->enqueued = 0;
	pipe->qh->next	   = NULL;
	pipe->qh->endpoint = endpoint;

	uint8_t ep_type = endpoint->desc->bmAttributes & 0x03;
	if (ep_type == USB_EP_INTERRUPT) {
		int type = aligned_down_log2n(endpoint->desc->bInterval | 1);
		uhci_skel_add_qh(usb_device->hcd->device->private_data, pipe->qh, type);
	} else if (ep_type == USB_EP_CONTROL) {
		uhci_skel_add_qh(
			usb_device->hcd->device->private_data, pipe->qh, ASYNC);
	} else {
		uhci_skel_add_qh(
			usb_device->hcd->device->private_data, pipe->qh, ASYNC);
	}
	pipe->first_td = NULL;
	pipe->last_td  = NULL;
	return pipe;
}

UhciTd *uhci_alloc_td(Uhci *uhci) {
	size_t	dma;
	UhciTd *td;
	td = dma_pool_alloc(uhci->td_qh_pool, &dma);
	if (td == NULL) {
		return NULL;
	}
	memset(td, 0, sizeof(UhciTd));
	td->dma_addr = dma;
	return td;
}

void uhci_free_td(Uhci *uhci, UhciTd *td) {
	dma_pool_free(uhci->td_qh_pool, td);
}

void uhci_free_all_td(Uhci *uhci, UhciPipeline *pipe) {
	UhciTd *td, *next;
	for (td = pipe->first_td; td != NULL; td = next) {
		next = td->next;
		uhci_free_td(uhci, td);
	}
	pipe->qh->first_td = NULL;
	pipe->first_td	   = NULL;
	pipe->last_td	   = NULL;
}

UhciTd *uhci_send_token_packet(
	UsbDevice *device, UsbEndpoint *ep, UhciQh *qh, uint8_t data_toggle,
	void *buffer, uint8_t packet_id, int length) {
	UsbDevice	 *usb_device = device;
	Uhci		 *uhci		 = usb_device->hcd->device->private_data;
	UhciPipeline *pipe		 = ep->pipe;
	UhciTd		 *td		 = uhci_alloc_td(uhci);
	if (td == NULL) return NULL;

	td->packet_id		= packet_id;
	td->device_addr		= usb_device->address & 0x7f;
	td->endpoint		= ep->desc->bEndpointAddress & 0x0f;
	td->data_toggle		= data_toggle;
	td->max_length		= length > 0 ? (length - 1) : 0x7ff;
	td->lowspeed_device = (usb_device->speed == USB_SPEED_LOW) ? 1 : 0;

	td->active = 1;
	td->actlen = 0;

	td->error_count = 3;

	if (buffer != NULL) td->buf_addr_phy = vir2phy((size_t)buffer);
	else td->buf_addr_phy = 0;

	if (pipe->first_td != NULL && pipe->last_td != NULL) {
		UhciTd *last_td = pipe->last_td;
		pipe->last_td	= td;
		last_td->link	= BIN_DIS(td->dma_addr, UHCI_QH_TD_SELECT);
		last_td->link	= BIN_EN(last_td->link, UHCI_VERTICAL_FIRST);
		last_td->next	= td;
	} else {
		qh->first_td   = td;
		pipe->first_td = td;
		pipe->last_td  = td;
	}
	td->link = UHCI_TERMINATE;
	return td;
}

static inline UhciTd *uhci_setup_transcation(
	UsbDevice *device, UsbEndpoint *ep, UhciQh *qh, void *buffer,
	uint32_t length) {
	return uhci_send_token_packet(
		device, ep, qh, 0, buffer, USB_PACKET_ID_SETUP, length);
}

static inline UhciTd *uhci_in_transcation(
	UsbDevice *device, UsbEndpoint *ep, UhciQh *qh, int data_toggle,
	void *buffer, uint32_t length) {
	return uhci_send_token_packet(
		device, ep, qh, data_toggle, buffer, USB_PACKET_ID_IN, length);
}

static inline UhciTd *uhci_out_transcation(
	UsbDevice *device, UsbEndpoint *ep, UhciQh *qh, int data_toggle,
	void *buffer, uint32_t length) {
	return uhci_send_token_packet(
		device, ep, qh, data_toggle, buffer, USB_PACKET_ID_OUT, length);
}

int uhci_wait_transfer(UhciQh *qh) {
	Timer timer;
	timer_init(&timer);
	UhciTd *td = qh->first_td;

	while (td != NULL) {
		if (td->active == 0) {
			td = td->next;
			continue;
		}
		if (td->bitstuff_Error || td->crc_timeout_Error ||
			td->databuffer_Error || td->stalled || td->NAK_received) {

			uint32_t *raw = (uint32_t *)td;
			printk(
				"TD raw: w0=%08x w1=%08x w2=%08x w3=%08x\n", raw[0], raw[1],
				raw[2], raw[3]);
			printk(
				" decoded: pid=%02x dev=%u ep=%u toggle=%u maxlen=%u "
				"active=%u\n",
				td->packet_id, td->device_addr, td->endpoint, td->data_toggle,
				td->max_length, td->active);
			printk("[UHCI]td %#08x(dma %#08x) timeout.\n", td, td->dma_addr);
			return -1;
		}

		delay_ms(&timer, 1);
	}

	return 0;
}

UsbSetupStatus uhci_ctrl_transfer_in(
	UsbHcd *hcd, UsbDevice *device, void *buffer, uint32_t data_length,
	UsbControlRequest *usb_req) {
	Uhci		 *uhci = hcd->device->private_data;
	UhciPipeline *pipe = device->ep0->pipe;
	UhciQh		 *qh   = pipe->qh;
	qh->qe_link		   = UHCI_TERMINATE;
	qh->qh_link		   = UHCI_TERMINATE;
	if (uhci_setup_transcation(device, device->ep0, qh, usb_req, 8) == NULL) {
		uhci_free_all_td(uhci, pipe);
		return USB_SETUP_CRC_TIMEOUT_ERR;
	}
	if (uhci_in_transcation(device, device->ep0, qh, 0, buffer, data_length) == NULL) {
		uhci_free_all_td(uhci, pipe);
		return USB_SETUP_CRC_TIMEOUT_ERR;
	}
	UhciTd *last_td = uhci_out_transcation(device, device->ep0, qh, 1, NULL, 0);
	UhciTd *first_td = qh->first_td;
	if (last_td == NULL || first_td == NULL) {
		uhci_free_all_td(uhci, pipe);
		return USB_SETUP_CRC_TIMEOUT_ERR;
	}
	qh->qe_link		 = BIN_DIS(first_td->dma_addr, UHCI_QH_TD_SELECT);

	if (uhci_wait_transfer(qh) < 0) {
		qh->qh_link = UHCI_TERMINATE;
		uhci_free_all_td(uhci, pipe);
		return USB_SETUP_CRC_TIMEOUT_ERR;
	}
	qh->qe_link = UHCI_TERMINATE;

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
	uhci_free_all_td(uhci, pipe);

	return result;
}

UsbSetupStatus uhci_ctrl_transfer_out(
	UsbHcd *hcd, UsbDevice *device, void *buffer, uint32_t data_length,
	UsbControlRequest *usb_req) {
	Uhci		 *uhci = hcd->device->private_data;
	UhciPipeline *pipe = device->ep0->pipe;
	UhciQh		 *qh   = pipe->qh;
	qh->qe_link		   = UHCI_TERMINATE;
	qh->qh_link		   = UHCI_TERMINATE;
	if (uhci_setup_transcation(device, device->ep0, qh, usb_req, 8) == NULL) {
		uhci_free_all_td(uhci, pipe);
		return USB_SETUP_CRC_TIMEOUT_ERR;
	}
	if (data_length != 0) {
		if (uhci_out_transcation(device, device->ep0, qh, 0, buffer, data_length) == NULL) {
			uhci_free_all_td(uhci, pipe);
			return USB_SETUP_CRC_TIMEOUT_ERR;
		}
	}
	UhciTd *last_td	 = uhci_in_transcation(device, device->ep0, qh, 1, NULL, 0);
	UhciTd *first_td = qh->first_td;
	if (last_td == NULL || first_td == NULL) {
		uhci_free_all_td(uhci, pipe);
		return USB_SETUP_CRC_TIMEOUT_ERR;
	}
	qh->qe_link		 = BIN_DIS(first_td->dma_addr, UHCI_QH_TD_SELECT);

	if (uhci_wait_transfer(qh) < 0) {
		uhci_free_all_td(uhci, pipe);
		return USB_SETUP_CRC_TIMEOUT_ERR;
	}
	qh->qe_link = UHCI_TERMINATE;

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
	uhci_free_all_td(uhci, pipe);

	return result;
}

void uhci_add_interrupt_transfer(
	UsbHcd *hcd, UsbDevice *device, UsbEndpoint *ep, UsbRequestBlock *urb) {
	UhciPipeline *pipe = ep->pipe;
	UhciQh		 *qh   = pipe->qh;
	UhciTd		 *td, *_td;
	qh->qe_link = UHCI_TERMINATE;
	if (ep->desc->bEndpointAddress >> 7 == USB_EP_IN) {
		td = uhci_in_transcation(
			device, ep, qh, ep->data_toggle, urb->buffer,
			ep->desc->wMaxPacketSize);
		_td = uhci_in_transcation(
			device, ep, qh, ep->data_toggle ^ 1, urb->buffer,
			ep->desc->wMaxPacketSize);
	} else {
		td = uhci_out_transcation(
			device, ep, qh, ep->data_toggle, urb->buffer,
			ep->desc->wMaxPacketSize);
		_td = uhci_out_transcation(
			device, ep, qh, ep->data_toggle ^ 1, urb->buffer,
			ep->desc->wMaxPacketSize);
	}
	if (td == NULL || _td == NULL || qh->first_td == NULL) {
		uhci_free_all_td(hcd->device->private_data, pipe);
		return;
	}

	UhciTd *first_td		   = qh->first_td;
	qh->qe_link				   = BIN_DIS(first_td->dma_addr, UHCI_QH_TD_SELECT);
	td->urb					   = urb;
	_td->urb				   = urb;
	td->interrupt_on_complete  = 1;
	_td->interrupt_on_complete = 0;
	wmb();
}

void uhci_interrupt_transfer(UsbHcd *hcd, UsbEndpoint *ep) {
	UhciPipeline *pipe = ep->pipe;

	UhciTd *first_td = pipe->qh->first_td;
	UhciTd *next	 = first_td->next;

	ep->data_toggle ^= 1;

	first_td->data_toggle		= ep->data_toggle;
	first_td->stalled			= 0;
	first_td->crc_timeout_Error = 0;
	first_td->bitstuff_Error	= 0;
	first_td->NAK_received		= 0;
	first_td->databuffer_Error	= 0;

	first_td->active				= 1;
	first_td->actlen				= 0;
	first_td->interrupt_on_complete = 1;

	next->data_toggle = ep->data_toggle ^ 1;
	next->active	  = 1;

	pipe->qh->qe_link = BIN_DIS(first_td->dma_addr, UHCI_QH_TD_SELECT);
}
