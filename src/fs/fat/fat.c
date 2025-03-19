#include "include/fat.h"
#include "include/attr.h"
#include "include/cluster.h"
#include "include/dir.h"
#include "include/entry.h"
#include "kernel/console.h"
#include "math.h"
#include "objects/handle.h"
#include <const.h>
#include <driver/storage/disk/disk.h>
#include <driver/storage/disk/mbr.h>
#include <driver/storage/disk/volume.h>
#include <driver/time_dm.h>
#include <fs/fs.h>
#include <kernel/block_cache.h>
#include <kernel/initcall.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <objects/transfer.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

FsResult fat_check(Partition *partition);
FsResult fat_mount(FileSystemInfo *fs_info, Object *root_object);

FsResult fat_lookup(
	FileSystemInfo *fs_info, Object *parent_obj, string_t *name,
	ObjectAttr **attr);
FsResult fat_open(
	FileSystemInfo *fs_info, Object *parent_obj, ObjectAttr *attr,
	string_t *name, Object **object);
FsResult fat_close(Object *object);
FsResult fat_seek(Object *file, size_t offset);
FsResult fat_read(Object *file, ObjectHandle *handle, void *buf, size_t size);
FsResult fat_write(
	Object *object, ObjectHandle *handle, void *buf, size_t size);
FsResult fat_create_handle(ObjectHandle *handle);
FsResult fat_delete_handle(ObjectHandle *handle);

FsResult fat_opendir(
	FileSystemInfo *fs_info, Object *parent_obj, void **iterator);
FsResult fat_readdir(
	FileSystemInfo *fs_info, ObjectIterator *iterator, Object **object);
FsResult fat_closedir(ObjectIterator *iterator);
FsResult fat_create_file(Object *parent_obj, string_t *name, Object **object);
FsResult fat_delete_file(Object *parent_obj, ObjectAttr *attr, string_t *name);
FsResult fat_mkdir(Object *parent_obj, string_t *name, Object **object);
FsResult fat_rmdir(Object *parent_obj, ObjectAttr *attr, string_t *name);
FsResult fat_get_attr(Object *object, ObjectAttr *attr);
FsResult fat_set_attr(Object *object, ObjectAttr *attr);

FileSystemOps fat_ops = {
	.fs_check = fat_check,
	.fs_mount = fat_mount,
};

FsFileOps fat_file_ops = {
	.fs_open		  = fat_open,
	.fs_close		  = fat_close,
	.fs_seek		  = fat_seek,
	.fs_read		  = fat_read,
	.fs_write		  = fat_write,
	.fs_get_attr	  = fat_get_attr,
	.fs_set_attr	  = fat_set_attr,
	.fs_create_handle = fat_create_handle,
	.fs_delete_handle = fat_delete_handle,
};

FsDirectoryOps fat_dir_ops = {
	.fs_lookup		= fat_lookup,
	.fs_opendir		= fat_opendir,
	.fs_readdir		= fat_readdir,
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

FatPrivOps fat32_priv_ops = {
	.fat_dir_lookup		= fat32_dir_lookup,
	.fat_read_dir_entry = fat32_read_dir_entry,
};
FatPrivOps fat_priv_ops = {
	.fat_dir_lookup		= fat_dir_lookup,
	.fat_read_dir_entry = fat_read_dir_entry,
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
		fat_info->max_cluster = 0xFF8;
		fat_info->ops		  = &fat_priv_ops;
		return FAT_TYPE_FAT12;
	} else if (count_of_cluster < 65525) {
		fat_info->max_cluster = 0xFFF8;
		fat_info->ops		  = &fat_priv_ops;
		return FAT_TYPE_FAT16;
	} else {
		fat_info->max_cluster  = 0x0FFFFFF8;
		fat_info->ops		   = &fat32_priv_ops;
		fat_info->use_longname = true;
		return FAT_TYPE_FAT32;
	}
}

FsResult fat_lookup(
	FileSystemInfo *fs_info, Object *parent_obj, string_t *name,
	ObjectAttr **attr) {
	FatInfo		*fat_info	  = fs_info->private_data;
	FatDirEntry *parent_entry = parent_obj->value.directory.data;

	FatLocation location;
	ShortDir	short_dir;
	FS_RESULT_PASS(fat_info->ops->fat_dir_lookup(
		fat_info, parent_entry, *name, &location, &short_dir));
	ObjectAttr *tmp_attr = kmalloc(sizeof(ObjectAttr));
	fat_attr_to_sys_attr(&short_dir, tmp_attr, &location);

	*attr = tmp_attr;

	return FS_OK;
}

FsResult fat_open(
	FileSystemInfo *fs_info, Object *parent_obj, ObjectAttr *attr,
	string_t *name, Object **object) {
	FatInfo		*fat_info = fs_info->private_data;
	FatDirEntry *entry, *parent_entry = parent_obj->value.directory.data;
	FatLocation *location = attr->fs_location;
	ShortDir	 short_dir;

	fat_entry_read(
		fat_info, parent_entry, location->shortname_cluster,
		location->shortname_offset, (uint8_t *)&short_dir);

	entry = generate_dir_entry(
		fat_info, parent_entry, &short_dir, *name, location, attr);
	*object = entry->object;
	return FS_OK;
}

