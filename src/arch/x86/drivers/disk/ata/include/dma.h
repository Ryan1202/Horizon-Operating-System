#ifndef _ATA_DMA_H
#define _ATA_DMA_H

#include "ide.h"
#include <stdint.h>

typedef struct PhysicalRegionDescriptor {
	uint32_t base_addr;
	uint16_t count;
	uint16_t sign;
} __attribute__((packed)) PhysicalRegionDescriptor;

typedef struct AtaDma {
	list_t segment_lh;

	int prdt_status;
	int max_segment_size;

	struct PhysicalRegionDescriptor *prds;

	uint32_t prdt_phy_addr;
} AtaDma;

void		 ata_bmdma_set_prdt(IdeDevice *device, AtaDma *ata_dma, int rw);
DriverResult ata_bmdma_map_buffer(AtaDma *ata_dma, void *ptr, uint32_t size);
void		 ata_bmdma_unmap_buffer(AtaDma *ata_dma, void *ptr, uint32_t size);

#endif