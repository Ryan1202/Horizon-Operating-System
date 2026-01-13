#ifndef _ATA_H
#define _ATA_H

typedef enum AtaCmdIndex {
	ATA_CMDSET_READ,
	ATA_CMDSET_READ_EXT,
	ATA_CMDSET_WRITE,
	ATA_CMDSET_WRITE_EXT,
	ATA_CMDSET_MAX,
} AtaCmdIndex;

#endif