#include "include/fat.h"
#include "include/attr.h"
#include "include/cluster.h"
#include "include/dir.h"
#include "include/entry.h"
#include <const.h>
#include <driver/storage/disk/disk.h>
#include <driver/storage/disk/mbr.h>
#include <driver/storage/disk/volume.h>
#include <driver/time_dm.h>
#include <fs/fs.h>
#include <kernel/block_cache.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <objects/transfer.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

FsResult fat_check(Partition *partition);
FsResult fat_mount(FileSystemInfo *fs_info, Object *root_object);
FsResult fat_open(
	FileSystemInfo *fs_info, void *parent, string_t name, Object **object);
FsResult fat_close(Object *object);
FsResult fat_seek(Object *object, size_t offset);
FsResult fat_read(Object *file, void *buf, size_t size);
FsResult fat_write(Object *file, void *buf, size_t size);

FsResult fat_opendir(
	FileSystemInfo *fs_info, void *parent, string_t name, Object **object);
FsResult fat_closedir(Object *object);
FsResult fat_create_file(Object *directory, string_t name, Object **object);
FsResult fat_delete_file(Object *directory, string_t name);
FsResult fat_mkdir(Object *parent_obj, string_t name, Object **object);
FsResult fat_rmdir(Object *parent_obj, string_t name);
FsResult fat_get_attr(Object *object, ObjectAttr *attr);
FsResult fat_set_attr(Object *object, ObjectAttr *attr);

FileSystemOps fat_ops = {
	.fs_check = fat_check,
	.fs_mount = fat_mount,
};

FsFileOps fat_file_ops = {
	.fs_open	 = fat_open,
	.fs_close	 = fat_close,
	.fs_seek	 = fat_seek,
	.fs_read	 = fat_read,
	.fs_write	 = fat_write,
	.fs_get_attr = fat_get_attr,
	.fs_set_attr = fat_set_attr,
};

FsDirectoryOps fat_dir_ops = {
	.fs_opendir		= fat_opendir,
	.fs_closedir	= fat_closedir,
	.fs_create_file = fat_create_file,
	.fs_delete_file = fat_delete_file,
	.fs_mkdir		= fat_mkdir,
	.fs_rmdir		= fat_rmdir,
	.fs_get_attr	= fat_get_attr,
	.fs_set_attr	= fat_set_attr,
};

FileSystem fat_fs = {
	.name = STRING_INIT("FAT"),
	.ops  = &fat_ops,
};

FatType fat_type_determine(FatInfo *fat_info);

FsResult fat_check(Partition *partition) {
	if (partition->type == PARTITION_TYPE_MBR) {
		if (partition->mbr->fs_type == 0x0b ||
			partition->mbr->fs_type == 0x0c) {
			return FS_OK;
		}
	}
	return FS_ERROR_UNSUPPORT;
}

