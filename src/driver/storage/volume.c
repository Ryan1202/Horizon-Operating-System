#include "objects/object.h"
#include "string.h"
#include <const.h>
#include <driver/storage/disk/disk.h>
#include <driver/storage/disk/volume.h>
#include <driver/storage/storage_dm.h>
#include <driver/storage/storage_io.h>
#include <fs/fs.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <objects/mount.h>

void probe_volume(Partition *partition) {
	Object		  *object = partition->storage_object;
	StorageDevice *storage_device =
		partition->storage_object->value.device->dm_ext;
	string_t prefix		  = storage_device->name;
	partition->superblock = kmalloc(2 * SECTOR_SIZE);

	storage_transfer(
		object, NULL, TRANSFER_IN, partition->superblock, partition->start_lba,
		2);

	if (partition->type == PARTITION_TYPE_MBR) {
		FileSystem *fs;
		list_for_each_owner (fs, &fs_list_head, list) {
			if (fs->ops->fs_check(partition) == FS_OK) {
				FileSystemInfo *fs_info = kmalloc(sizeof(FileSystemInfo));
				fs_info->partition		= partition;
				fs_info->ops			= fs->ops;
				string_t name;
				string_new_with_string_number(
					&name, prefix.text, prefix.length - 1, "Volume", 6,
					partition->index);
				ObjectAttr attr = device_object_attr;
				Object	  *root_object =
					create_object_directory(&volumes_object, name, attr);
				partition->object->fs_info = fs_info;

				object_mount(partition->object, root_object);
				break;
			}
		}
	}
}