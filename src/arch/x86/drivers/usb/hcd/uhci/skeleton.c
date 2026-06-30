#include <drivers/usb/uhci.h>
#include <kernel/dma.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <string.h>

DriverResult uhci_skel_init(Uhci *uhci) {
	int		i;
	UhciQh *qhs;

	// Skel 含 11 个 QH，用 dma_alloc_coherent
	PhysicalDevice *phy = uhci->hcd->device->physical_device;
	uhci->skel_handle = dma_alloc_coherent(phy->dma, sizeof(UhciSkel));
	if (uhci->skel_handle == NULL) {
		printk("UHCI: failed to allocate skeleton QHs\n");
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}
	uhci->skel = dma_handle_cpu_addr(uhci->skel_handle);
	if (uhci->skel == NULL) {
		printk("UHCI: failed to get skeleton QH CPU address\n");
		dma_free_coherent(uhci->skel_handle);
		uhci->skel_handle = NULL;
		return DRIVER_ERROR_NULL_POINTER;
	}
	qhs		   = uhci->skel->qh;

	// 初始化每个 QH 的 dma_addr
	for (i = 0; i < UHCI_SKEL_QH_COUNT; i++) {
		qhs[i].dma_addr = (uint32_t)dma_handle_dma_addr(uhci->skel_handle) + offsetof(UhciSkel, qh[i]);
	}

	qhs[TIME_1MS].next	  = &qhs[ASYNC];
	qhs[TIME_1MS].qh_link = BIN_EN(qhs[ASYNC].dma_addr, UHCI_QH_TD_SELECT);
	qhs[TIME_1MS].qe_link = UHCI_TERMINATE;
	for (i = 1; i < 8; i++) {
		qhs[i].next	   = &qhs[TIME_1MS];
		qhs[i].qh_link = BIN_EN(qhs[TIME_1MS].dma_addr, UHCI_QH_TD_SELECT);
		qhs[i].qe_link = UHCI_TERMINATE;
	}
	qhs[ASYNC].next	   = &qhs[TERM];
	qhs[ASYNC].qh_link = BIN_EN(qhs[TERM].dma_addr, UHCI_QH_TD_SELECT);
	qhs[ASYNC].qe_link = UHCI_TERMINATE;

	// Term TD 从 pool 分配
	size_t	term_dma;
	UhciTd *term_td;
	term_td = dma_pool_alloc(uhci->td_qh_pool, &term_dma);
	if (term_td == NULL) {
		printk("UHCI: failed to allocate terminal TD\n");
		dma_free_coherent(uhci->skel_handle);
		uhci->skel_handle = NULL;
		uhci->skel		  = NULL;
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}
	memset(term_td, 0, sizeof(UhciTd));

	term_td->dma_addr	 = term_dma;
	term_td->max_length	 = 0x7ff;
	term_td->device_addr = 0x7f;
	term_td->packet_id	 = USB_PACKET_ID_IN;
	term_td->link		 = UHCI_TERMINATE;

	qhs[TERM].qh_link  = UHCI_TERMINATE;
	qhs[TERM].qe_link  = term_td->dma_addr;
	qhs[TERM].first_td = term_td;

	i = 0;
	while (i < 1024) {
		int irq = 1 + BIT_FFS_R(i + FRAMELIST_SIZE);
		if (irq > 7) irq = 0;

		uhci->fl.frames_vir[i] = BIN_EN(qhs[irq].dma_addr, UHCI_QH_TD_SELECT);
		i++;
	}
	return DRIVER_OK;
}

void uhci_skel_add_qh(Uhci *uhci, UhciQh *qh, UhciSkelType type) {
	UhciQh *first_qh = uhci->skel->qh[type].first_qh;
	if (qh->enqueued) return;
	if (first_qh != NULL) {
		qh->qh_link					  = uhci->skel->qh[type].qe_link;
		qh->next					  = first_qh;
		uhci->skel->qh[type].first_qh = qh;
		uhci->skel->qh[type].qe_link  = BIN_EN(qh->dma_addr, UHCI_QH_TD_SELECT);
	} else {
		qh->qh_link					 = UHCI_TERMINATE;
		uhci->skel->qh[type].qe_link = BIN_EN(qh->dma_addr, UHCI_QH_TD_SELECT);
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
			if (prev != NULL) {
				prev->next	  = qh->next;
				prev->qh_link = qh->qh_link;
			} else if (qh->next != NULL) {
				uhci->skel->qh[type].first_qh = qh->next;
				uhci->skel->qh[type].qe_link =
					BIN_EN(qh->next->dma_addr, UHCI_QH_TD_SELECT);
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
	if (qh->endpoint != NULL && qh->first_td != NULL)
		uhci_free_all_td(uhci, qh->endpoint->pipe);
	qh->first_td = NULL;
	qh->enqueued = 0;
}