FsResult fat_mount(FileSystemInfo *fs_info, Object *root_object) {
	FatBpb	  *bpb = (FatBpb *)fs_info->partition->superblock;
	FatFsInfo *fat_fs_info =
		(FatFsInfo *)(fs_info->partition->superblock + SECTOR_SIZE);
	FatInfo *fat_info	  = kmalloc(sizeof(FatInfo));
	fat_info->fs_info	  = fs_info;
	fs_info->private_data = fat_info;

	fat_info->partition = fs_info->partition;

	fat_info->bpb		  = bpb;
	fat_info->fat_fs_info = fat_fs_info;

	int root_dir_sectors =
		((bpb->BPB_RootEntCnt * 32) + (bpb->BPB_BytesPerSec - 1)) /
		bpb->BPB_BytesPerSec;

	fat_info->fat_sectors =
		(bpb->BPB_FATSz16 != 0) ? (bpb->BPB_FATSz16) : (bpb->fat32.BPB_FATSz32);

	fat_info->total_sectors =
		(bpb->BPB_TotSec16 != 0) ? (bpb->BPB_TotSec16) : (bpb->BPB_TotSec32);

	fat_info->data_sectors =
		fat_info->total_sectors -
		(bpb->BPB_RevdSecCnt + (bpb->BPB_NumFATs * fat_info->fat_sectors) +
		 root_dir_sectors);

	fat_info->type = fat_type_determine(fat_info);

	fat_info->fat_start = fs_info->partition->start_lba + bpb->BPB_RevdSecCnt;
	fat_info->data_start =
		fat_info->fat_start + (bpb->BPB_NumFATs * fat_info->fat_sectors);
	fat_info->sector_per_cluster = bpb->BPB_SecPerClus;
	fat_info->bytes_per_cluster =
		fat_info->sector_per_cluster * bpb->BPB_BytesPerSec;
	fat_info->entry_per_cluster = fat_info->bytes_per_cluster / 32;

	int cache_size = fat_info->bytes_per_cluster;

	if (fat_info->type == FAT_TYPE_FAT32) {
		fat_info->num_count = cache_size / 4;
	} else if (fat_info->type == FAT_TYPE_FAT16) {
		fat_info->num_count = cache_size / 2;
	}

	fat_info->fat_table_cache = block_cache_create(
		fat_info->bytes_per_cluster, 4, fat_table_read, fat_table_write,
		fat_info);

	const string_t root_name	  = STRING_INIT("");
	const ShortDir root_short_dir = {
		{"        ", "   "},
		 ATTR_DIRECTORY, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0
	 };
	const FatDirEntry root_entry = {
		root_name, root_short_dir, 0, 0, 0, 0, {0}, NULL, NULL,
	};
	fat_info->root_entry = root_entry;

	fat_info->root_entry.object		  = root_object;
	root_object->value.directory.data = &fat_info->root_entry;
	fat_info->root_entry.cluster_list =
		dyn_array_new(sizeof(ClusterSegment), 8);
	entry_cache_init(
		fat_info, &fat_info->root_entry, fat_info->bytes_per_cluster);
	get_cluster_segment(fat_info, &fat_info->root_entry);

	root_object->fs_info->private_data = fat_info;
	root_object->fs_info->file_ops	   = fat_file_ops;
	root_object->fs_info->dir_ops	   = fat_dir_ops;
	fat_info->storage_device =
		fs_info->partition->storage_object->value.device->dm_ext;

	return FS_OK;
}

FatType fat_type_determine(FatInfo *fat_info) {

	int count_of_cluster =
		fat_info->data_sectors / fat_info->bpb->BPB_SecPerClus;

	if (count_of_cluster < 4085) {
		return FAT_TYPE_FAT12;
	} else if (count_of_cluster < 65525) {
		return FAT_TYPE_FAT16;
	} else {
		return FAT_TYPE_FAT32;
	}
}

FsResult fat_open(
	FileSystemInfo *fs_info, void *parent, string_t name, Object **object) {
	FatInfo		*fat_info = fs_info->private_data;
	FatDirEntry *entry;

	FsResult result = search_dir(fat_info, parent, name, false, &entry, 0);
	*object			= entry->object;
	return result;
}

FsResult fat_opendir(
	FileSystemInfo *fs_info, void *parent, string_t name, Object **object) {
	FatInfo		*fat_info = fs_info->private_data;
	FatDirEntry *entry;

	FsResult result = search_dir(fat_info, parent, name, true, &entry, 0);
	*object			= entry->object;
	return result;
}

FsResult fat_close(Object *object) {
	FatDirEntry *entry = object->value.file.data;
	if (entry->cache != NULL) block_cache_destroy(entry->cache);
	dyn_array_delete(entry->cluster_list);
	string_del(&entry->name);
	kfree(entry);
	return FS_OK;
}

FsResult fat_closedir(Object *object) {
	return fat_close(object);
}

FsResult fat_seek(Object *object, size_t offset) {
	object->value.file.offset = offset;
	return FS_OK;
}

