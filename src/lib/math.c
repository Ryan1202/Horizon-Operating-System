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
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;
	return n;
}