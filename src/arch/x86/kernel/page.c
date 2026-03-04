/**
 * @file page.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 分页管理
 * @version 1.2
 * @date 2022-07-15
 */
#include <drivers/vesa_display.h>
#include <kernel/console.h>
#include <kernel/func.h>
#include <kernel/memory.h>
#include <kernel/memory/block.h>
#include <kernel/page.h>
#include <kernel/thread.h>
#include <sections.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define page_align_up(x)   (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define page_align_down(x) ((x) & ~(PAGE_SIZE - 1))

extern struct VesaDisplayInfo vesa_display_info;

extern void	 *_kernel_start_phy, *_kernel_start_vir;
extern void	 *_kernel_end_phy, *_kernel_end_vir;
extern size_t PREALLOCATED_END_PHY;
extern size_t VIR_BASE[];

size_t pdt_phy_addr;
size_t kernel_start_vir;

void __early_init early_memset(void *dst, uint8_t value, size_t size) {
	uint8_t *_dst = dst;
	for (int i = 0; i < size; i++)
		*_dst++ = value;
}

size_t __early_init page_early_setup(void) {
	size_t pdt_addr =
		((((size_t)&_kernel_end_phy) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1));
	size_t pt_addr_start = pdt_addr + PAGE_SIZE;

	size_t *pdt = (size_t *)pdt_addr;
	early_memset(pdt, 0, PAGE_SIZE);
	for (int i = 0; i < PAGE_SIZE / sizeof(size_t) - 1; i++) {
		pdt[i] = 0;
	}

	pdt[1023] =
		(pdt_addr | SIGN_RW | SIGN_SYS |
		 SIGN_P); // 第1023个页(最后4MB内存):页表

	// 第0个页(前4MB内存):GDT、BIOS
	size_t *pt = (size_t *)pt_addr_start;
	pdt[0]	   = ((size_t)pt | SIGN_RW | SIGN_SYS | SIGN_P);

	pt[0] = (0 | SIGN_RW | SIGN_SYS) &
			~SIGN_P; // 0x00000000-0x00000fff设为不存在，暴露NULL指针引发的问题
	size_t addr = PAGE_SIZE;
	for (int i = 1; i < PAGE_SIZE / sizeof(size_t); i++) {
		pt[i] = addr | SIGN_RW | SIGN_SYS | SIGN_P;
		addr += PAGE_SIZE;
	}

	// 数学上:
	// 当 pages <= 1024
	//    pages                   = size / PAGE_SIZE + pages / PAGE_SIZE + 1
	// => pages *  PAGE_SIZE      = size + PAGE_SIZE + pages
	// => pages * (PAGE_SIZE - 1) = size + PAGE_SIZE
	// => pages                   = (size + PAGE_SIZE) / (PAGE_SIZE - 1)

	int page;
	int kernel_start_page = (((size_t)&_kernel_start_vir) >> 12) & ~0x3ff;
	int kernel_end_page	  = (((size_t)&_kernel_end_vir) + PAGE_SIZE - 1) >> 12;
	addr				  = (size_t)&_kernel_start_phy & ~0x3fffff;

	for (page = kernel_start_page; page < kernel_end_page; page++) {
		if ((page & ((PAGE_SIZE) / sizeof(size_t) - 1)) == 0) {
			pt = (size_t *)((size_t)pt + PAGE_SIZE);
			early_memset(pt, 0, PAGE_SIZE);
			pdt[page >> 10] = (size_t)pt | SIGN_RW | SIGN_SYS | SIGN_P;
		}
		int num = page & ((PAGE_SIZE) / sizeof(size_t) - 1);
		pt[num] = (addr | SIGN_RW | SIGN_SYS | SIGN_P);
		addr += PAGE_SIZE;
	}

	size_t *preallocated_end =
		(size_t *)((size_t)&PREALLOCATED_END_PHY - (size_t)VIR_BASE);
	*preallocated_end = (size_t)pt + PAGE_SIZE;
	return (size_t)pdt;
}

