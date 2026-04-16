#ifndef _RANDOM_H
#define _RANDOM_H

#include <stdint.h>

void	 rand_seed(uint64_t seed);
uint64_t rand(void);
uint64_t splitmix(uint64_t x);

#endif