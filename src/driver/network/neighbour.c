#include "driver/network/network_dm.h"
#include "driver/network/protocols/ipv4/arp.h"
#include "kernel/list.h"
#include "kernel/spinlock.h"
#include <driver/network/neighbour.h>
#include <kernel/memory.h>
#include <stdint.h>
#include <string.h>

NeighbourTable neighbour_table;
int			   neighbour_max_retries = 5;

void neighbour_init(void) {
	for (int i = 0; i < NEIGH_BUCKET_SIZE; i++) {
		list_init(&neighbour_table.buckets[i]);
		spinlock_init(&neighbour_table.lock[i]);
	}
}

NeighbourEntry *neighbour_entry_create(
	NetworkDevice *device, NeighbourKey hash_key, uint8_t *addr,
	uint8_t length) {
	NeighbourEntry *entry = kzalloc(sizeof(NeighbourEntry) + length);
	if (!entry) { return NULL; }

	hash_key %= NEIGH_BUCKET_SIZE;

	spinlock_init(&entry->lock);
	entry->key	  = hash_key;
	entry->state  = NEIGH_STATE_NONE;
	entry->device = device;
	entry->hlen	  = 0;
	memset(entry->haddr, 0, sizeof(entry->haddr));
	memcpy(entry->ip_addr, addr, length);

	// list_init(&entry->list);

	switch (device->type) {
	case NETWORK_TYPE_ETHERNET:
		entry->ops = &arp_proto_ops;
		break;
	default:
		break;
	}

	entry->ip_length = length;
	memcpy(entry->ip_addr, addr, length);

	NeighbourTable *table = &neighbour_table;
	spin_lock(&table->lock[hash_key]);
	list_add_tail(&entry->list, &table->buckets[hash_key]);
	spin_unlock(&table->lock[hash_key]);

	return entry;
}

NeighbourEntry *neighbour_table_try_lookup(
	NetworkDevice *device, NeighbourKey hash_key, uint8_t *ip_addr,
	uint8_t ip_length) {
	NeighbourEntry *entry = NULL;

	hash_key %= NEIGH_BUCKET_SIZE;
	spin_lock(&neighbour_table.lock[hash_key]);
	list_for_each_owner (entry, &neighbour_table.buckets[hash_key], list) {
		if (ip_length == entry->ip_length &&
			memcmp(entry->ip_addr, ip_addr, ip_length) == 0) {
			spin_unlock(&neighbour_table.lock[hash_key]);
			return entry;
		}
	}
	spin_unlock(&neighbour_table.lock[hash_key]);

	return NULL;
}

NeighbourEntry *neighbour_table_lookup(
	NetworkDevice *device, NeighbourKey hash_key, uint8_t *ip_addr,
	uint8_t ip_length) {
	NeighbourEntry *entry =
		neighbour_table_try_lookup(device, hash_key, ip_addr, ip_length);
	if (entry != NULL) return entry;

	entry = neighbour_entry_create(device, hash_key, ip_addr, ip_length);

	entry->ops->request(entry, NULL);

	return entry;
}
