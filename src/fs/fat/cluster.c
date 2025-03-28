#include "include/cluster.h"
#include "include/dir.h"
#include "include/fat.h"
#include "multiple_return.h"
#include <driver/storage/disk/disk.h>
#include <driver/storage/storage_io.h>
#include <dyn_array.h>
#include <fs/fs.h>
#include <kernel/block_cache.h>
#include <kernel/list.h>
#include <kernel/thread.h>
#include <objects/transfer.h>
#include <stdint.h>
#include <types.h>

inline uint32_t cluster2sector(FatInfo *fat_info, uint32_t cluster) {
	return fat_info->data_start + (cluster - 2) * fat_info->bpb->BPB_SecPerClus;
}

FsResult fat_table_read(
	BlockCacheEntry *entry, size_t cache_size, void *private_data) {
	FatInfo	  *fat_info	 = private_data;
	Partition *partition = fat_info->partition;

	int count = cache_size / SECTOR_SIZE;

	TRANSFER_IN_BLOCK(
		partition->storage_object, NULL, entry->data,
		fat_info->fat_start + entry->position, count);
	return FS_OK;
}

FsResult fat_table_write(
	BlockCacheEntry *entry, size_t cache_size, void *private_data) {
	FatInfo	  *fat_info	 = private_data;
	Partition *partition = fat_info->partition;

	int count = cache_size / SECTOR_SIZE;

	for (int i = 0; i < fat_info->bpb->BPB_NumFATs; i++) {
		TRANSFER_OUT_BLOCK(
			partition->storage_object, NULL, entry->data,
			fat_info->fat_start + entry->position, count);
	}

	return FS_OK;
}

uint32_t get_next_cluster(FatInfo *fat_info, uint32_t cluster) {
	uint32_t index	= cluster / fat_info->num_count;
	uint32_t offset = cluster % fat_info->num_count;

	BlockCacheEntry *entry = block_cache_read(fat_info->fat_table_cache, index);
	if (entry == NULL) return 0xffffffff;
	uint32_t data = 0xffffffff;
	if (fat_info->type == FAT_TYPE_FAT32) {
		data = ((uint32_t *)entry->data)[offset];
	} else if (fat_info->type == FAT_TYPE_FAT16) {
		data = ((uint16_t *)entry->data)[offset];
	}
	block_cache_read_done(entry);

	return data;
}

void set_cluster(FatInfo *fat_info, uint32_t cluster, uint32_t data) {
	uint32_t index	= cluster / fat_info->num_count;
	uint32_t offset = cluster % fat_info->num_count;

	BlockCacheEntry *entry =
		block_cache_write(fat_info->fat_table_cache, index);

	if (fat_info->type == FAT_TYPE_FAT32) {
		((uint32_t *)entry->data)[offset] = data;
	} else if (fat_info->type == FAT_TYPE_FAT16) {
		((uint16_t *)entry->data)[offset] = data & 0xffff;
	}

	block_cache_write_done(fat_info->storage_device, entry);
}

FsResult alloc_cluster(
	FatInfo *fat_info, uint32_t last_cluster, bool is_first_cluster,
	uint32_t *out_cluster) {

	// 1.先找到一个空闲的簇
	uint32_t index	= fat_info->last_cluster / fat_info->num_count;
	uint32_t offset = fat_info->last_cluster % fat_info->num_count;

	int	 i;
	bool finded = false;
	while (index < fat_info->fat_sectors && !finded) {
		BlockCacheEntry *entry =
			block_cache_read(fat_info->fat_table_cache, index);
		for (i = offset; i < fat_info->num_count; i++) {
			if (((uint32_t *)entry->data)[i] == 0) {
				finded = true;
				break;
			}
		}
		block_cache_read_done(entry);

		index++;
		offset = 0;
	}
	fat_info->last_cluster = index * fat_info->num_count + i;
	if (finded) {
		set_cluster(fat_info, index * fat_info->num_count + i, 0x0fffffff);
		*out_cluster = index * fat_info->num_count + i;
	} else {
		return FS_ERROR_NO_SPARE_SPACE;
	}

	if (!is_first_cluster) {
		// 2.将上一个簇的FAT表项指向新分配的簇
		set_cluster(fat_info, last_cluster, *out_cluster);
	}

	return FS_OK;
}

