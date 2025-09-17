#include <drivers/usb/uhci.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <stdint.h>

void uhci_skel_init(Uhci *uhci) {
	int		i;
	UhciQh *qhs;

	uhci->skel = kmalloc(sizeof(UhciSkel));
	qhs		   = uhci->skel->qh;

	memset32(uhci->fl.frames_vir, 1, 1024);

	qhs[TIME_1MS].qh_addr_phy = vir2phy((uint32_t)&qhs[TIME_1MS]);
	qhs[TIME_1MS].next_ptr	  = (uint32_t)&qhs[LOW_SPEED];
	qhs[TIME_1MS].qh_link =
		BIN_EN(vir2phy(qhs[TIME_1MS].next_ptr), UHCI_QH_TD_SELECT);
	qhs[TIME_1MS].qe_link = UHCI_TERMINATE;
	for (i = 1; i < 8; i++) {
		qhs[i].qh_addr_phy = vir2phy((uint32_t)&qhs[i]);
		qhs[i].next_ptr	   = (uint32_t)&qhs[TIME_1MS];
		qhs[i].qh_link = BIN_EN(qhs[TIME_1MS].qh_addr_phy, UHCI_QH_TD_SELECT);
		qhs[i].qe_link = UHCI_TERMINATE;
	}
	qhs[LOW_SPEED].qh_addr_phy = vir2phy((uint32_t)&qhs[LOW_SPEED]);
	qhs[LOW_SPEED].next_ptr	   = (uint32_t)&qhs[TERM];
	qhs[LOW_SPEED].qh_link =
		BIN_EN(vir2phy(qhs[LOW_SPEED].next_ptr), UHCI_QH_TD_SELECT);
	qhs[LOW_SPEED].qe_link = UHCI_TERMINATE;

	// 构建结束的QH和TD

	UhciTd *term_td = (UhciTd *)kmalloc(sizeof(UhciTd));
	memset(term_td, 0, sizeof(UhciTd));

	term_td->max_length	 = 0x7ff;
	term_td->device_addr = 0x7f;
	term_td->packet_id	 = USB_PACKET_ID_IN;
	term_td->td_addr_phy = vir2phy((uint32_t)term_td);
	term_td->link		 = term_td->td_addr_phy;

	qhs[TERM].qh_link = UHCI_TERMINATE;
	qhs[TERM].qe_link = term_td->td_addr_phy;

	i = 0;
	while (i < 1024) {
		int irq = 1 + BIT_FFS_R(i + FRAMELIST_SIZE);
		if (irq > 7) irq = 7;

		uhci->fl.frames_vir[i] =
			BIN_EN(vir2phy((uint32_t)&qhs[irq]), UHCI_QH_TD_SELECT);
		i++;
	}
}

void uhci_skel_add_qh(Uhci *uhci, UhciQh *qh, UhciSkelType type) {
	UhciQh *last_qh = ((UhciQh *)uhci->skel->qh[type].last_ptr);
	if (last_qh != NULL) {
		qh->qh_link		  = last_qh->qh_link;
		last_qh->next_ptr = (uint32_t)qh;
		qh->prev_ptr	  = (uint32_t)last_qh;
		last_qh->qh_link  = BIN_EN(qh->qh_addr_phy, UHCI_QH_TD_SELECT);
	} else {
		qh->qh_link = UHCI_TERMINATE;
		uhci->skel->qh[type].qe_link =
			BIN_EN(qh->qh_addr_phy, UHCI_QH_TD_SELECT);
	}
	uhci->skel->qh[type].last_ptr = (uint32_t)qh;
}

void uhci_skel_del_qh(Uhci *uhci, UhciQh *qh, UhciSkelType type) {
	if (qh->prev_ptr != 0) {
		if (qh->next_ptr != 0) {
			UhciQh *next_qh	  = (UhciQh *)qh->next_ptr;
			UhciQh *prev_qh	  = (UhciQh *)qh->prev_ptr;
			prev_qh->next_ptr = qh->next_ptr;
			next_qh->prev_ptr = qh->prev_ptr;
		}
	} else {
		if (qh->next_ptr != 0) {
			UhciQh *next_qh				 = (UhciQh *)qh->next_ptr;
			next_qh->prev_ptr			 = qh->prev_ptr;
			uhci->skel->qh[type].qe_link = qh->qh_link;
		} else {
			uhci->skel->qh[type].qe_link  = UHCI_TERMINATE;
			uhci->skel->qh[type].last_ptr = 0;
		}
	}
	qh->next_ptr = 0;
	qh->prev_ptr = 0;
	qh->last_ptr = 0;
}