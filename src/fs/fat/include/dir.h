#ifndef _FAT_DIR_H
#define _FAT_DIR_H

#include "dyn_array.h"
#include "fs/fs.h"
#include "kernel/block_cache.h"
#include "name.h"
#include "objects/object.h"
#include "stdint.h"
#include "string.h"
#include <stdint.h>

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN	   0x02
#define ATTR_SYSTEM	   0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LONG_NAME 0x0f

#define FAT32_BASE_L 0x08
#define FAT32_EXT_L	 0x10

typedef struct ShortDir {
	ShortName name;
	uint8_t	  attr;
	uint8_t	  nt_res;
	uint8_t	  crt_time_tenth;
	uint16_t  crt_time;
	uint16_t  crt_date;
	uint16_t  last_access_date;
	uint16_t  first_cluster_high;
	uint16_t  write_time;
	uint16_t  write_date;
	uint16_t  first_cluster_low;
	uint32_t  file_size;
} __attribute__((packed)) ShortDir;

typedef struct LongDir {
	uint8_t	 order;
	uint16_t name1[5];
	uint8_t	 attr;
	uint8_t	 type;
	uint8_t	 checksum;
	uint16_t name2[6];
	uint16_t first_cluster;
	uint16_t name3[2];
} __attribute__((packed)) LongDir;

typedef struct FatDirEntry {
	string_t name;
	ShortDir short_dir;

	// 短目录项所在的簇号
	uint32_t shortname_cluster;
	// 短目录项所在的簇内的序号
	uint8_t	 shortname_number;

	// 长目录项所在的簇号
	uint32_t longname_cluster;
	// 长目录项所在的簇内的序号
	uint8_t	 longname_number;

	union {
		// 文件夹已知的最后一个子目录项的序号
		int new_entry_number;
	};

	DynArray   *cluster_list;
	BlockCache *cache;

	Object *object;
} FatDirEntry;

struct FatInfo;
FsResult search_dir(
	struct FatInfo *fat_info, FatDirEntry *parent_entry, string_t name,
	bool match_directory, FatDirEntry **out_entry, int mode);
void entry_cache_init(
	struct FatInfo *fat_info, FatDirEntry *entry, size_t cache_size);

#endif