FsResult free_cluster(
	FatInfo *fat_info, uint32_t cluster, uint32_t *out_cluster) {
	uint32_t index	= cluster / fat_info->num_count;
	uint32_t offset = cluster % fat_info->num_count;

	BlockCacheEntry *entry;

	if (out_cluster != NULL) {
		entry = block_cache_read(fat_info->fat_table_cache, index);
		if (fat_info->type == FAT_TYPE_FAT32) {
			*out_cluster = ((uint32_t *)entry->data)[offset];
		} else if (fat_info->type == FAT_TYPE_FAT16) {
			*out_cluster = ((uint16_t *)entry->data)[offset];
		}
		block_cache_read_done(entry);
	}

	set_cluster(fat_info, cluster, 0);

	return FS_OK;
}

inline bool is_eof(FatInfo *fat_info, uint32_t cluster) {
	if (fat_info->type == FAT_TYPE_FAT32) {
		return cluster >= 0x0FFFFFF8;
	} else if (fat_info->type == FAT_TYPE_FAT16) {
		return cluster >= 0xFFF8;
	} else if (fat_info->type == FAT_TYPE_FAT12) {
		return cluster >= 0x0FF8;
	}
	return false;
}

FsResult get_cluster_segment(FatInfo *fat_info, FatDirEntry *entry) {
	DynArray	  *arr = entry->cluster_list;
	ClusterSegment segment;

	uint32_t cluster;
	if (arr->size == 0) {
		// 如果为空使用第一个簇
		cluster = entry->short_dir.first_cluster_low;
		if (fat_info->type == FAT_TYPE_FAT32) {
			cluster |= entry->short_dir.first_cluster_high << 16;
		}
	} else {
		// 否则使用最后一个簇的下一个簇
		cluster =
			((ClusterSegment)dyn_array_get(arr, ClusterSegment, arr->size - 1))
				.end;
		if (is_eof(fat_info, cluster)) { return FS_ERROR_END_OF_FILE; }
		cluster = get_next_cluster(fat_info, cluster);
	}
	segment.start = cluster;

	int offset = cluster % fat_info->bpb->BPB_BytesPerSec;
	int max	   = cluster + fat_info->bpb->BPB_BytesPerSec - offset;
	while (!is_eof(fat_info, cluster) && cluster < max) {
		uint32_t tmp = get_next_cluster(fat_info, cluster);
		if (tmp != cluster + 1) {
			segment.end = cluster;
			dyn_array_append(arr, ClusterSegment, segment);
			if (tmp < 2) return FS_ERROR_ILLEGAL_DATA;
			else return FS_OK;
		}
		cluster = tmp;
	}
	if (cluster == max) {
		segment.end = cluster;
		dyn_array_append(arr, ClusterSegment, segment);
		return FS_OK;
	}

	segment.start = segment.end = 0x0fffffff;
	dyn_array_append(arr, ClusterSegment, segment);

	return FS_OK;
}

FsResult fat_cluster_list_get(
	FatInfo *fat_info, FatDirEntry *entry, uint32_t index,
	CurrentCluster *cur_cluster) {
	DynArray *arr = entry->cluster_list;

	int counter = index;

	ClusterSegment *clus_seg;
	dyn_array_foreach(arr, ClusterSegment, clus_seg) {
		int length = clus_seg->end - clus_seg->start + 1;
		if (counter < length) {
			cur_cluster->cluster = clus_seg->start + counter;
			cur_cluster->offset	 = _block_offset;
			cur_cluster->block	 = _block;
			cur_cluster->index	 = index;
			cur_cluster->entry	 = entry;
			return FS_OK;
		}
		counter -= length;
	}

	return FS_OK;
}

