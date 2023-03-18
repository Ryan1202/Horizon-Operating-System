#include <stdlib.h>

static unsigned long int __seed = 1;

void srand(unsigned long seed) {
	__seed = seed;
}

int rand() {
	__seed = __seed * 1103515245 + 12345;
	return (int)(__seed >> 16) % 0x7fff;
}