void setup_page(void) {
	pdt_phy_addr =
		((((size_t)&_kernel_end_phy) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1));
	// 物理地址->虚拟地址映射关系:
	// 0x00000000-0x00400000 => 0x00000000-0x00400000
	// 0x00000000-0x37ffffff => 0xc0000000-0xf7ffffff

	// 启动时只映射内核部分内存和预分配内存
	size_t end = KERNEL_LINEAR_SIZE;

	size_t *pdt		= (size_t *)0xfffff000;
	int		pdt_off = ((size_t)VIR_BASE) >> 22;

	size_t kernel_end = page_align_up((size_t)&_kernel_end_phy);
	// 预分配内存占用的页数
	size_t page_delta = (end - kernel_end) >> 12;

	// 由于需要计算要额外分配的页表数，
	// 所以需要向上对齐到页目录表项，忽略已分配过的最后一个页表
	size_t end_pdt		  = ((end - 1) + 0x003fffff) >> 22;
	size_t kernel_end_pdt = (kernel_end + 0x003fffff) >> 22;

	// 管理预分配内存所需的页目录表项数，
	// 一个页目录表项对应一个页表，一个页表需要额外分配一个页
	size_t pdt_delta = end_pdt - kernel_end_pdt;

	int page_start = kernel_end >> 12;
	int page_end   = page_start + page_delta + pdt_delta;

	size_t start_addr = early_allocate_pages(pdt_delta);
	size_t addr		  = start_addr;

	int		pdt_num;
	int		pt_num;
	int		page = page_start;
	size_t *pt, *pde;

	for (pdt_num = kernel_end_pdt - 1; pdt_num < end_pdt; pdt_num++) {
		pde = (size_t *)&pdt[pdt_off + pdt_num];
		pt	= (size_t *)(0xffc00000 + ((pdt_off + pdt_num) << 12));
		if (!(*pde & 0xfffff000)) {
			*pde = ((size_t)addr | SIGN_RW | SIGN_SYS | SIGN_P);
			addr += PAGE_SIZE;
		}
		pt_num = (page & 0x3ff);
		for (int i = pt_num; i < 1024 && page < page_end; i++, page++) {
			pt[i] = (page << 12 | SIGN_RW | SIGN_SYS | SIGN_P);
		}
	}

	// 将页表所在的页设置为不存在，这样只能通过映射到最后4MB的页修改页表
	// 避免代码bug导致意外修改
	for (int i = 0; i < 1024; i++) {
		if (pdt[i] & SIGN_P && pdt[i] & 0xfffff000) {
			size_t phy_addr = pdt[i] & 0xfffff000;
			size_t vir_addr = (size_t)VIR_BASE + phy_addr;

			size_t *pte = pte_ptr(vir_addr);
			(*pte) &= ~SIGN_P;
		}
	}
}

/**
 * @brief 获取页所在的页表地址
 *
 * @param vaddr 虚拟地址
 * @return uint32_t* 页表地址
 */
uint32_t *pte_ptr(uint32_t vaddr) {
	uint32_t *pte =
		(uint32_t *)(0xffc00000 + (((vaddr & 0xffc00000) >> 10) +
								   ((vaddr & 0x003ff000) >> 12) * 4));
	return pte;
}

/**
 * @brief 获取页所在的页目录表地址
 *
 * @param vaddr 虚拟地址
 * @return uint32_t* 页目录表地址
 */
uint32_t *pde_ptr(uint32_t vaddr) {
	uint32_t *pde =
		(uint32_t *)((0xfffff000) + ((vaddr & 0xffc00000) >> 22) * 4);
	return pde;
}

/**
 * @brief 虚拟地址转物理地址
 *
 * @param vaddr 虚拟地址
 * @return uint32_t 物理地址
 */
uint32_t vir2phy(uint32_t vaddr) {
	uint32_t *pte = pte_ptr(vaddr);
	uint32_t  phy_addr =
		(uint32_t)(*pte & 0xfffff000); // 获取页表物理地址并去除属性
	phy_addr += vaddr & 0x00000fff;	   // 加上页表内偏移地址
	return (uint32_t)phy_addr;
}
