#ifndef _FAT_ENTRY_H
#define _FAT_ENTRY_H

#include "dir.h"
#include "fs/fs.h"

struct FatInfo;
struct FatDirEntry;
FsResult fat_entry_read(
	struct FatInfo *fat_info, struct FatDirEntry *parent_entry, int cluster,
	int number, uint8_t *entry);
FsResult fat_entry_write(
	struct FatInfo *fat_info, struct FatDirEntry *parent_entry, int cluster,
	int number, uint8_t *entry);
FatDirEntry *generate_dir_entry(
	struct FatInfo *fat_info, FatDirEntry *parent_entry, ShortDir *short_dir,
	string_t name, bool is_directory, uint32_t cluster, uint32_t number,
	uint32_t longname_cluster, uint32_t longname_number);
FsResult fat_create_entry(
	struct FatInfo *fat_info, FatDirEntry *parent_entry, string_t name,
	bool is_directory, FatDirEntry **out_entry);
FsResult fat_delete_entry(
	struct FatInfo *fat_info, FatDirEntry *parent, FatDirEntry *entry,
	string_t name);

#endif