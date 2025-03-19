#include "include/dir.h"
#include "include/cluster.h"
#include "include/entry.h"
#include "include/fat.h"
#include "include/name.h"
#include "multiple_return.h"
#include "types.h"
#include <dyn_array.h>
#include <fs/fs.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <stdint.h>
#include <string.h>

void fat_dir_iterator_init(
	FatDirIterator *iter, FatInfo *fat_info, FatDirEntry *dir) {
	iter->fat_info	= fat_info;
	iter->dir_entry = dir;
	fat_cluster_list_get(fat_info, dir, 0, &iter->current_cluster);
	iter->entry_index = 0;
	if (fat_info->use_longname) {
		iter->longname_buf = kmalloc(MAX_LONGNAME * sizeof(uint16_t));
	} else {
		iter->longname_buf = NULL;
	}
	iter->longname_len		   = 0;
	iter->checksum			   = 0;
	iter->longname_valid	   = false;
	iter->longname_cluster	   = iter->current_cluster.cluster;
	iter->longname_entry_index = 0;
	iter->last_cluster		   = 0;
	iter->last_entry_index	   = 0;
}

void fat_dir_iterator_next(FatDirIterator *iter) {
	// 重置长名状态
	iter->longname_len	 = 0;
	iter->longname_valid = false;
}

void fat_dir_iterator_destroy(FatDirIterator *iter) {
	if (iter->longname_buf != NULL) kfree(iter->longname_buf);
}

static inline FsResult fat_read_entry(FatDirIterator *iter, uint8_t *buf) {
	return fat_entry_read(
		iter->fat_info, iter->dir_entry, iter->current_cluster.cluster,
		iter->entry_index, buf);
}

static inline bool is_long_entry(ShortDir *short_dir) {
	return short_dir->attr == ATTR_LONG_NAME;
}

PRIVATE void process_long_entry(
	FatDirIterator *iter, uint32_t current_cluster, int current_entry_index,
	LongDir *entry) {
	// 检查顺序标识
	uint8_t order = entry->order & 0x3F;
	if (order == 0) return;

	// 起始条目初始化缓冲区
	if (entry->order & 0x40) {
		iter->longname_len		   = 0;
		iter->checksum			   = entry->checksum;
		iter->longname_cluster	   = current_cluster;
		iter->longname_entry_index = current_entry_index;
		iter->longname_valid	   = true;
	}

	iter->longname_len +=
		read_long_name(entry, iter->longname_buf + (order - 1) * 13);
}

PRIVATE FsResult process_short_entry(
	FatDirIterator *iter, ShortDir *short_dir, uint32_t current_cluster,
	int current_entry_index) {
	// 验证长名校验和
	if (iter->longname_len > 0) {
		if (fat_checksum(&short_dir->name) != iter->checksum) {
			iter->longname_len = 0;
		}
	}
	return FS_OK;
}

FsResult fat32_read_dir_entry(
	FatDirIterator *iter, DEF_MRET(ShortDir, short_dir)) {
	uint8_t	  entry_buf[32];
	ShortDir *short_dir = (ShortDir *)entry_buf;
	LongDir	 *long_dir;

	while (1) {
		// 读取当前条目
		FS_RESULT_PASS(fat_read_entry(iter, entry_buf));
		iter->last_cluster	   = iter->current_cluster.cluster;
		iter->last_entry_index = iter->entry_index;
		// 读取完立即更新索引
		if (++iter->entry_index >= iter->fat_info->entry_per_cluster) {
			// 获取下一个簇
			uint32_t next = fat_cluster_list_get_next(
				iter->fat_info, iter->dir_entry, &iter->current_cluster);
			if (is_eof(iter->fat_info, next)) return FS_ERROR_CANNOT_FIND;
			iter->entry_index = 0;
		}

		// 处理目录结束标记
		if (entry_buf[0] == 0x00) return FS_ERROR_CANNOT_FIND;
		// 跳过已删除条目
		if (entry_buf[0] == 0xE5) continue;

		if (is_long_entry(short_dir)) {
			// 处理长文件名条目
			long_dir = (LongDir *)entry_buf;
			process_long_entry(
				iter, iter->last_cluster, iter->last_entry_index, long_dir);
		} else {
			MRET(short_dir) = *short_dir;
			// 处理短文件名条目
			return process_short_entry(
				iter, short_dir, iter->last_cluster, iter->last_entry_index);
		}
	}
}

FsResult fat_read_dir_entry(
	FatDirIterator *iter, DEF_MRET(ShortDir, short_dir)) {
	uint8_t	  entry_buf[32];
	ShortDir *short_dir;

	uint32_t current_cluster;
	int		 current_entry_index;

	while (1) {
		// 读取当前条目
		FS_RESULT_PASS(fat_read_entry(iter, entry_buf));
		current_cluster		= iter->current_cluster.cluster;
		current_entry_index = iter->entry_index;
		// 读取完立即更新索引
		if (++iter->entry_index >= iter->fat_info->entry_per_cluster) {
			// 跳转到下一个簇
			uint32_t next = fat_cluster_list_get_next(
				iter->fat_info, iter->dir_entry, &iter->current_cluster);
			if (is_eof(iter->fat_info, next)) return FS_ERROR_CANNOT_FIND;
			iter->entry_index = 0;
		}

		short_dir = (ShortDir *)entry_buf;

		// 处理目录结束标记
		if (entry_buf[0] == 0x00) return FS_ERROR_CANNOT_FIND;

		// 跳过已删除条目
		if (entry_buf[0] == 0xE5) continue;

		// 处理短文件名条目
		MRET(short_dir) = *short_dir;
		return process_short_entry(
			iter, short_dir, current_cluster, current_entry_index);
	}
}

