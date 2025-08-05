#ifndef _RANDOM_H
#define _RANDOM_H

#include <stdint.h>

void	 rand_seed(uint32_t seed);
uint32_t rand(void);
uint32_t splitmix(uint32_t x);

#endif