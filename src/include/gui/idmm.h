#ifndef _IDMM_H
#define _IDMM_H

#include <kernel/list.h>

typedef struct {
	unsigned int start, end;
	list_t list;
} idcache_t;

typedef struct {
	unsigned int id;
	unsigned int id_max;
	list_t cache;
	list_t list_head;
} idmm_t;

void init_idmm(idmm_t *idmm, unsigned int id_max);
int alloc_id(idmm_t *idmm);
void free_id(idmm_t *idmm, int id);

#endif