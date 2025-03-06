#include "kernel/block_cache.h"
#include "driver/storage/storage_dm.h"
#include "kernel/rwlock.h"
#include "stddef.h"
#include <kernel/list.h>
#include <kernel/memory.h>

// 将节点移到LRU链表头部
static void move_to_list_head(BlockCache *cache, BlockCacheEntry *entry) {
	list_del(&entry->lru_node);
	list_add(&entry->lru_node, &cache->lru_lh);
}

static BlockCacheEntry *get_free_entry(BlockCache *cache) {
	BlockCacheEntry *entry;
	list_for_each_owner_reverse (entry, &cache->lru_lh, lru_node) {
		if (rwlock_write_try_lock(&entry->lock)) {
			if (entry->dirty) {
				cache->write(entry, cache->size, cache->private_data);
				entry->dirty = false;
				list_del(&entry->list);
			}
			return entry;
		}
	}
	return NULL;
}

BlockCache *block_cache_create(
	int size, int count, BlockCacheRealTransfer read,
	BlockCacheRealTransfer write, void *private_data) {
	BlockCache *cache = kmalloc(sizeof(BlockCache));
	if (!cache) return NULL;
	cache->count   = count;
	cache->size	   = size;
	cache->entries = kmalloc(sizeof(BlockCacheEntry) * count);
	if (!cache->entries) {
		kfree(cache);
		return NULL;
	}

	cache->read			= read;
	cache->write		= write;
	cache->private_data = private_data;

	list_init(&cache->lru_lh);
	for (int i = 0; i < count; i++) {
		BlockCacheEntry *entry = &cache->entries[i];
		rwlock_init(&entry->lock);
		entry->list.prev = NULL;
		entry->list.next = NULL;
		entry->cache	 = cache;
		entry->position	 = -1;
		entry->data		 = kmalloc(cache->size);
		list_add_tail(&entry->lru_node, &cache->lru_lh);
	}
	return cache;
}

void block_cache_destroy(BlockCache *cache) {
	// 释放所有缓存项
	BlockCacheEntry *entry;
	list_t			*pos, *next;
	list_for_each_safe(pos, next, &cache->lru_lh) {
		entry = (BlockCacheEntry *)((char *)pos -
									offsetof(BlockCacheEntry, lru_node));
		if (entry->dirty) {
			cache->write(entry, cache->size, cache->private_data);
			list_del(&entry->list);
		}
		list_del(&entry->lru_node);
		kfree(entry->data);
		kfree(entry);
	}
	kfree(cache);
}

static BlockCacheEntry *find_entry(BlockCache *cache, size_t position) {
	BlockCacheEntry *entry;
	list_for_each_owner (entry, &cache->lru_lh, lru_node) {
		if (entry->position == position) return entry;
	}
	return NULL;
}

BlockCacheEntry *block_cache_read(BlockCache *cache, size_t position) {
	BlockCacheEntry *entry = find_entry(cache, position);
	if (entry == NULL) {
		// 未命中，调用read回调函数
		entry = get_free_entry(cache);
		if (entry == NULL) return NULL;
		entry->position = position;
		cache->read(entry, cache->size, cache->private_data);
		move_to_list_head(cache, entry);
		rwlock_write_unlock(&entry->lock);
	}
	rwlock_read_lock(&entry->lock);
	return entry;
}

BlockCacheEntry *block_cache_write(BlockCache *cache, size_t position) {
	BlockCacheEntry *entry = find_entry(cache, position);
	if (entry == NULL) {
		// 未命中，调用read回调函数
		entry = get_free_entry(cache);
		if (entry == NULL) return NULL;
		entry->position = position;
		move_to_list_head(cache, entry);
	} else {
		rwlock_write_lock(&entry->lock);
	}
	entry->dirty = true;
	return entry;
}

void block_cache_read_done(BlockCacheEntry *entry) {
	rwlock_read_unlock(&entry->lock);
}

void block_cache_write_done(
	StorageDevice *storage_device, BlockCacheEntry *entry) {
	if (storage_device->block_cache_lh.next != NULL &&
		entry->list.prev == NULL) {
		list_add_tail(&entry->list, &storage_device->block_cache_lh);
	}
	rwlock_write_unlock(&entry->lock);
}