FsResult fat_close(Object *object) {
	FatDirEntry *entry = object->value.file.data;
	if (entry->cache != NULL) block_cache_destroy(entry->cache);
	dyn_array_delete(entry->cluster_list);
	string_del(&entry->name);
	kfree(entry);
	return FS_OK;
}

FsResult fat_opendir(
	FileSystemInfo *fs_info, Object *parent_obj, void **iterator) {
	FatInfo		*fat_info = fs_info->private_data;
	FatDirEntry *parent	  = parent_obj->value.directory.data;

	*iterator = kmalloc(sizeof(FatDirIterator));
	if (fat_info->type == FAT_TYPE_FAT32) {
		fat_dir_iterator_init(*iterator, fat_info, parent);
	} else {
		fat_dir_iterator_init(*iterator, fat_info, parent);
	}
	return FS_OK;
}

FsResult fat_readdir(
	FileSystemInfo *fs_info, ObjectIterator *iterator, Object **object) {
	FatInfo		   *fat_info = fs_info->private_data;
	FatDirEntry	   *entry;
	string_t		name;
	ShortDir		short_dir;
	FatDirIterator *iter	   = iterator->fs_iterator;
	Object		   *parent_obj = iterator->parent_object;

	FS_RESULT_PASS(fat_info->ops->fat_read_dir_entry(iter, &short_dir));

	Object *cur;
	list_for_each_owner (cur, &parent_obj->value.directory.children, list) {
		FatLocation *location = cur->attr->fs_location;
		if (location->shortname_cluster == iter->last_cluster &&
			location->shortname_offset == iter->last_entry_index) {
			*object = cur;
			return FS_OK;
		}
	}

	FatLocation location;
	location.longname_cluster  = iter->longname_cluster;
	location.longname_offset   = iter->longname_entry_index;
	location.shortname_cluster = iter->last_cluster;
	location.shortname_offset  = iter->last_entry_index;

	// 生成文件名
	if (iter->longname_valid) {
		fat_utf16_to_utf8(iter->longname_buf, iter->longname_len, &name);
	} else {
		name = read_short_name(&short_dir);
	}
	fat_dir_iterator_next(iter);

	entry = generate_dir_entry(
		fat_info, iter->dir_entry, &short_dir, name, &location, NULL);

	*object = entry->object;
	return FS_OK;
}

FsResult fat_closedir(ObjectIterator *iterator) {
	fat_dir_iterator_destroy(iterator->fs_iterator);
	kfree(iterator);
	return FS_OK;
}

FsResult fat_seek(Object *object, size_t offset) {
	object->value.file.offset = offset;
	return FS_OK;
}

FsResult fat_create_handle(ObjectHandle *handle) {
	if (handle == NULL || handle->object == NULL)
		return FS_ERROR_INVALID_PARAMS;
	Object *file = handle->object;
	if (file->fs_info == NULL) return FS_ERROR_INVALID_PARAMS;
	FatInfo		   *fat_info	= file->fs_info->private_data;
	CurrentCluster *cur_cluster = kmalloc(sizeof(CurrentCluster));
	if (cur_cluster == NULL) return FS_ERROR_OUT_OF_MEMORY;
	fat_cluster_list_get(fat_info, file->value.file.data, 0, cur_cluster);
	handle->handle_data = cur_cluster;
	return FS_OK;
}

FsResult fat_delete_handle(ObjectHandle *handle) {
	if (handle == NULL) return FS_ERROR_INVALID_PARAMS;
	kfree(handle->handle_data);
	return FS_OK;
}

FsResult fat_transfer(
	BlockTransfer transfer, TransferDirection direction,
	IsTransferDone is_transfer_done, FatInfo *fat_info, Object *storage_object,
	Object *file, ObjectHandle *handle, void *buf, size_t size) {
	FatDirEntry *entry = file->value.file.data;

	uint32_t cluster_index =
		file->value.file.offset / fat_info->bytes_per_cluster;

	CurrentCluster *cur_cluster = handle->handle_data;
	if (cluster_index - cur_cluster->index > 1) {
		fat_cluster_list_get(fat_info, entry, cluster_index, cur_cluster);
	} else if (cluster_index - cur_cluster->index == 1) {
		fat_cluster_list_get_next(fat_info, entry, cur_cluster);
	}

	uint32_t offset = file->value.file.offset % fat_info->bytes_per_cluster;
	uint32_t done	= 0;

	while (size > 0) {
		uint32_t read_size = fat_info->bytes_per_cluster - offset;
		if (read_size > size) { read_size = size; }

		uint32_t sector = cluster2sector(fat_info, cur_cluster->cluster);

		if (offset + read_size == fat_info->bytes_per_cluster) {
			CurrentCluster next = *cur_cluster;

			int count		= 0;
			int total_count = DIV_ROUND_UP(size, fat_info->bytes_per_cluster);
			count			= get_remaining_continuous_clusters(&next);
			count			= MIN(count, total_count);
			fat_cluster_list_skip(entry, cur_cluster, count);

			TransferResult result = transfer(
				storage_object, NULL, direction, buf + done, sector,
				count * fat_info->sector_per_cluster);
			if (result != TRANSFER_OK) return FS_ERROR_TRANSFER;

			done += read_size + (count - 1) * fat_info->bytes_per_cluster;
			size -= read_size + (count - 1) * fat_info->bytes_per_cluster;
			offset = 0;
		} else {
			TransferResult result = transfer(
				storage_object, NULL, direction, buf + done, sector,
				fat_info->sector_per_cluster);
			if (result != TRANSFER_OK) return FS_ERROR_TRANSFER;

			done += read_size;
			size -= read_size;
			offset += read_size;

			if (offset == fat_info->bytes_per_cluster) {
				offset = 0;
				fat_cluster_list_get_next(fat_info, entry, cur_cluster);
			}
		}
	}
	file->value.file.offset += done;

	return FS_OK;
}

