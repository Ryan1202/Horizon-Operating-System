#ifndef _FAT_CLUSTER_H
#define _FAT_CLUSTER_H

#include "dir.h"
#include "dyn_array.h"
#include "fat.h"
#include "fs/fs.h"
#include <stdint.h>
#include <types.h>

// 保存一个连续的簇链
typedef struct ClusterSegment {
	uint32_t start;
	uint32_t end;
} ClusterSegment;

typedef struct CurrentCluster {
	uint32_t	   cluster;
	int			   offset;
	DynArrayBlock *block;
} CurrentCluster;

uint32_t cluster2sector(FatInfo *fat_info, uint32_t cluster);

FsResult fat_table_read(
	BlockCacheEntry *entry, size_t cache_size, void *private_data);
FsResult fat_table_write(
	BlockCacheEntry *entry, size_t cache_size, void *private_data);

uint32_t get_next_cluster(FatInfo *fat_info, uint32_t clus);
void	 set_cluster(FatInfo *fat_info, uint32_t cluster, uint32_t data);
FsResult alloc_cluster(
	FatInfo *fat_info, uint32_t last_cluster, bool is_first_cluster,
	uint32_t *out_clus);
FsResult free_cluster(FatInfo *fat_info, uint32_t cluster, uint32_t *out_clus);
bool	 is_eof(FatInfo *fat_info, uint32_t clus);
FsResult get_cluster_segment(FatInfo *fat_info, FatDirEntry *entry);

FsResult fat_cluster_list_get(
	FatInfo *fat_info, FatDirEntry *entry, uint32_t index,
	CurrentCluster *cur_cluster);
FsResult fat_cluster_list_get_next(
	FatInfo *fat_info, FatDirEntry *entry, CurrentCluster *cur_cluster);
inline bool fat_cluster_list_is_last(
	FatInfo *fat_info, FatDirEntry *entry, CurrentCluster *cur_cluster);
FsResult get_last_cluster(
	FatInfo *fat_info, FatDirEntry *entry, DEF_MRET(uint32_t, last_cluster));

#endif