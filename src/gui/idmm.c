/**
 * @file idmm.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 图层ID管理器
 * @version 0.1
 * @date 2022-08-13
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <gui/idmm.h>
#include <kernel/memory.h>

/**
 * @brief 初始化ID管理器
 * 
 * @param idmm ID管理结构
 * @param id_max 最大的ID
 */
void init_idmm(idmm_t *idmm, unsigned int id_max)
{
	list_init(&idmm->cache);
	idmm->id = 1;
	idmm->id_max = id_max;
	return;
}

/**
 * @brief 分配一个id
 * 
 * @param idmm ID管理结构
 * @return int 分配的id,-1为分配失败
 */
int alloc_id(idmm_t *idmm)
{
	int id = -1;
	if (!list_empty(&idmm->cache))
	{
		idcache_t *idcache = list_first_owner(&idmm->cache, idcache_t, list);
		id = idcache->start;
		idcache->start++;
		if (idcache->start > idcache->end)
		{
			list_del(&idcache->list);
			kfree(idcache);
		}
	}
	else
	{
		if (idmm->id == idmm->id_max)
		{
			return -1;
		}
		id = idmm->id;
		idmm->id++;
	}
	return id;
}

/**
 * @brief 释放一个id
 * 
 * @param idmm ID管理结构
 * @param id 要释放的id
 */
void free_id(idmm_t *idmm, int id)
{
	if (idmm->id == id + 1)
	{
		idmm->id--;
		return;
	}
	else
	{
		if (!list_empty(&idmm->cache))
		{
			idcache_t *cur, *next;
			list_for_each_owner_safe(cur, next, &idmm->list_head, list)
			{
				if (cur->start == id + 1)
				{
					cur->start--;
					return;
				}
				else if (cur->end == id - 1)
				{
					cur->end++;
					return;
				}
			}
		}
		idcache_t *idcache = kmalloc(sizeof(idcache_t));
		idcache->start = idcache->end = id;
		list_add_tail(&idcache->list, &idmm->cache);
	}
	return;
}