FsResult fat_read(Object *file, ObjectHandle *handle, void *buf, size_t size) {
	FatInfo *fat_info		= file->fs_info->private_data;
	Object	*storage_object = fat_info->partition->storage_object;
	return fat_transfer(
		storage_object->in.block, TRANSFER_IN,
		storage_object->in.is_transfer_done, fat_info, storage_object, file,
		handle, buf, size);
}

FsResult fat_write(Object *file, ObjectHandle *handle, void *buf, size_t size) {
	FatInfo *fat_info		= file->fs_info->private_data;
	Object	*storage_object = fat_info->partition->storage_object;
	return fat_transfer(
		storage_object->out.block, TRANSFER_OUT,
		storage_object->out.is_transfer_done, fat_info, storage_object, file,
		handle, buf, size);
}

FsResult fat_create_file(Object *parent_obj, string_t *name, Object **object) {
	FatInfo		*fat_info = parent_obj->fs_info->private_data;
	FatDirEntry *entry, *parent = parent_obj->value.directory.data;

	FatLocation location;
	ShortDir	short_dir;
	FsResult	result = fat_info->ops->fat_dir_lookup(
		   fat_info, parent, *name, &location, &short_dir);
	if (result == FS_OK) { return FS_ERROR_ALREADY_EXISTS; }

	fat_create_entry(fat_info, parent, *name, false, &entry);

	*object = entry->object;
	return FS_OK;
}

FsResult fat_delete_file(Object *parent_obj, ObjectAttr *attr, string_t *name) {
	FatInfo		*fat_info = parent_obj->fs_info->private_data;
	FatDirEntry *parent	  = parent_obj->value.directory.data;

	FatLocation *location = attr->fs_location;

	fat_delete_entry(fat_info, parent, location);
	return FS_OK;
}

FsResult fat_mkdir(Object *parent_obj, string_t *name, Object **object) {
	FatInfo		*fat_info = parent_obj->fs_info->private_data;
	FatDirEntry *entry, *parent = parent_obj->value.directory.data;

	FatLocation location;
	ShortDir	short_dir;
	FsResult	result = fat_info->ops->fat_dir_lookup(
		   fat_info, parent, *name, &location, &short_dir);
	if (result == FS_OK) { return FS_ERROR_ALREADY_EXISTS; }

	entry = generate_dir_entry(
		fat_info, parent, &short_dir, *name, &location, NULL);

	fat_create_entry(fat_info, parent, *name, true, &entry);

	*object = entry->object;
	return FS_OK;
}

FsResult fat_rmdir(Object *parent_obj, ObjectAttr *attr, string_t *name) {
	FatInfo		*fat_info = parent_obj->fs_info->private_data;
	FatDirEntry *entry, *parent = parent_obj->value.directory.data;

	FatLocation *location = attr->fs_location;
	ShortDir	 short_dir;
	FsResult	 result = fat_entry_read(
		fat_info, parent, location->shortname_cluster,
		location->shortname_offset, (uint8_t *)&short_dir);
	if (result != FS_OK) { return result; }

	entry =
		generate_dir_entry(fat_info, parent, &short_dir, *name, location, attr);

	if (!fat_dir_is_empty(fat_info, entry)) return FS_ERROR_NOT_EMPTY;

	fat_delete_entry(fat_info, parent, location);
	return FS_OK;
}

FsResult fat_get_attr(Object *object, ObjectAttr *attr) {
	FatDirEntry *entry = object->value.file.data;

	fat_attr_to_sys_attr(&entry->short_dir, attr, object->attr->fs_location);
	return FS_OK;
}

FsResult fat_set_attr(Object *object, ObjectAttr *attr) {
	FatInfo		*fat_info = object->fs_info->private_data;
	FatDirEntry *entry	  = object->value.file.data;
	FatDirEntry *parent	  = object->parent->value.directory.data;
	fat_attr_from_sys_attr(&entry->short_dir, attr, object->attr->fs_location);
	fat_entry_write(
		fat_info, parent, entry->shortname_cluster, entry->shortname_number,
		(uint8_t *)&entry->short_dir);
	return FS_OK;
}

void fat_initcall() {
	register_fs(&fat_fs);
}

fs_initcall(fat_initcall);
