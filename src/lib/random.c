#include <stdint.h>

#define GOLDEN_RATIO_32 0x9e3779b9u
#define SPLITMIX32_MUL1 0x85ebca6bu
#define SPLITMIX32_MUL2 0xc2b2ae35u

static uint32_t xorshift_state = 0;

uint32_t splitmix(uint32_t x);

void rand_seed(uint32_t seed) {
	uint32_t a = (uint32_t)(void *)&a;							// 栈地址
	uint32_t b = (uint32_t)(void *)rand_seed;					// 函数地址
	uint32_t c = (uint32_t)(void *)__builtin_return_address(0); // 返回地址

	xorshift_state = seed ^ a ^ b ^ c ? seed : GOLDEN_RATIO_32;

	xorshift_state = splitmix(xorshift_state); // 使用splitmix算法
}

uint32_t rand(void) {
	if (xorshift_state == 0) {
		xorshift_state = GOLDEN_RATIO_32; // 避免状态为0
		rand_seed(xorshift_state);
	}
	uint32_t x = xorshift_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	xorshift_state = x;
	return x;
}

uint32_t splitmix(uint32_t x) {
	x += GOLDEN_RATIO_32;
	x ^= x >> 16;
	x *= SPLITMIX32_MUL1;
	x ^= x >> 13;
	x *= SPLITMIX32_MUL2;
	x ^= x >> 16;
	return x;
}