FsResult fat32_dir_lookup(
	FatInfo *fat_info, FatDirEntry *parent_entry, string_t name,
	DEF_MRET(FatLocation, location), DEF_MRET(ShortDir, short_dir)) {
	FatDirIterator iter;
	string_t	   _name;
	uint8_t		   entry_buf[0x20];
	ShortDir	  *short_dir = (ShortDir *)entry_buf;
	fat_dir_iterator_init(&iter, fat_info, parent_entry);
	while (fat32_read_dir_entry(&iter, short_dir) == FS_OK) {
		// 生成文件名
		if (iter.longname_valid) {
			fat_utf16_to_utf8(iter.longname_buf, iter.longname_len, &_name);
		} else {
			_name = read_short_name(short_dir);
		}
		fat_dir_iterator_next(&iter);

		if (strncmp(_name.text, name.text, name.length) == 0) {
			string_del(&_name);
			fat_dir_iterator_destroy(&iter);
			MRET(location).longname_cluster	 = iter.longname_cluster;
			MRET(location).longname_offset	 = iter.longname_entry_index;
			MRET(location).shortname_cluster = iter.last_cluster;
			MRET(location).shortname_offset	 = iter.last_entry_index;
			MRET(location).parent_cluster =
				parent_entry->short_dir.first_cluster_high << 16 |
				parent_entry->short_dir.first_cluster_low;
			MRET(location).first_cluster = short_dir->first_cluster_high << 16 |
										   short_dir->first_cluster_low;
			MRET(short_dir) = *short_dir;
			// MRET(entry) = generate_dir_entry(
			// 	fat_info, parent_entry, short_dir, name,
			// 	short_dir->attr & ATTR_DIRECTORY, iter.last_cluster,
			// 	iter.last_entry_index, iter.longname_cluster,
			// 	iter.longname_entry_index);
			return FS_OK;
		}
	}
	fat_dir_iterator_destroy(&iter);
	return FS_ERROR_CANNOT_FIND;
}

FsResult fat_dir_lookup(
	FatInfo *fat_info, FatDirEntry *parent_entry, string_t name,
	DEF_MRET(FatLocation, location), DEF_MRET(ShortDir, short_dir)) {
	FatDirIterator iter;
	string_t	   _name;
	uint8_t		   entry_buf[0x20];
	ShortDir	  *short_dir = (ShortDir *)entry_buf;

	fat_dir_iterator_init(&iter, fat_info, parent_entry);
	while (fat_read_dir_entry(&iter, short_dir) == FS_OK) {
		// 生成文件名
		if (iter.longname_valid) {
			fat_utf16_to_utf8(iter.longname_buf, iter.longname_len, &_name);
		} else {
			_name = read_short_name(short_dir);
		}
		fat_dir_iterator_next(&iter);

		if (strncmp(_name.text, name.text, name.length) == 0) {
			string_del(&_name);
			fat_dir_iterator_destroy(&iter);
			MRET(location).longname_cluster	 = iter.longname_cluster;
			MRET(location).longname_offset	 = iter.longname_entry_index;
			MRET(location).shortname_cluster = iter.last_cluster;
			MRET(location).shortname_offset	 = iter.last_entry_index;
			MRET(location).parent_cluster =
				parent_entry->short_dir.first_cluster_high << 16 |
				parent_entry->short_dir.first_cluster_low;
			MRET(location).first_cluster = short_dir->first_cluster_high << 16 |
										   short_dir->first_cluster_low;
			MRET(short_dir) = *short_dir;
			// MRET(entry) = generate_dir_entry(
			// 	fat_info, parent_entry, short_dir, name,
			// 	short_dir->attr & ATTR_DIRECTORY, iter.last_cluster,
			// 	iter.last_entry_index, iter.longname_cluster,
			// 	iter.longname_entry_index);
			return FS_OK;
		}
	}
	fat_dir_iterator_destroy(&iter);
	return FS_ERROR_CANNOT_FIND;
}

bool fat_dir_is_empty(FatInfo *fat_info, FatDirEntry *parent_entry) {
	FatDirIterator iter;

	uint8_t	  entry_buf[0x20];
	ShortDir *short_dir = (ShortDir *)entry_buf;

	FsResult result = FS_OK;

	fat_dir_iterator_init(&iter, fat_info, parent_entry);
	while (1) {
		result = fat_read_entry(&iter, entry_buf);
		if (result != FS_OK) break;

		if (entry_buf[0] == 0xe5 || entry_buf[0] == 0x05) continue;
		if (short_dir->attr == ATTR_LONG_NAME) continue;
		if (entry_buf[0] == '.') continue;

		if (entry_buf[0] != 0) {
			fat_dir_iterator_destroy(&iter);
			return false;
		}
	}
	fat_dir_iterator_destroy(&iter);
	return true;
}
