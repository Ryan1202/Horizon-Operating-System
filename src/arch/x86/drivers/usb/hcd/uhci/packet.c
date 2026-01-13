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
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define DEFAULT_TD_COUNT 4

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
	UhciPipeline *pipe = kmalloc(sizeof(UhciPipeline));
	pipe->qh.qe_link   = UHCI_TERMINATE;
	pipe->qh.qh_link   = UHCI_TERMINATE;
	pipe->qh.endpoint  = endpoint;

	uint8_t ep_type = endpoint->desc->bmAttributes & 0x03;
	if (ep_type == USB_EP_INTERRUPT) {
		pipe->td_count = 2; // 1个正式TD和1个备用TD

		// 向下对齐后最大128
		int type =
			aligned_down_log2n(endpoint->desc->bInterval | 1 /* 保证最小为1 */);
		uhci_skel_add_qh(
			usb_device->hcd->device->private_data, &pipe->qh, type);
	} else if (ep_type == USB_EP_CONTROL) {
		pipe->td_count = 3; // SETUP, DATA, STATUS 3个阶段

		uhci_skel_add_qh(
			usb_device->hcd->device->private_data, &pipe->qh, ASYNC);
	} else {
		pipe->td_count = DEFAULT_TD_COUNT;
		uhci_skel_add_qh(
			usb_device->hcd->device->private_data, &pipe->qh, ASYNC);
	}
	pipe->td_used	   = 0;
	pipe->pre_alloc_td = kmalloc(sizeof(UhciTd) * pipe->td_count);
	list_init(&pipe->pipe_lh);

	for (int i = 0; i < pipe->td_count; i++) {
		pipe->pre_alloc_td[i].link = UHCI_TERMINATE;
	}
	return pipe;
}

UhciTd *uhci_alloc_td(UhciPipeline *pipe) {
	UhciTd *td;
	int		index = BIT_FFZ_R(pipe->td_used);
	if (index >= pipe->td_count) {
		td = kmalloc(sizeof(UhciTd));
	} else {
		td = &pipe->pre_alloc_td[index];
		pipe->td_used |= (1 << index);
	}
	return td;
}

void uhci_free_td(UhciPipeline *pipe, UhciTd *td) {
	int index = (td - pipe->pre_alloc_td);
	if (index >= 0 && index < pipe->td_count) {
		pipe->td_used &= ~(1 << index);
	} else {
		kfree(td);
	}
}

void uhci_free_all_td(UhciPipeline *pipe) {
	UhciTd *td, *next;
	list_for_each_owner_safe (td, next, &pipe->pipe_lh, list) {
		list_del(&td->list);
		uhci_free_td(pipe, td);
	}
}

