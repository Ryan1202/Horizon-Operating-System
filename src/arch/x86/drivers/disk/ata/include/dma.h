#ifndef _ATA_DMA_H
#define _ATA_DMA_H

#include "ide.h"
#include "kernel/dma.h"
#include <stdint.h>

typedef struct PhysicalRegionDescriptor {
	uint32_t base_addr;
	uint16_t count;
	uint16_t sign;
} __attribute__((packed)) PhysicalRegionDescriptor;

typedef struct AtaDma {
	DmaDevice  *dma_device;
	DmaHandle  *prdt_handle;
	DmaScatterList *sg_list;
	int sg_nents;
	DmaDirection direction;

	int prdt_status;
	int max_segment_size;

	struct PhysicalRegionDescriptor *prds;

	uint32_t prdt_dma_addr;
} AtaDma;

void		 ata_bmdma_set_prdt(IdeDevice *device, AtaDma *ata_dma, int rw);
DriverResult ata_bmdma_map_sg(
	AtaDma *ata_dma, void *ptr, uint32_t size, int rw);
void ata_bmdma_unmap_sg(AtaDma *ata_dma, void *ptr, uint32_t size);
void ata_bmdma_cancel_sg(AtaDma *ata_dma);

#endif
