#include <drivers/usb/uhci.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <stdint.h>
#include <string.h>

void uhci_skel_init(Uhci *uhci) {
	int		i;
	UhciQh *qhs;

	uhci->skel = kmalloc(sizeof(UhciSkel));
	qhs		   = uhci->skel->qh;

	memset32(uhci->fl.frames_vir, 1, 1024);

	uint32_t phy	   = vir2phy((uint32_t)&qhs[TIME_1MS]);
	qhs[TIME_1MS].next = &qhs[ASYNC];
	qhs[TIME_1MS].qh_link =
		BIN_EN(vir2phy((uint32_t)qhs[TIME_1MS].next), UHCI_QH_TD_SELECT);
	qhs[TIME_1MS].qe_link = UHCI_TERMINATE;
	for (i = 1; i < 8; i++) {
		qhs[i].next	   = &qhs[TIME_1MS];
		qhs[i].qh_link = BIN_EN(phy, UHCI_QH_TD_SELECT);
		qhs[i].qe_link = UHCI_TERMINATE;
	}
	qhs[ASYNC].next = &qhs[TERM];
	qhs[ASYNC].qh_link =
		BIN_EN(vir2phy((uint32_t)qhs[ASYNC].next), UHCI_QH_TD_SELECT);
	qhs[ASYNC].qe_link = UHCI_TERMINATE;

	// 构建结束的QH和TD

	UhciTd *term_td = (UhciTd *)kmalloc(sizeof(UhciTd));
	memset(term_td, 0, sizeof(UhciTd));

	term_td->max_length	 = 0x7ff;
	term_td->device_addr = 0x7f;
	term_td->packet_id	 = USB_PACKET_ID_IN;
	term_td->link		 = vir2phy((uint32_t)term_td);

	qhs[TERM].qh_link  = UHCI_TERMINATE;
	qhs[TERM].qe_link  = term_td->link;
	qhs[TERM].first_td = term_td;

	i = 0;
	while (i < 1024) {
		int irq = 1 + BIT_FFS_R(i + FRAMELIST_SIZE);
		if (irq > 7) irq = 0;

		uhci->fl.frames_vir[i] =
			BIN_EN(vir2phy((uint32_t)&qhs[irq]), UHCI_QH_TD_SELECT);
		i++;
	}
}

void uhci_skel_add_qh(Uhci *uhci, UhciQh *qh, UhciSkelType type) {
	UhciQh *first_qh = uhci->skel->qh[type].first_qh;
	if (qh->enqueued) return; // 已经被添加
	if (first_qh != NULL) {
		qh->qh_link					  = uhci->skel->qh[type].qe_link;
		qh->next					  = first_qh;
		uhci->skel->qh[type].first_qh = qh;
		uhci->skel->qh[type].qe_link =
			BIN_EN(vir2phy((uint32_t)qh), UHCI_QH_TD_SELECT);
	} else {
		qh->qh_link = UHCI_TERMINATE;
		uhci->skel->qh[type].qe_link =
			BIN_EN(vir2phy((uint32_t)qh), UHCI_QH_TD_SELECT);
	}
	qh->enqueued				  = 1;
	uhci->skel->qh[type].first_qh = qh;
}

void uhci_skel_del_qh(Uhci *uhci, UhciQh *qh, UhciSkelType type) {
	UhciQh *_qh, *prev = NULL;
	UhciQh *ptr = uhci->skel->qh[type].first_qh;
	while (ptr != NULL) {
		_qh = ptr;
		if (_qh == qh) {
			// Found the QH to delete
			if (prev != NULL) {
				prev->next	  = qh->next;
				prev->qh_link = qh->qh_link;
			} else if (qh->next != NULL) {
				uhci->skel->qh[type].first_qh = qh->next;
				uhci->skel->qh[type].qe_link =
					BIN_EN(vir2phy((uint32_t)qh->next), UHCI_QH_TD_SELECT);
			} else {
				uhci->skel->qh[type].qe_link = UHCI_TERMINATE;
			}
			_qh->qh_link = UHCI_TERMINATE;
			_qh->next	 = 0;
			break;
		}
		prev = _qh;
		ptr	 = _qh->next;
	}
	qh->qe_link = UHCI_TERMINATE;
	if (qh->first_td != NULL) uhci_free_all_td((UhciPipeline *)qh);
	qh->first_td = NULL;
	qh->enqueued = 0;
}