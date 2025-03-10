#ifndef _FAT_H
#define _FAT_H

#include "dir.h"
#include "driver/storage/disk/disk.h"
#include "driver/storage/storage_dm.h"
#include "fs/fs.h"
#include "kernel/block_cache.h"
#include <stdint.h>

#define FAT_CACHE_SIZE 2

typedef enum FatType {
	FAT_TYPE_FAT12,
	FAT_TYPE_FAT16,
	FAT_TYPE_FAT32,
	FAT_TYPE_EXFAT
} FatType;

typedef struct FatBpb {
	uint8_t	 BS_jmpBoot[3];
	char	 BS_OEMName[8];
	uint16_t BPB_BytesPerSec;
	uint8_t	 BPB_SecPerClus;
	uint16_t BPB_RevdSecCnt;
	uint8_t	 BPB_NumFATs;
	uint16_t BPB_RootEntCnt;
	uint16_t BPB_TotSec16;
	uint8_t	 BPB_Media;
	uint16_t BPB_FATSz16;
	uint16_t BPB_SecPerTrk;
	uint16_t BPB_NumHeads;
	uint32_t BPB_HiddSec;
	uint32_t BPB_TotSec32;
	union {
		struct {
			uint8_t	 BS_DrvNum;
			uint8_t	 BS_Reserved1;
			uint8_t	 BS_BootSig;
			uint32_t BS_VolID;
			char	 BS_VolLab[11];
			char	 BS_FilSysType[8];
		} __attribute__((packed)) fat12_16;
		struct {
			uint32_t BPB_FATSz32;
			uint16_t BPB_ExtFlags;
			uint16_t BPB_FSVer;
			uint32_t BPB_RootClus;
			uint16_t BPB_FSInfo;
			uint16_t BPB_BkBootSec;
			uint8_t	 BPB_Reserved[12];
			uint8_t	 BS_DrvNum;
			uint8_t	 BS_Reserved1;
			uint8_t	 BS_BootSig;
			uint32_t BS_VolID;
			char	 BS_VolLab[11];
			char	 BS_FilSysType[8];
		} __attribute__((packed)) fat32;
	};
} __attribute__((packed)) FatBpb;

typedef struct FatFsInfo {
	uint32_t FSI_LeadSig;
	uint8_t	 FSI_Reserved1[480];
	uint32_t FSI_StrucSig;
	uint32_t FSI_Free_Count;
	uint32_t FSI_Nxt_Free;
	uint8_t	 FSI_Reserved2[12];
	uint32_t FSI_TrailSig;
} __attribute__((packed)) FatFsInfo;

typedef struct FatInfo {
	Partition	   *partition;
	FileSystemInfo *fs_info;
	FatType			type;

	StorageDevice *storage_device;

	FatBpb	  *bpb;
	FatFsInfo *fat_fs_info;

	uint32_t fat_start;
	uint32_t data_start;
	int		 fat_sectors;
	int		 data_sectors;
	int		 total_sectors;
	int		 bytes_per_cluster;
	int		 sector_per_cluster;
	int		 entry_per_cluster;
	int		 num_count;
	int		 max_cluster;
	bool	 use_longname;

	struct FatPrivOps *ops;

	BlockCache *fat_table_cache;

	uint32_t last_cluster;

	FatDirEntry root_entry;
} FatInfo;

typedef struct FatLocation {
	uint32_t longname_cluster;	// 长文件名所在簇号
	uint32_t longname_offset;	// 长文件名所在簇内的序号
	uint32_t shortname_cluster; // 短文件名所在簇号
	uint32_t shortname_offset;	// 短文件名所在簇内的序号
	uint32_t parent_cluster;	// 父目录所在簇号
	uint32_t first_cluster;		// 文件数据所在簇号
} FatLocation;

typedef struct FatPrivOps {
	FsResult (*fat_dir_lookup)(
		struct FatInfo *fat_info, FatDirEntry *parent_entry, string_t name,
		DEF_MRET(FatLocation, location), DEF_MRET(ShortDir, short_dir));
	FsResult (*fat_read_dir_entry)(
		FatDirIterator *iter, DEF_MRET(ShortDir, short_dir));
} FatPrivOps;

#endif