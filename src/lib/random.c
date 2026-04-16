#include <random.h>
#include <stdint.h>

#define GOLDEN_RATIO_64 0x9e3779b97f4a7c15ull
#define SPLITMIX64_MUL1 0xbf58476d1ce4e5b9ull
#define SPLITMIX64_MUL2 0x94d049bb133111ebull
#define XORSHIFT64_MUL	0x2545f4914f6cdd1dull

static uint64_t xorshift_state = 0;

static inline uint64_t random_entropy(void) {
	size_t stack_addr = (size_t)(void *)&stack_addr;
	size_t func_addr  = (size_t)(void *)rand_seed;
	void  *ret = __builtin_extract_return_addr(__builtin_return_address(0));
	size_t ret_addr = (size_t)ret;

	uint64_t mixed = 0;
	mixed ^= (stack_addr << 17);
	mixed ^= (func_addr << 7);
	mixed ^= ret_addr;

	return splitmix(mixed);
}

static inline uint64_t xorshift64star_next(void) {
	uint64_t x = xorshift_state;
	if (x == 0) x = GOLDEN_RATIO_64;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	xorshift_state = x;
	return x * XORSHIFT64_MUL;
}

void rand_seed(uint64_t seed) {
	uint64_t base  = ((uint64_t)seed << 32) | (uint64_t)seed;
	uint64_t mixed = splitmix(base ^ random_entropy());
	xorshift_state = mixed ? mixed : GOLDEN_RATIO_64;
}

uint64_t rand(void) {
	if (xorshift_state == 0) { rand_seed((uint64_t)random_entropy()); }

	return xorshift64star_next();
}

uint64_t splitmix(uint64_t x) {
	x += GOLDEN_RATIO_64;
	x = (x ^ (x >> 30)) * SPLITMIX64_MUL1;
	x = (x ^ (x >> 27)) * SPLITMIX64_MUL2;
	return x ^ (x >> 31);
}