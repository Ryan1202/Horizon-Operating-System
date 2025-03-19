#include "include/entry.h"
#include "include/attr.h"
#include "include/cluster.h"
#include "include/dir.h"
#include "include/fat.h"
#include "include/name.h"
#include "include/time.h"
#include "kernel/memory.h"
#include "multiple_return.h"
#include "objects/object.h"
#include <driver/time_dm.h>
#include <fs/fs.h>
#include <kernel/block_cache.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

PRIVATE FsResult
entry_read(BlockCacheEntry *entry, size_t cache_size, void *private_data) {
	FatInfo	  *fat_info	 = private_data;
	Partition *partition = fat_info->partition;

	int		 count	= DIV_ROUND_UP(cache_size, fat_info->bpb->BPB_BytesPerSec);
	uint32_t sector = cluster2sector(fat_info, entry->position);

	TRANSFER_IN_BLOCK(
		partition->storage_object, NULL, entry->data, sector, count);
	return FS_OK;
}

PRIVATE FsResult
entry_write(BlockCacheEntry *entry, size_t cache_size, void *private_data) {
	FatInfo	  *fat_info	 = private_data;
	Partition *partition = fat_info->partition;

	int		 count	= DIV_ROUND_UP(cache_size, fat_info->bpb->BPB_BytesPerSec);
	uint32_t sector = cluster2sector(fat_info, entry->position);

	for (int i = 0; i < fat_info->bpb->BPB_NumFATs; i++) {
		TRANSFER_OUT_BLOCK(
			partition->storage_object, NULL, entry->data, sector, count);
	}

	return FS_OK;
}

PUBLIC void entry_cache_init(
	FatInfo *fat_info, FatDirEntry *entry, size_t cache_size) {

	entry->cache =
		block_cache_create(cache_size, 1, entry_read, entry_write, fat_info);
	BlockCacheEntry *cache_entry = block_cache_read(entry->cache, 0);
	block_cache_read_done(cache_entry);
}

PUBLIC FsResult fat_entry_read(
	FatInfo *fat_info, FatDirEntry *parent_entry, int cluster, int number,
	uint8_t *entry) {
	BlockCacheEntry *cache_entry =
		block_cache_read(parent_entry->cache, cluster);
	memcpy(
		entry, cache_entry->data + number * sizeof(ShortDir), sizeof(ShortDir));
	block_cache_read_done(cache_entry);
	return FS_OK;
}

PUBLIC FsResult fat_entry_write(
	FatInfo *fat_info, FatDirEntry *parent_entry, int cluster, int number,
	uint8_t *entry) {
	BlockCacheEntry *cache_entry =
		block_cache_write(parent_entry->cache, cluster);
	memcpy(
		cache_entry->data + number * sizeof(ShortDir), entry, sizeof(ShortDir));
	block_cache_write_done(fat_info->storage_device, cache_entry);
	return FS_OK;
}

PRIVATE FsResult fat_dir_get_new_entry(
	FatInfo *fat_info, FatDirEntry *entry, DEF_MRET(uint32_t, last_cluster),
	DEF_MRET(int, last_number)) {

	int		 number = entry->new_entry_number;
	uint32_t cluster;
	uint8_t	 buf[32];
	FS_RESULT_PASS(get_last_cluster(fat_info, entry, &cluster));
	do {
		FS_RESULT_PASS(fat_entry_read(fat_info, entry, cluster, number, buf));
		number++;
	} while (buf[0] != 0 && number <= fat_info->entry_per_cluster);
	number -= 1;
	if (number == fat_info->entry_per_cluster && buf[0] != 0) {
		FS_RESULT_PASS(alloc_cluster(fat_info, cluster, false, &cluster));
		number = 0;
	}
	entry->new_entry_number = number;
	MRET(last_cluster)		= cluster;
	MRET(last_number)		= number;
	return FS_OK;
}

PRIVATE int fat_increase_new_entry_number(
	FatInfo *fat_info, FatDirEntry *entry, uint32_t *cluster) {
	entry->new_entry_number++;
	if (entry->new_entry_number == fat_info->entry_per_cluster) {
		FS_RESULT_PASS(alloc_cluster(fat_info, *cluster, false, cluster));
		entry->new_entry_number = 0;
	}
	return entry->new_entry_number;
}

