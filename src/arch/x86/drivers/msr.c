/**
 * @file msr.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief
 * @version 0.1
 * @date 2021-08
 */
#include <drivers/msr.h>

void cpu_rdmsr(uint32_t msr, uint32_t *lo, uint32_t *hi) {
	uint32_t l, h;
	__asm__ __volatile__("rdmsr \n\t" : "=a"(l), "=d"(h) : "c"(msr) : "memory");
	*lo = l;
	*hi = h;
}

void cpu_wrmsr(uint32_t msr, uint32_t lo, uint32_t hi) {
	__asm__ __volatile__("wrmsr \n\t"
						 :
						 : "a"(lo), "d"(hi), "c"(msr)
						 : "memory");
}