UhciTd *uhci_send_token_packet(
	UsbDevice *device, UsbEndpoint *ep, UhciQh *qh, uint8_t data_toggle,
	void *buffer, uint8_t packet_id, int length) {
	UsbDevice	 *usb_device = device;
	UhciPipeline *pipe		 = ep->pipe;
	UhciTd		 *td		 = uhci_alloc_td(pipe);
	memset(td, 0, sizeof(UhciTd));

	td->packet_id		= packet_id;
	td->device_addr		= usb_device->address & 0x7f;
	td->endpoint		= ep->desc->bEndpointAddress & 0x0f;
	td->data_toggle		= data_toggle;
	td->max_length		= length > 0 ? (length - 1) : 0x7ff;
	td->lowspeed_device = (usb_device->speed == USB_SPEED_LOW) ? 1 : 0;

	td->active = 1;
	td->actlen = 0;

	td->error_count = 3;

	if (buffer != NULL) td->buf_addr_phy = vir2phy((uint32_t)buffer);
	else td->buf_addr_phy = 0;

	if (!list_empty(&pipe->pipe_lh)) {
		UhciTd *last_td = list_last_owner(&pipe->pipe_lh, UhciTd, list);
		list_add_tail(&td->list, &pipe->pipe_lh);
		last_td->link = BIN_DIS(vir2phy((uint32_t)td), UHCI_QH_TD_SELECT);
		last_td->link = BIN_EN(last_td->link, UHCI_VERTICAL_FIRST);
		last_td->next = td;
	} else {
		list_add_tail(&td->list, &pipe->pipe_lh);
		qh->first_td = td;
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
	while (td->active) {
		if (td->active == 0) { break; }

		delay_ms(&timer, 1);
	}
	if (!(td->crc_timeout_Error | td->bitstuff_Error | td->databuffer_Error |
		  td->stalled | td->NAK_received))
		return 0;

	// 发送错误
	uint32_t *raw = (uint32_t *)td; // TD 在内存首地址
	printk(
		"TD raw: w0=%08x w1=%08x w2=%08x w3=%08x\n", raw[0], raw[1], raw[2],
		raw[3]);
	printk(
		" decoded: pid=%02x dev=%u ep=%u toggle=%u maxlen=%u active=%u\n",
		td->packet_id, td->device_addr, td->endpoint, td->data_toggle,
		td->max_length, td->active);
	printk("[UHCI]td %#08x(phy %#08x) timeout.\n", td, vir2phy((uint32_t)td));
	return -1;
}

UsbSetupStatus uhci_ctrl_transfer_in(
	UsbHcd *hcd, UsbDevice *device, void *buffer, uint32_t data_length,
	UsbControlRequest *usb_req) {
	UhciPipeline *pipe = device->ep0->pipe;
	UhciQh		 *qh   = &pipe->qh;
	qh->qe_link		   = UHCI_TERMINATE;
	qh->qh_link		   = UHCI_TERMINATE;
	uhci_setup_transcation(device, device->ep0, qh, usb_req, 8);
	uhci_in_transcation(device, device->ep0, qh, 0, buffer, data_length);
	UhciTd *last_td = uhci_out_transcation(device, device->ep0, qh, 1, NULL, 0);
	UhciTd *first_td = qh->first_td;
	qh->qe_link		 = BIN_DIS(vir2phy((uint32_t)first_td), UHCI_QH_TD_SELECT);

	if (uhci_wait_transfer(qh) < 0) {
		// 超时
		uhci_free_all_td(pipe);
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
	uhci_free_all_td(pipe);

	return result;
}

UsbSetupStatus uhci_ctrl_transfer_out(
	UsbHcd *hcd, UsbDevice *device, void *buffer, uint32_t data_length,
	UsbControlRequest *usb_req) {
	UhciPipeline *pipe = device->ep0->pipe;
	UhciQh		 *qh   = &pipe->qh;
	qh->qe_link		   = UHCI_TERMINATE;
	qh->qh_link		   = UHCI_TERMINATE;
	uhci_setup_transcation(device, device->ep0, qh, usb_req, 8);
	if (data_length != 0) {
		uhci_out_transcation(device, device->ep0, qh, 0, buffer, data_length);
	}
	UhciTd *last_td	 = uhci_in_transcation(device, device->ep0, qh, 1, NULL, 0);
	UhciTd *first_td = qh->first_td;
	qh->qe_link		 = BIN_DIS(vir2phy((uint32_t)first_td), UHCI_QH_TD_SELECT);

	if (uhci_wait_transfer(qh) < 0) {
		// 超时
		uhci_free_all_td(pipe);
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
	uhci_free_all_td(pipe);

	return result;
}

void uhci_add_interrupt_transfer(
	UsbHcd *hcd, UsbDevice *device, UsbEndpoint *ep, UsbRequestBlock *urb) {
	UhciPipeline *pipe = ep->pipe;
	UhciQh		 *qh   = &pipe->qh;
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

	UhciTd *first_td = qh->first_td;
	qh->qe_link		 = BIN_DIS(vir2phy((uint32_t)first_td), UHCI_QH_TD_SELECT);
	td->urb			 = urb;
	_td->urb		 = urb;
	td->interrupt_on_complete  = 1; // 传输完成后中断通知
	_td->interrupt_on_complete = 0;
	wmb();
}

void uhci_interrupt_transfer(UsbHcd *hcd, UsbEndpoint *ep) {
	UhciPipeline *pipe = ep->pipe;

	UhciTd *first_td = pipe->qh.first_td;
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

	// 2. 挂回 QH
	pipe->qh.qe_link = BIN_DIS(vir2phy((uint32_t)first_td), UHCI_QH_TD_SELECT);
}