PRIVATE FsResult fat_longname_entry_write(
	FatInfo *fat_info, FatDirEntry *parent_entry, string_t name,
	uint8_t checksum, uint32_t *cluster, int *number) {
	LongDir long_dir;
	int		len8, len16;
	int		longdir_count;

	uint16_t *utf16_name = kmalloc(name.length);
	fat_utf8_to_utf16((uint8_t *)name.text, utf16_name, &len8, &len16);
	longdir_count = DIV_ROUND_UP(len16, 13);

	long_dir.attr		   = ATTR_LONG_NAME;
	long_dir.checksum	   = checksum;
	long_dir.first_cluster = 0;
	long_dir.type		   = 0;

	uint16_t *p = utf16_name + (longdir_count - 1) * 13;
	for (int i = 0; i < longdir_count; i++) {
		long_dir.order = longdir_count - i;
		if (i == 0) {
			long_dir.order |= 0x40;
			int j	= 0, k;
			int len = len16 % 13;
			for (k = 0; k < 5; j++, k++) {
				if (j < len) long_dir.name1[k] = p[j];
				else long_dir.name1[k] = 0xffff;
			}
			for (k = 0; k < 6; j++, k++) {
				if (j < len) long_dir.name2[k] = p[j];
				else long_dir.name2[k] = 0xffff;
			}
			for (j = 0; j < 2; j++) {
				if (j < len) long_dir.name3[k] = p[j];
				else long_dir.name3[k] = 0xffff;
			}
		} else {
			memcpy(long_dir.name1, p, 5 * sizeof(uint16_t));
			memcpy(long_dir.name2, p + 5, 6 * sizeof(uint16_t));
			memcpy(long_dir.name3, p + 11, 2 * sizeof(uint16_t));
		}
		FS_RESULT_PASS(fat_entry_write(
			fat_info, parent_entry, *cluster, *number, (uint8_t *)&long_dir));
		p -= 13;

		*number =
			fat_increase_new_entry_number(fat_info, parent_entry, cluster);
	}
	return FS_OK;
}

PUBLIC FatDirEntry *generate_dir_entry(
	FatInfo *fat_info, FatDirEntry *parent_entry, ShortDir *short_dir,
	string_t name, FatLocation *location, ObjectAttr *attr) {
	FatDirEntry *entry = kmalloc(sizeof(FatDirEntry));
	entry->name.text   = kmalloc(name.length);
	memcpy(entry->name.text, name.text, name.length);
	entry->name.length	   = name.length;
	entry->name.max_length = name.length;

	memcpy(&entry->short_dir, short_dir, sizeof(ShortDir));
	bool	 is_directory	  = short_dir->attr & ATTR_DIRECTORY;
	uint32_t cluster		  = location->shortname_cluster;
	int		 number			  = location->shortname_offset;
	uint32_t longname_cluster = location->longname_cluster;
	int		 longname_number  = location->longname_offset;

	entry->cluster_list = dyn_array_new(sizeof(ClusterSegment), 8);
	get_cluster_segment(fat_info, entry);

	Object	   *object;
	ObjectAttr *_attr = attr;
	if (_attr == NULL) {
		_attr = kmalloc(sizeof(ObjectAttr));
		fat_attr_to_sys_attr(short_dir, _attr, location);
	}

	if (is_directory) {
		FsResult result = fs_obj_create_dir(
			parent_entry->object, fat_info->fs_info, entry->name, &object,
			_attr);
		if (result != FS_OK) { return NULL; }
		object->value.directory.data = entry;
		entry_cache_init(fat_info, entry, fat_info->bytes_per_cluster);
	} else {
		FsResult result = fs_obj_create_file(
			parent_entry->object, fat_info->fs_info, entry->name, &object,
			_attr);
		if (result != FS_OK) { return NULL; }
		object->value.file.data	  = entry;
		object->value.file.offset = 0;
		object->value.file.size	  = entry->short_dir.file_size;
	}
	entry->longname_cluster	 = longname_cluster;
	entry->longname_number	 = longname_number;
	entry->shortname_cluster = cluster;
	entry->shortname_number	 = number;
	entry->object			 = object;

	return entry;
}

