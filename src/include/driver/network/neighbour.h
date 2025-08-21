#ifndef NEIGHBOUR_H
#define NEIGHBOUR_H

#include "driver/network/network_dm.h"
#include "kernel/list.h"
#include "kernel/spinlock.h"
#include <stdint.h>

#define NEIGH_BUCKET_SIZE 32

typedef enum {
	NEIGH_STATE_NONE,
	NEIGH_STATE_WAITING,
	NEIGH_STATE_REACHABLE,
	NEIGH_STATE_STALE,
	NEIGH_STATE_FAILED,
} NeighbourState;

typedef uint32_t NeighbourKey;

struct NeighbourEntry;
typedef struct {
	void (*request)(struct NeighbourEntry *entry, void *arg);
} NeighbourProtoOps;

typedef struct NeighbourEntry {
	list_t	   list;
	spinlock_t lock;

	NeighbourKey   key;
	NeighbourState state;
	uint8_t		   haddr[8]; // Hardware address (MAC address)
	NetworkDevice *device;

	void			  *arg;
	NeighbourProtoOps *ops;

	list_t pending_lh;

	uint8_t ip_length;
	uint8_t ip_addr[0];
} NeighbourEntry;

typedef struct NeighbourTable {
	list_t	   buckets[NEIGH_BUCKET_SIZE];
	spinlock_t lock[NEIGH_BUCKET_SIZE];
} NeighbourTable;

extern int			  neighbour_max_retries;
extern NeighbourTable neighbour_table;

void			neighbour_init(void);
NeighbourEntry *neighbour_entry_create(
	NetworkDevice *device, NeighbourKey hash_key, uint8_t *addr,
	uint8_t length);
NeighbourEntry *neighbour_table_lookup(
	NetworkDevice *device, NeighbourKey hash_key, uint8_t *ip_addr,
	uint8_t ip_length);

#endif