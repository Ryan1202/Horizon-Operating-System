#ifndef _FAT_CLUSTER_H
#define _FAT_CLUSTER_H

#include "dyn_array.h"
#include "fs/fs.h"
#include "kernel/block_cache.h"
#include <multiple_return.h>
#include <stdint.h>
#include <types.h>

// 保存一个连续的簇链
typedef struct ClusterSegment {
	uint32_t start;
	uint32_t end;
} ClusterSegment;

struct FatDirEntry;
typedef struct CurrentCluster {
	struct FatDirEntry *entry;
	uint32_t			cluster;
	int					offset;
	int					index;
	DynArrayBlock	   *block;
} CurrentCluster;

struct FatInfo;
struct FatDirEntry;
uint32_t cluster2sector(struct FatInfo *fat_info, uint32_t cluster);

FsResult fat_table_read(
	BlockCacheEntry *entry, size_t cache_size, void *private_data);
FsResult fat_table_write(
	BlockCacheEntry *entry, size_t cache_size, void *private_data);

uint32_t get_next_cluster(struct FatInfo *fat_info, uint32_t clus);
void	 set_cluster(struct FatInfo *fat_info, uint32_t cluster, uint32_t data);
FsResult alloc_cluster(
	struct FatInfo *fat_info, uint32_t last_cluster, bool is_first_cluster,
	uint32_t *out_clus);
FsResult free_cluster(
	struct FatInfo *fat_info, uint32_t cluster, uint32_t *out_clus);
bool	 is_eof(struct FatInfo *fat_info, uint32_t clus);
FsResult get_cluster_segment(
	struct FatInfo *fat_info, struct FatDirEntry *entry);

FsResult fat_cluster_list_get(
	struct FatInfo *fat_info, struct FatDirEntry *entry, uint32_t index,
	CurrentCluster *cur_cluster);
FsResult fat_cluster_list_get_next(
	struct FatInfo *fat_info, struct FatDirEntry *entry,
	CurrentCluster *cur_cluster);
uint32_t get_remaining_continuous_clusters(
	struct FatInfo *info, CurrentCluster *cur_cluster);
FsResult fat_cluster_list_skip(
	struct FatInfo *fat_info, struct FatDirEntry *entry,
	CurrentCluster *cur_cluster, int count);
FsResult get_last_cluster(
	struct FatInfo *fat_info, struct FatDirEntry *entry,
	DEF_MRET(uint32_t, last_cluster));

#endif