PUBLIC FsResult fat_create_entry(
	FatInfo *fat_info, FatDirEntry *parent_entry, string_t name,
	bool is_directory, FatDirEntry **out_entry) {

	Time		 time;
	DriverResult result = get_current_time(TIME_TYPE_LOCAL, &time);
	if (result != DRIVER_RESULT_OK) { return FS_ERROR_OTHER; }
	TimeFull full_time = time.time;
	uint16_t cur_time =
		FAT_TIME(full_time.hour, full_time.minute, full_time.second);
	uint16_t cur_date =
		FAT_DATE(full_time.year, full_time.month, full_time.day);

	uint32_t first_cluster;
	FS_RESULT_PASS(alloc_cluster(fat_info, 0, true, &first_cluster));

	NameType type =
		check_name(fat_info->type, (uint8_t *)name.text, name.length);
	ShortName short_name;
	if (fat_info->type != FAT_TYPE_FAT32 && type == LONG_NAME) {
		return FS_ERROR_INVALID_PATH_OR_NAME;
	}
	FS_RESULT_PASS(long_name2short_name(
		fat_info, parent_entry, name, is_directory, &short_name));

	int caps  = check_name_caps((uint8_t *)name.text, 11);
	int ntres = 0;
	if ((caps & 0x03) == 0x01) ntres |= FAT32_BASE_L;
	if ((caps & 0x0c) == 0x04) ntres |= FAT32_BASE_L;

	ShortDir short_dir;
	short_dir.name				 = short_name;
	short_dir.attr				 = ATTR_ARCHIVE;
	short_dir.nt_res			 = ntres;
	short_dir.crt_time_tenth	 = (full_time.second % 2) * 100;
	short_dir.crt_time			 = cur_time;
	short_dir.crt_date			 = cur_date;
	short_dir.last_access_date	 = cur_date;
	short_dir.first_cluster_high = first_cluster >> 16;
	short_dir.write_time		 = cur_time;
	short_dir.write_date		 = cur_date;
	short_dir.first_cluster_low	 = first_cluster & 0xffff;
	short_dir.file_size			 = 0;
	if (is_directory) { short_dir.attr |= ATTR_DIRECTORY; }

	int checksum = fat_checksum(&short_dir.name);

	uint32_t cluster, longname_cluster = 0;
	int		 number, longname_number   = 0;
	fat_dir_get_new_entry(fat_info, parent_entry, &cluster, &number);
	if (fat_info->type == FAT_TYPE_FAT32 && type == LONG_NAME) {
		longname_cluster = cluster;
		longname_number	 = number;
		FS_RESULT_PASS(fat_longname_entry_write(
			fat_info, parent_entry, name, checksum, &cluster, &number));
	}
	FS_RESULT_PASS(fat_entry_write(
		fat_info, parent_entry, cluster, number, (uint8_t *)&short_dir));

	FatDirEntry *entry;
	FatLocation	 location;
	location.longname_cluster  = longname_cluster;
	location.longname_offset   = longname_number;
	location.shortname_cluster = cluster;
	location.shortname_offset  = number;
	entry					   = generate_dir_entry(
		 fat_info, parent_entry, &short_dir, name, &location, NULL);

	if (is_directory) {
		static ShortName dot = {".       ", "   "};
		short_dir.name		 = dot;
		short_dir.attr		 = ATTR_DIRECTORY;
		FS_RESULT_PASS(fat_entry_write(
			fat_info, entry, first_cluster, 0, (uint8_t *)&short_dir));

		short_dir.name.base[1]		= '.';
		short_dir.first_cluster_low = parent_entry->short_dir.first_cluster_low;
		short_dir.first_cluster_high =
			parent_entry->short_dir.first_cluster_high;
		FS_RESULT_PASS(fat_entry_write(
			fat_info, entry, first_cluster, 1, (uint8_t *)&short_dir));
		entry->new_entry_number = 1;
	}
	return FS_OK;
}

PUBLIC FsResult fat_delete_entry(
	FatInfo *fat_info, FatDirEntry *parent, FatLocation *location) {
	uint32_t cluster = location->shortname_cluster;
	uint32_t num	 = location->shortname_offset;

	uint8_t buf[32];
	FS_RESULT_PASS(fat_entry_read(fat_info, parent, cluster, num, buf));
	buf[0] = 0xe5;
	FS_RESULT_PASS(fat_entry_write(fat_info, parent, cluster, num, buf));

	if (fat_info->type == FAT_TYPE_FAT32 && location->longname_cluster != 0) {
		uint32_t cluster = location->longname_cluster;
		int		 number	 = location->longname_offset;

		while (cluster <= location->shortname_cluster &&
			   number < location->shortname_offset) {
			FS_RESULT_PASS(
				fat_entry_read(fat_info, parent, cluster, number, buf));
			buf[0] = 0xe5;
			FS_RESULT_PASS(
				fat_entry_write(fat_info, parent, cluster, number, buf));
			number++;
			if (number == fat_info->entry_per_cluster) {
				cluster = get_next_cluster(fat_info, cluster);
				num		= 0;
			}
		}
	}

	return FS_OK;
}
