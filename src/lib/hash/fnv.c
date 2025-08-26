#include <stdint.h>

#define FNV_32_PRIME 0x01000193

uint32_t fnv1_hash_32(const void *data, int len) {
	const uint32_t fnv_offset_basis = 0x811c9dc5; // 2166136261

	const uint8_t *p	= (const uint8_t *)data;
	uint32_t	   hash = fnv_offset_basis;
	for (int i = 0; i < len; i++) {
		hash *= FNV_32_PRIME;
		hash ^= (uint32_t)(p[i]);
	}
	return hash;
}

uint32_t fnv1a_hash_32(const void *data, int len) {
	const uint32_t fnv_offset_basis = 0x811c9dc5; // 2166136261

	const uint8_t *p	= (const uint8_t *)data;
	uint32_t	   hash = fnv_offset_basis;
	for (int i = 0; i < len; i++) {
		hash ^= (uint32_t)(p[i]);
		hash *= FNV_32_PRIME;
	}
	return hash;
}

uint32_t fnv0_hash_32(const void *data, int len) {
	const uint8_t *p	= (const uint8_t *)data;
	uint32_t	   hash = 0;
	for (int i = 0; i < len; i++) {
		hash *= FNV_32_PRIME;
		hash ^= (uint32_t)(p[i]);
	}
	return hash;
}
