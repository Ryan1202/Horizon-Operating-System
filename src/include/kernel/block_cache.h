#ifndef _BLOCK_CACHE_H
#define _BLOCK_CACHE_H

#include "driver/storage/storage_dm.h"
#include "fs/fs.h"
#include "kernel/list.h"
#include "kernel/rwlock.h"
#include "stddef.h"

typedef struct BlockCacheEntry {
	struct BlockCache *cache;
	list_t			   list;
	size_t			   position;
	void			  *data;
	bool			   dirty;
	rwlock_t		   lock;
	list_t			   lru_node;
} BlockCacheEntry;

typedef FsResult (*BlockCacheRealTransfer)(
	BlockCacheEntry *cache_entry, size_t cache_size, void *private_data);

typedef struct BlockCache {
	int				 count;
	int				 size;
	list_t			 lru_lh; // 全局LRU链表头，头部为最近使用
	BlockCacheEntry *entries;

	void *private_data;

	BlockCacheRealTransfer read;
	BlockCacheRealTransfer write;
} BlockCache;

// 创建和销毁LRU缓存
BlockCache *block_cache_create(
	int size, int count, BlockCacheRealTransfer read,
	BlockCacheRealTransfer write, void *private_data);
void block_cache_destroy(BlockCache *cache);

BlockCacheEntry *block_cache_read(BlockCache *cache, size_t position);
BlockCacheEntry *block_cache_write(BlockCache *cache, size_t position);

void block_cache_read_done(BlockCacheEntry *entry);
void block_cache_write_done(
	StorageDevice *storage_device, BlockCacheEntry *entry);

#endif
