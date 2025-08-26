#ifndef _HASH_H
#define _HASH_H

#include <stdint.h>

uint32_t fnv1_hash_32(const void *data, int len);
uint32_t fnv1a_hash_32(const void *data, int len);
uint32_t fnv0_hash_32(const void *data, int len);

void md5(uint8_t digest[16], const uint8_t *data, size_t len);

#endif