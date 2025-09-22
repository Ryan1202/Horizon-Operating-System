#include <config.h>
#include <math.h>

int max(int a, int b) {
	return a > b ? a : b;
}

int min(int a, int b) {
	return a < b ? a : b;
}

int abs(int n) {
	return n >= 0 ? n : -n;
}

int pow(int x, int y) {
	int i, ans = x;
	for (i = 0; i < y; i++) {
		ans *= x;
	}
	return ans;
}

unsigned int find_next_pow_of_2(unsigned int n) {
#ifdef HAS_BUILTIN_CLZ
	if (n == 0) return 1;
	return 1 << (32 - __builtin_clz(n - 1));
#else
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;
#endif
	return n;
}

// 计算log2(n)的向上取整
int aligned_up_log2n(unsigned int n) {
#ifdef HAS_BUILTIN_CLZ
	return 32 - __builtin_clz(n - 1);
#else
	// De Bruijn序列查找表（32位版本）
	static const uint8_t de_bruijn_table[32] = {
		0, 9,  1,  10, 13, 21, 2,  29, 11, 14, 16, 18, 22, 25, 3, 30,
		8, 12, 20, 28, 15, 17, 24, 7,  19, 27, 23, 6,  26, 5,  4, 31};
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;
	return de_bruijn_table[(n * 0x07C4ACDD) >> 27];
#endif
}

int aligned_down_log2n(unsigned int n) {
#ifdef HAS_BUILTIN_CLZ
	return 31 - __builtin_clz(n);
#else
	// De Bruijn序列查找表（32位版本）
	static const uint8_t de_bruijn_table[32] = {
		0, 9,  1,  10, 13, 21, 2,  29, 11, 14, 16, 18, 22, 25, 3, 30,
		8, 12, 20, 28, 15, 17, 24, 7,  19, 27, 23, 6,  26, 5,  4, 31};
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n >>= 1;
	n++;
	return de_bruijn_table[(n * 0x07C4ACDD) >> 27];
#endif
}
