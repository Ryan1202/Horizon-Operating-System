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

usb_hcd_interface_t uhci_interface = {
	.control_transaction_in	 = uhci_control_transaction_in,
	.control_transaction_out = uhci_control_transaction_out,
};

void uhci_send_token_packet(
	usb_transfer_t *transfer, uhci_qh_t *qh, uint8_t data_toggle, void *buffer,
	uint8_t packet_id, int length) {
	usb_device_t *usb_device = transfer->device;
	uhci_td_t	 *td		 = kmalloc(sizeof(uhci_td_t));

	td->packet_id	= packet_id;
	td->device_addr = usb_device->address & 0x7f;
	td->endpoint	= transfer->ep->endpoint & 0x0f;
	td->data_toggle = data_toggle;
	td->max_length	= length - 1;

	td->active = 1;
	td->actlen = 0;

	td->td_addr_phy = vir2phy((uint32_t)td);

	if (buffer != NULL) { td->buf_addr_phy = vir2phy((uint32_t)buffer); }

	td->prev_ptr	   = qh->last_ptr;
	uhci_td_t *last_td = (uhci_td_t *)qh->last_ptr;
	if (last_td != NULL) {
		last_td->link = BIN_EN(
			BIN_DIS(td->td_addr_phy, UHCI_QH_TD_SELECT), UHCI_VERTICAL_FIRST);
	} else {
		qh->qe_link = BIN_DIS(td->td_addr_phy, UHCI_QH_TD_SELECT);
	}
	td->link = UHCI_TERMINATE;

	qh->last_ptr = (uint32_t)td;
}

int uhci_setup_packet(
	usb_device_t *device, usb_transfer_t *transfer, uhci_qh_t *qh, void *buffer,
	uint32_t length) {
	uhci_send_token_packet(
		transfer, qh, 0, buffer, USB_PACKET_ID_SETUP, length);
	return 0;
}

int uhci_in_packet(
	usb_device_t *device, usb_transfer_t *transfer, uhci_qh_t *qh, void *buffer,
	uint32_t length) {
	uhci_send_token_packet(transfer, qh, 1, buffer, USB_PACKET_ID_IN, length);
	return 0;
}

int uhci_out_packet(
	usb_device_t *device, usb_transfer_t *transfer, uhci_qh_t *qh, void *buffer,
	uint32_t length) {
	uhci_send_token_packet(transfer, qh, 1, buffer, USB_PACKET_ID_OUT, length);
	return 0;
}

int uhci_wait_transfer(uhci_qh_t *qh) {
	int		   timeout = 150;
	uhci_td_t *td	   = (uhci_td_t *)qh->last_ptr;
	while (timeout > 0) {
		if (td->active == 0) { return 1; }

		// TODO: Delay
		// delay(10 / 10);
		timeout--;
	}
	printk("[UHCI]td %#08x(phy %#08x) timeout.", td, td->td_addr_phy);
	return -1;
}

usb_setup_status_t uhci_control_transaction_in(
	usb_hcd_t *hcd, usb_device_t *device, usb_transfer_t *transfer,
	void *buffer, uint32_t data_length, usb_request_t *usb_req) {
	uhci_qh_t *qh	= kmalloc(sizeof(uhci_qh_t));
	qh->qh_addr_phy = vir2phy((uint32_t)qh);
	qh->qe_link		= UHCI_TERMINATE;
	qh->qh_link		= UHCI_TERMINATE;
	uhci_setup_packet(device, transfer, qh, usb_req, 8);
	uhci_in_packet(device, transfer, qh, buffer, data_length);
	uhci_out_packet(device, transfer, qh, NULL, 0x800);

	uhci_skel_add_qh(hcd->device->device_extension, qh, LOW_SPEED);
	uhci_wait_transfer(qh);

	uhci_td_t		  *last_td = (uhci_td_t *)qh->last_ptr;
	usb_setup_status_t result;

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
	uhci_skel_del_qh(hcd->device->device_extension, qh, LOW_SPEED);

	return result;
}

usb_setup_status_t uhci_control_transaction_out(
	usb_hcd_t *hcd, usb_device_t *device, usb_transfer_t *transfer,
	void *buffer, uint32_t data_length, usb_request_t *usb_req) {
	uhci_qh_t *qh	= kmalloc(sizeof(uhci_qh_t));
	qh->qh_addr_phy = vir2phy((uint32_t)qh);
	qh->qe_link		= UHCI_TERMINATE;
	qh->qh_link		= UHCI_TERMINATE;
	uhci_setup_packet(device, transfer, qh, usb_req, 8);
	if (data_length != 0) {
		uhci_out_packet(device, transfer, qh, buffer, data_length);
	}
	uhci_in_packet(device, transfer, qh, NULL, 0x800);

	uhci_skel_add_qh(hcd->device->device_extension, qh, LOW_SPEED);
	uhci_wait_transfer(qh);

	uhci_td_t		  *last_td = (uhci_td_t *)qh->last_ptr;
	usb_setup_status_t result;

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
	uhci_skel_del_qh(hcd->device->device_extension, qh, LOW_SPEED);

	return result;
}
