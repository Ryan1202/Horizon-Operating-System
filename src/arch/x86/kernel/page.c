/**
 * @file page.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 分页管理
 * @version 1.2
 * @date 2022-07-15
 */
#include <kernel/page.h>
#include <sections.h>
#include <stddef.h>
#include <stdint.h>

#define page_align_up(x)   (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define page_align_down(x) ((x) & ~(PAGE_SIZE - 1))

void __early_init early_memset(void *dst, uint8_t value, size_t size) {
	uint8_t *_dst = dst;
	for (int i = 0; i < size; i++)
		*_dst++ = value;
}

void __early_init page_early_setup(void) {
	size_t *pml4 = (size_t *)PML4_BASE;
	size_t *pdpt = (size_t *)PDPT0_BASE;
	size_t *pdt	 = (size_t *)PDT0_BASE;

	// 重新配置页表
	pdt[0] = (0 | SIGN_HUGE | SIGN_SYS | SIGN_RW | SIGN_P);
	early_memset(&pdt[1], 0, PAGE_SIZE - sizeof(size_t));

	pdpt[0] = (PDT0_BASE | SIGN_SYS | SIGN_RW | SIGN_P);
	early_memset(&pdpt[1], 0, PAGE_SIZE - sizeof(size_t));

	pml4[0] = (PDPT0_BASE | SIGN_SYS | SIGN_RW | SIGN_P);
	early_memset(&pml4[1], 0, PAGE_SIZE - sizeof(size_t));

	pdpt = (size_t *)PDPT_KBASE;

	early_memset((void *)PDPT_KBASE, 0, PAGE_SIZE);
	pml4[511] = (PDPT_KBASE | SIGN_SYS | SIGN_RW | SIGN_P);

	early_memset((void *)PDT_KBASE, 0, PAGE_SIZE);

	// 以 2MB 为单位映射内核和预分配内存，直到 512MB
	int page;

	pdt = (size_t *)PDT_KBASE;

	size_t addr = 0;
	size_t _2mb = 512 * PAGE_SIZE;

	for (page = 0; page < 256; page++) {
		pdt[page] = (addr | SIGN_HUGE | SIGN_RW | SIGN_SYS | SIGN_P);
		addr += _2mb;
	}

	pdpt[510] = (PDT_KBASE | SIGN_SYS | SIGN_RW | SIGN_P);

	return;
}

void page_setup() {
	size_t *pdt = (size_t *)(0xffffffff80000000 + PDT0_BASE);
	pdt[0] &= ~SIGN_P;
}