FsResult fat_transfer(
	BlockTransfer transfer, TransferDirection direction,
	IsTransferDone is_transfer_done, FatInfo *fat_info, Object *storage_object,
	Object *file, void *buf, size_t size) {
	FatDirEntry *entry = file->value.file.data;

	uint32_t cluster_index =
		file->value.file.offset / fat_info->bytes_per_cluster;

	CurrentCluster cur_cluster;
	fat_cluster_list_get(fat_info, entry, cluster_index, &cur_cluster);

	uint32_t offset = file->value.file.offset;
	uint32_t readed = 0;

	void *handle;
	while (size > 0) {
		uint32_t read_size = fat_info->bytes_per_cluster - offset;
		if (read_size > size) { read_size = size; }

		uint32_t sector = cluster2sector(fat_info, cur_cluster.cluster);

		transfer(
			storage_object, direction, buf + readed, sector,
			fat_info->sector_per_cluster);

		readed += read_size;
		size -= read_size;
		offset += read_size;

		if (offset == fat_info->bytes_per_cluster) {
			offset = 0;
			fat_cluster_list_get_next(fat_info, entry, &cur_cluster);
		}
	}
	bool done;
	do {
		TRANSFER_IN_IS_DONE(storage_object, &handle, &done);
	} while (!done);
	file->value.file.offset = offset;

	return FS_OK;
}

FsResult fat_read(Object *file, void *buf, size_t size) {
	FatInfo *fat_info		= file->fs_info->private_data;
	Object	*storage_object = fat_info->partition->storage_object;
	return fat_transfer(
		storage_object->in.block, TRANSFER_IN,
		storage_object->in.is_transfer_done, fat_info, storage_object, file,
		buf, size);
}

FsResult fat_write(Object *file, void *buf, size_t size) {
	FatInfo *fat_info		= file->fs_info->private_data;
	Object	*storage_object = fat_info->partition->storage_object;
	return fat_transfer(
		storage_object->out.block, TRANSFER_OUT,
		storage_object->out.is_transfer_done, fat_info, storage_object, file,
		buf, size);
}

FsResult fat_create_file(Object *parent_obj, string_t name, Object **object) {
	FatInfo		*fat_info = parent_obj->fs_info->private_data;
	FatDirEntry *entry, *parent = parent_obj->value.directory.data;

	FsResult result = search_dir(fat_info, parent, name, false, &entry, 0);
	if (result == FS_OK) { return FS_ERROR_ALREADY_EXISTS; }

	fat_create_entry(fat_info, parent, name, false, &entry);

	*object = entry->object;
	return FS_OK;
}

FsResult fat_delete_file(Object *parent_obj, string_t name) {
	FatInfo		*fat_info = parent_obj->fs_info->private_data;
	FatDirEntry *entry, *parent = parent_obj->value.directory.data;

	FsResult result = search_dir(fat_info, parent, name, false, &entry, 0);
	if (result != FS_OK) { return result; }

	fat_delete_entry(fat_info, parent, entry, name);
	return FS_OK;
}

FsResult fat_mkdir(Object *parent_obj, string_t name, Object **object) {
	FatInfo		*fat_info = parent_obj->fs_info->private_data;
	FatDirEntry *entry, *parent = parent_obj->value.directory.data;

	FsResult result = search_dir(fat_info, parent, name, true, &entry, 0);
	if (result == FS_OK) { return FS_ERROR_ALREADY_EXISTS; }

	fat_create_entry(fat_info, parent, name, true, &entry);

	*object = entry->object;
	return FS_OK;
}

FsResult fat_rmdir(Object *parent_obj, string_t name) {
	FatInfo		*fat_info = parent_obj->fs_info->private_data;
	FatDirEntry *entry, *parent = parent_obj->value.directory.data;

	FsResult result = search_dir(fat_info, parent, name, true, &entry, 0);
	if (result != FS_OK) { return result; }

	if (!fat_dir_is_empty(fat_info, entry)) return FS_ERROR_NOT_EMPTY;

	fat_delete_entry(fat_info, parent, entry, name);
	return FS_OK;
}

FsResult fat_get_attr(Object *object, ObjectAttr *attr) {
	FatDirEntry *entry = object->value.file.data;
	fat_attr_to_sys_attr(&entry->short_dir, attr);
	return FS_OK;
}

FsResult fat_set_attr(Object *object, ObjectAttr *attr) {
	FatInfo		*fat_info = object->fs_info->private_data;
	FatDirEntry *entry	  = object->value.file.data;
	FatDirEntry *parent	  = object->parent->value.directory.data;
	fat_attr_from_sys_attr(&entry->short_dir, attr);
	fat_entry_write(
		fat_info, parent, entry->shortname_cluster, entry->shortname_number,
		(uint8_t *)&entry->short_dir);
	return FS_OK;
}

void fat_initcall() {
	register_fs(&fat_fs);
}

fs_initcall(fat_initcall);