FsResult fat_cluster_list_get_next(
	FatInfo *fat_info, FatDirEntry *entry, CurrentCluster *cur_cluster) {
	FsResult		result;
	ClusterSegment *seg =
		&((ClusterSegment *)cur_cluster->block->data)[cur_cluster->offset];
	if (cur_cluster->cluster < seg->end) {
		cur_cluster->cluster++;
	} else {
		cur_cluster->offset++;
		if (cur_cluster->offset >= entry->cluster_list->block_size) {
			cur_cluster->offset = 0;
			if (cur_cluster->block->next == NULL) {
				result = get_cluster_segment(fat_info, entry);
				if (result != FS_OK) return result;
			}
			cur_cluster->block = cur_cluster->block->next;
		}
		seg =
			&((ClusterSegment *)cur_cluster->block->data)[cur_cluster->offset];
		if (cur_cluster->offset >= (entry->cluster_list->block_size -
									cur_cluster->block->left_space)) {
			FS_RESULT_PASS(get_cluster_segment(fat_info, entry));
		}
		cur_cluster->cluster = seg->start;
	}
	cur_cluster->index++;
	return FS_OK;
}

uint32_t get_remaining_continuous_clusters(
	FatInfo *fat_info, CurrentCluster *cur_cluster) {
	uint32_t		current = cur_cluster->cluster;
	ClusterSegment *seg =
		&((ClusterSegment *)cur_cluster->block->data)[cur_cluster->offset];
	// TODO:获取下一个簇段
	if (seg->end == current) {
		FsResult result = fat_cluster_list_get_next(
			fat_info, cur_cluster->entry, cur_cluster);
		if (result != FS_OK) return 0;
		seg =
			&((ClusterSegment *)cur_cluster->block->data)[cur_cluster->offset];
	}

	return seg->end - current;
}

FsResult fat_cluster_list_skip(
	FatInfo *fat_info, FatDirEntry *entry, CurrentCluster *cur_cluster,
	int count) {
	ClusterSegment *seg =
		&((ClusterSegment *)cur_cluster->block->data)[cur_cluster->offset];
	uint32_t current = cur_cluster->cluster;
	uint32_t end	 = seg->end;
	if (current + count <= end) {
		cur_cluster->cluster += count;
	} else {
		count -= end - current + 1;
		cur_cluster->offset++;
		if (cur_cluster->offset >= entry->cluster_list->block_size) {
			cur_cluster->block	= cur_cluster->block->next;
			cur_cluster->offset = 0;
		}
		seg =
			&((ClusterSegment *)cur_cluster->block->data)[cur_cluster->offset];
		while (count > seg->end - seg->start + 1) {
			count -= seg->end - seg->start + 1;
			cur_cluster->offset++;
			if (cur_cluster->offset >= entry->cluster_list->block_size) {
				if (cur_cluster->block->next == NULL) {
					FS_RESULT_PASS(get_cluster_segment(fat_info, entry));
				}
				cur_cluster->block	= cur_cluster->block->next;
				cur_cluster->offset = 0;
			}
			seg = &((ClusterSegment *)
						cur_cluster->block->data)[cur_cluster->offset];
		}
		cur_cluster->cluster = seg->start + count;
	}
	return FS_OK;
}

FsResult get_last_cluster(
	FatInfo *fat_info, FatDirEntry *entry, DEF_MRET(uint32_t, last_cluster)) {
	DynArray	  *arr = entry->cluster_list;
	ClusterSegment seg, tmp;

	tmp = dyn_array_get(arr, ClusterSegment, arr->size - 1);
	if (!is_eof(fat_info, tmp.end)) {
		while (!is_eof(fat_info, tmp.end)) {
			seg = tmp;
			FS_RESULT_PASS(get_cluster_segment(fat_info, entry));
			tmp = dyn_array_get(arr, ClusterSegment, arr->size - 1);
		}
	}
	MRET(last_cluster) = seg.end;
	return FS_OK;
}
