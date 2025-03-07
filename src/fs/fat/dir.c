#include "include/dir.h"
#include "include/cluster.h"
#include "include/entry.h"
#include "include/fat.h"
#include "include/name.h"
#include "types.h"
#include <dyn_array.h>
#include <fs/fs.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <stdint.h>
#include <string.h>

typedef struct FatCurrentEntry {
	FatDirEntry	  *entry;
	CurrentCluster cur_cluster;
	int			   number;
} FatCurrentEntry;

static inline void fat_first_entry(
	FatInfo *fat_info, FatCurrentEntry *entry, int number) {
	entry->number = number;
	int index	  = number / fat_info->entry_per_cluster;
	fat_cluster_list_get(fat_info, entry->entry, index, &entry->cur_cluster);
}

static inline FsResult fat_next_entry(
	FatInfo *fat_info, FatCurrentEntry *entry) {
	entry->number++;
	if (entry->number == fat_info->entry_per_cluster) {
		entry->number = 0;
		FS_RESULT_PASS(fat_cluster_list_get_next(
			fat_info, entry->entry, &entry->cur_cluster));
		if (entry->cur_cluster.cluster >= 0x0fffffff)
			return FS_ERROR_END_OF_FILE;
	}
	return FS_OK;
}

static inline void fat_read_current_entry(
	FatInfo *fat_info, FatCurrentEntry *entry, uint8_t *buf) {
	fat_entry_read(
		fat_info, entry->entry, entry->cur_cluster.cluster, entry->number, buf);
}

// 扫描目录项
// mode = 0 : 匹配目录项
// mode = 1 : 扫描所有目录项
FsResult fat32_read_dir_entry(
	FatCurrentEntry *cur_entry, string_t *name, uint8_t *buf,
	uint16_t *utf16_name, uint16_t **_utf16_name, int *utf16_length,
	uint32_t *longname_cluster, int *longname_number) {
	ShortDir *short_dir = (ShortDir *)buf;
	if (short_dir->name.base[0] == 0x00) return FS_ERROR_CANNOT_FIND;
	if (buf[0] == 0xe5 || buf[0] == 0x05) return FS_ERROR_NOT_MATCH;

	LongDir *long_dir = (LongDir *)buf;
	if (long_dir->attr == ATTR_LONG_NAME) {
		if (long_dir->order & 0x40) {
			int count		  = long_dir->order & 0x3f;
			*longname_cluster = cur_entry->cur_cluster.cluster;
			*longname_number  = cur_entry->number;
			*_utf16_name	  = utf16_name + (count - 1) * 13;
			*utf16_length	  = 0;
		}
		*utf16_length += read_long_name(long_dir, *_utf16_name);
		*_utf16_name -= 13;
	} else if (*utf16_length > 0) {
		fat_utf16_to_utf8(utf16_name, *utf16_length + 1, name);
	} else {
		if (short_dir->name.base[0] == '.') return FS_ERROR_NOT_MATCH;
		*name = read_short_name(short_dir);
	}

	return FS_OK;
}

FsResult search_dir(
	FatInfo *fat_info, FatDirEntry *parent_entry, string_t name,
	bool is_directory, FatDirEntry **out_entry, int mode) {
	FatCurrentEntry cur_entry;
	cur_entry.entry = parent_entry;

	FatDirEntry *tmp_entry;

	uint8_t buf[0x20];

	ShortDir *short_dir;
	uint16_t *utf16_name   = kmalloc(256 * sizeof(uint16_t)), *_utf16_name;
	int		  utf16_length = 0;

	uint32_t longname_cluster;
	int		 longname_number;

	FsResult result;
	FsResult _result = FS_OK;

	string_t _name;

	fat_first_entry(fat_info, &cur_entry, 0);
	for (; _result == FS_OK; _result = fat_next_entry(fat_info, &cur_entry)) {
		fat_read_current_entry(fat_info, &cur_entry, buf);
		result = fat32_read_dir_entry(
			&cur_entry, &_name, buf, utf16_name, &_utf16_name, &utf16_length,
			&longname_cluster, &longname_number);

		if (result == FS_ERROR_NOT_MATCH) continue;
		else if (result == FS_ERROR_CANNOT_FIND) break;

		short_dir = (ShortDir *)buf;
		if (utf16_length == 0) {
			// 为短文件名
			longname_cluster = 0;
			longname_number	 = 0;
		}
		if (short_dir->attr != ATTR_LONG_NAME) {
			tmp_entry = generate_dir_entry(
				fat_info, parent_entry, (ShortDir *)buf, _name, is_directory,
				cur_entry.cur_cluster.cluster, cur_entry.number,
				longname_cluster, longname_number);
		}

		if (is_directory && (short_dir->attr & ATTR_DIRECTORY) == 0) continue;
		if (!is_directory && (short_dir->attr & ATTR_DIRECTORY) != 0) continue;

		if (mode == 0 && strncmp(_name.text, name.text, _name.length) == 0) {
			*out_entry = tmp_entry;

			parent_entry->new_entry_number = cur_entry.number + 1;
			result						   = FS_OK;
			break;
		}
	}
	kfree(utf16_name);
	return result;
}

bool fat_dir_is_empty(FatInfo *fat_info, FatDirEntry *parent_entry) {
	FatCurrentEntry cur_entry;
	cur_entry.entry = parent_entry;

	uint8_t	  buf[0x20];
	ShortDir *short_dir = (ShortDir *)buf;

	FsResult _result = FS_OK;

	fat_first_entry(fat_info, &cur_entry, 0);
	for (; _result == FS_OK; _result = fat_next_entry(fat_info, &cur_entry)) {
		fat_read_current_entry(fat_info, &cur_entry, buf);

		if (buf[0] == 0xe5 || buf[0] == 0x05) continue;
		if (short_dir->attr == ATTR_LONG_NAME) continue;
		if (buf[0] == '.') continue;

		if (buf[0] != 0) return false;
	}
	return true;
}
