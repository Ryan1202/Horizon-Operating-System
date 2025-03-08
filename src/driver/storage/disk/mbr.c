#include "kernel/device.h"
#include "kernel/memory.h"
#include <driver/storage/disk/disk.h>
#include <driver/storage/disk/mbr.h>
#include <driver/storage/disk/volume.h>
#include <driver/storage/storage_dm.h>
#include <objects/object.h>
#include <objects/transfer.h>
#include <string.h>

#define MBR_PARTITION_ENTRY_SIZE   16
#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_PARTITION_COUNT		   4

bool disk_is_mbr(StorageDevice *storage_device) {
	return storage_device->superblock[510] == 0x55 &&
		   storage_device->superblock[511] == 0xAA;
}

void parse_mbr_partition_table(StorageDevice *storage_device) {
	int				   partition_count = 0;
	MBRPartitionEntry *partition_table =
		(MBRPartitionEntry *)&storage_device
			->superblock[MBR_PARTITION_TABLE_OFFSET];

	for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
		if (partition_table[i].fs_type != 0) {
			string_t name;
			string_new_with_number(&name, "Partition", 9, partition_count);
			ObjectAttr attr = device_object_attr;
			attr.type		= OBJECT_TYPE_PARTITION;
			Object *object	= create_object(storage_device->object, name, attr);

			Partition *partition	  = kmalloc(sizeof(Partition));
			partition->storage_object = storage_device->device->object;
			partition->type			  = PARTITION_TYPE_MBR;
			partition->start_lba	  = partition_table[i].start_lba;
			partition->size_lba		  = partition_table[i].size;
			partition->mbr			  = &partition_table[i];
			partition->index		  = partition_count;
			partition->object		  = object;
			object->value.partition	  = partition;

			object->in.type				 = TRANSFER_TYPE_BLOCK;
			object->in.block			 = disk_transfer_in;
			object->in.block_async		 = disk_transfer_in_async;
			object->in.is_transfer_done	 = disk_is_transfer_in_done;
			object->out.type			 = TRANSFER_TYPE_BLOCK;
			object->out.block			 = disk_transfer_out;
			object->out.block_async		 = disk_transfer_out_async;
			object->out.is_transfer_done = disk_is_transfer_out_done;

			probe_volume(partition);

			partition_count++;
		}
	}
}