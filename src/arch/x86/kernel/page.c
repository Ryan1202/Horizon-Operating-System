/**
 * @file page.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 分页管理
 * @version 1.2
 * @date 2022-07-15
 */
#include "math.h"
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

	// 将页表所在的页设置为只读，这样只能通过映射到最后4MB的页修改页表
	// 避免代码bug导致意外修改
	for (int i = 0; i < 1024; i++) {
		if (pdt[i] & SIGN_P && pdt[i] & 0xfffff000) {
			size_t phy_addr = pdt[i] & 0xfffff000;
			size_t vir_addr = (size_t)VIR_BASE + phy_addr;

			size_t *pte = pte_ptr(vir_addr);
			(*pte) &= ~SIGN_RW;
		}
	}
}

size_t alloc_page_table(void) {
	size_t page_table_phy = allocate_pages(ZONE_LINEAR, 0);
	if (!page_table_phy) {
		printk(COLOR_RED "kernel no page left!\n");
		return 0;
	}
	size_t page_table_vir = (size_t)VIR_BASE + page_table_phy; // 映射到线性区
	memset((void *)page_table_vir, 0, PAGE_SIZE);

	size_t *pte = pte_ptr(page_table_vir);
	*pte &= ~SIGN_RW;

	return page_table_phy;
}

bool page_link(
	size_t vaddr, size_t paddr, uint16_t page_count, uint8_t cache_type) {
	size_t *pde = pde_ptr(vaddr);
	size_t *pte = pte_ptr(vaddr);

	if (!(*pde & SIGN_P)) {
		size_t page_table_phy = alloc_page_table();
		if (!page_table_phy) { return false; }

		*pde = (page_table_phy | SIGN_RW | SIGN_SYS | SIGN_P);
	}

	if (*pte & SIGN_P) { return false; }

	uint8_t cache_sign = 0;
	switch (cache_type) {
	case PAGE_CACHE_WRITE_BACK:
		cache_sign = 0;
		break;
	case PAGE_CACHE_WRITE_COMBINE:
		// Write Combine需要配置PAT来支持，否则和Write Through一样
		cache_sign = SIGN_PWT | SIGN_PAT;
		break;
	case PAGE_CACHE_WRITE_THROUGH:
		cache_sign = SIGN_PWT;
		break;
	case PAGE_CACHE_UNCACHED:
		cache_sign = SIGN_PCD | SIGN_PWT;
		break;
	case PAGE_CACHE_UNCACHED_MINUS:
		cache_sign = SIGN_PCD;
		break;
	}

	for (int i = 0; i < page_count; i++) {
		*pte = (paddr | SIGN_RW | SIGN_SYS | SIGN_P | cache_sign);
		pte++;
		paddr += PAGE_SIZE;
		if (((size_t)pte & 0x00000fff) == 0) {
			// 跨页表了，获取下一个页表
			pde++;
			if (!(*pde & SIGN_P)) {
				size_t page_table = alloc_page_table();
				if (!page_table) { return false; }

				*pde = (page_table | SIGN_RW | SIGN_SYS | SIGN_P);
			}
			pte = (size_t *)((*pde & 0xfffff000) + (size_t)VIR_BASE);
		}
	}

	return true;
}

void page_unlink(size_t vaddr, size_t page_count) {
	size_t *pde = pde_ptr(vaddr);
	size_t *pte = pte_ptr(vaddr);

	for (int i = 0; i < page_count; i++) {
		*pte = 0;
		pte++;
		if (((size_t)pte & 0x00000fff) == 0) {
			// 跨页表了，获取下一个页表
			pde++;
			if (!(*pde & SIGN_P)) { return; }
			pte = (size_t *)((*pde & 0xfffff000) + (size_t)VIR_BASE);
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

/**
 * @brief 链接虚拟页和物理页
 *
 * @param va 虚拟页地址
 * @param pa 物理页地址
 * @param prot 标志
 * @return int 成功为0，失败为-1
 */
int __page_link(unsigned long va, unsigned long pa, unsigned long prot) {
	unsigned long vaddr = (unsigned long)va;
	unsigned long paddr = (unsigned long)pa;

	uint32_t *pde = pde_ptr(vaddr);
	uint32_t *pte = pte_ptr(vaddr);

	if (!(*pde & SIGN_P)) {
		uint32_t page_table = (uint32_t)kernel_alloc_pages(1);
		if (!page_table) {
			printk(COLOR_RED "kernel no page left!\n");
			return -1;
		}
		*pde = (page_table | prot | SIGN_P);
		memset((void *)((unsigned long)pte & 0xfffff000), 0, PAGE_SIZE);
	}
	*pte = (paddr | prot | SIGN_P);

	return 0;
}

/**
 * @brief 将物理地址映射到指定虚拟地址
 *
 * @param paddr 物理地址
 * @param vaddr 虚拟地址
 * @param size 大小
 * @return int 成功为0，失败为-1
 */
int __remap(uint32_t paddr, uint32_t vaddr, size_t size) {
	uint32_t end = vaddr + size;
	while (vaddr < end) {
		if (__page_link(vaddr, paddr, SIGN_RW)) return -1;
		vaddr += PAGE_SIZE;
		paddr += PAGE_SIZE;
	}
	return 0;
}

/**
 * @brief 取消映射
 *
 * @param vaddr 虚拟地址
 * @param size 大小
 * @return int 成功为0，失败为-1
 */
int __unmap(uint32_t vaddr, size_t size) {
	uint32_t end = vaddr + size;
	while (vaddr < end) {
		if (__page_link(vaddr, 0, 0)) return -1;
		vaddr += PAGE_SIZE;
	}
	return 0;
}

/**
 * @brief 映射物理地址到虚拟地址
 *
 * @param paddr 物理地址
 * @param size 大小
 * @param uint32_t* 虚拟地址
 */
MemoryResult remap(uint32_t in_paddr, size_t in_size, uint32_t *out_vaddr) {

	if (!in_paddr || !in_size) { return MEMORY_RESULT_INVALID_INPUT; }

	if (in_paddr + in_size < 0x400000) {
		// 低4MB内存默认已经映射，直接返回
		return in_paddr;
	}

	uint32_t vaddr;
	MEMORY_RESULT_DELIVER_CALL(alloc_vaddr, in_size, &vaddr);
	int old_status = io_load_eflags();
	io_cli();
	__remap(in_paddr, vaddr, in_size);
	io_store_eflags(old_status);

	io_sti();

	uint32_t ret = vaddr & 0xfffff000;
	ret |= (in_paddr & 0x0fff);
	*out_vaddr = ret;
	return MEMORY_RESULT_OK;
}

/**
 * @brief 取消映射
 *
 * @param vaddr 虚拟地址
 * @param size 大小
 */
void unmap(uint32_t vaddr, size_t size) {

	if (!vaddr || !size) { return; }
	__unmap(vaddr, size);
}

/**
 * @brief 分配虚拟页
 *
 * @return int 虚拟页地址
 */
int alloc_vir_pages(int count) {
	int idx;
	int vir_addr;
	idx = mmap_search(&vir_page_mmap, count);
	if (idx != -1) {
		for (int i = 0; i < count; i++) {
			mmap_set(&vir_page_mmap, idx + i, 1);
		}
	} else {
		return -1;
	}
	vir_addr = idx * 0x1000 + VIR_MEM_BASE_ADDR + KERN_VIR_MEM_BASE_ADDR;
	return vir_addr;
}

/**
 * @brief 释放虚拟页
 *
 * @param vir_addr 西你也地址
 * @return int 成功为0，失败为-1
 */
int free_vir_page(int vir_addr) {
	int idx;
	idx = (vir_addr - VIR_MEM_BASE_ADDR - KERN_VIR_MEM_BASE_ADDR) / 0x1000;
	if (vir_page_mmap.bits[idx / 8] & (1 << (idx % 8))) {
		mmap_set(&vir_page_mmap, idx, 0);
	} else {
		return -1;
	}
	return 0;
}

/**
 * @brief 分配一个内核页
 *
 * @param pages 页数
 * @return void* 起始地址
 */
void *kernel_alloc_pages(int pages) {
	int order = aligned_up_log2n(pages);
	return (void *)((size_t)VIR_BASE + allocate_pages(ZONE_LINEAR, order));
	// int i;
	// int vir_page_addr, vir_page_addr_more;

	// int old_status = io_load_eflags();
	// io_cli();

	// vir_page_addr = alloc_vir_pages(pages); // 分配一个虚拟地址的页
	// if (vir_page_addr < 0) return NULL;
	// fill_vir_page_table(
	// 	vir_page_addr, alloc_mem_page(),
	// 	SIGN_SYS); // 把页添加到当前页目录表系统中，使他可以被使用

	// vir_page_addr_more = vir_page_addr + PAGE_SIZE; // 分配一个虚拟地址的页
	// for (i = 1; i < pages; i++) {
	// 	fill_vir_page_table(
	// 		vir_page_addr_more, alloc_mem_page(),
	// 		SIGN_SYS); // 把页添加到当前页目录表系统中，使他可以被使用
	// 	vir_page_addr_more += PAGE_SIZE;
	// }

	// memset((void *)vir_page_addr, 0, PAGE_SIZE * pages);
	// io_store_eflags(old_status);

	// return (void *)vir_page_addr;
}

/**
 * @brief 分配物理地址连续的页
 *
 * @param pages 页数
 * @return void* 起始地址
 */
void *kernel_alloc_continuous_pages(int pages) {
	size_t vir_page_addr = 0;

	int old_status = io_load_eflags();
	io_cli();

	vir_page_addr = alloc_vir_pages(pages);

	size_t paddr = alloc_mem_pages(pages);
	for (int i = 0; i < pages; i++) {
		fill_vir_page_table(
			vir_page_addr + i * PAGE_SIZE, paddr + i * PAGE_SIZE,
			SIGN_SYS); // 把页添加到当前页目录表系统中，使他可以被使用
	}
	if (!paddr) return NULL;
	io_store_eflags(old_status);

	return (void *)vir_page_addr;
}

/**
 * @brief 释放内核页
 *
 * @param vaddr 虚拟地址
 * @param pages 页数
 */
int kernel_free_page(int vaddr, int pages) {
	size_t paddr = vir2phy(vaddr);
	return free_pages(paddr);
	// int i;
	// int vir_page_addr = vaddr;

	// int old_status = io_load_eflags();
	// io_cli();

	// free_vir_page(vir_page_addr);
	// clean_vir_page_table(vir_page_addr);
	// if (pages == 1) { // 如果只有一个页
	// 	io_store_eflags(old_status);

	// 	return;
	// } else if (pages > 1) {
	// 	for (i = 1; i < pages; i++) {
	// 		vir_page_addr += PAGE_SIZE;
	// 		free_vir_page(vir_page_addr);
	// 		clean_vir_page_table(vir_page_addr);
	// 	}
	// 	io_store_eflags(old_status);

	// 	return;
	// }
}

/**
 * @brief 创建指定虚拟内存的页表项
 *
 * @param vaddr 虚拟内存地址
 * @param sign 是用户内存还是系统内存(SIGN_SYS:系统内存,SIGN_USER:用户内存)
 */
void fill_vir_page_table(uint32_t vaddr, uint32_t paddr, uint8_t sign) {
	uint32_t *pde, *pte;
	pde = pde_ptr(vaddr);
	if (((*pde) & 0x00000001) != 0x00000001) { // 不存在页表
		uint32_t pt = alloc_mem_page();		   // 分配页表地址
		pt |= SIGN_RW | SIGN_P | (sign & SIGN_US);
		*pde = pt; // 填写页目录项为页表的地址
	}
	pte			  = pte_ptr(vaddr);
	uint32_t page = paddr;
	page |= SIGN_RW | SIGN_P | (sign & SIGN_US);
	*pte = page; // 填写页表项为页的地址
}

/**
 * @brief 删除指定虚拟内存的页表项
 *
 * @param vaddr 虚拟内存地址
 */
void clean_vir_page_table(uint32_t vaddr) {
	uint32_t *pte;
	uint32_t  page_phy_addr;
	pte			  = pte_ptr(vaddr);
	page_phy_addr = *pte;		  // 获得页表项中页的物理地址
	*pte		  = 0;			  // 清空页表项
	page_phy_addr &= 0xfffff000;  // 保留高22位，对齐
	free_mem_page(page_phy_addr); // 释放对应的物理页
}

/**
 * @brief 分配一个物理页
 *
 * @return uint32_t 成功为物理页地址，失败为NULL
 */
uint32_t alloc_mem_page(void) {
	int idx;
	int mem_addr;
	idx = mmap_search(&phy_page_mmap, 1);
	if (idx != -1) {
		mmap_set(&phy_page_mmap, idx, 1);
	} else {
		return (int)NULL;
	}
	mem_addr = idx * 0x1000 + PHY_MEM_BASE_ADDR;

	return mem_addr;
}

uint32_t alloc_mem_pages(int pages) {
	int addr = 0;
	int idx;
	int mem_addr;
	idx = mmap_search(&phy_page_mmap, pages);
	if (idx != -1) {
		for (int i = 0; i < pages; i++) {
			mmap_set(&phy_page_mmap, idx + i, 1);
		}
	} else {
		return (int)NULL;
	}
	mem_addr = idx * 0x1000 + PHY_MEM_BASE_ADDR;

	return mem_addr;
	return addr;
}

/**
 * @brief 释放一个物理页
 *
 * @param address 物理地址
 * @return uint32_t 成功为0，失败为-1
 */
uint32_t free_mem_page(int address) {
	int addr = address;
	int idx;

	if (addr < PHY_MEM_BASE_ADDR) { return -1; }
	idx = (addr - PHY_MEM_BASE_ADDR) / 0x1000;
	if (phy_page_mmap.bits[idx / 8] & (1 << idx % 8)) {
		mmap_set(&phy_page_mmap, idx, 0);
	} else {
		return -1;
	}
	return 0;
}

/**
 * @brief 为虚拟地址分配页
 *
 * @param thread 线程
 * @param vaddr 虚拟地址
 * @return void* 虚拟地址
 */
void *thread_get_page(struct task_s *thread, uint32_t vaddr) {
	int idx = -1;
	idx		= (vaddr - USER_VIR_MEM_BASE_ADDR) / PAGE_SIZE;

	mmap_set(&thread->vir_page_mmap, idx, 1);

	fill_vir_page_table(vaddr, alloc_mem_page(), SIGN_USER);
	return (void *)vaddr;
}

/**
 * @brief 在进程的内存空间中分配一个虚拟页
 *
 * @param thread 要分配虚拟页的进程
 * @return uint32_t 分配的虚拟地址
 */
uint32_t thread_alloc_vir_page(struct task_s *thread) {
	int idx;
	idx = mmap_search(&thread->vir_page_mmap, 1);
	if (idx != -1) {
		mmap_set(&thread->vir_page_mmap, idx, 1);
	} else {
		return -1;
	}
	return idx * PAGE_SIZE + USER_VIR_MEM_BASE_ADDR;
}

/**
 * @brief 释放线程中的虚拟页
 *
 * @param thread 线程
 * @param addr 地址
 * @return uint32_t 成功为0，失败为-1
 */
uint32_t thread_free_vir_page(struct task_s *thread, uint32_t addr) {
	int idx;
	idx = (addr - USER_VIR_MEM_BASE_ADDR) / PAGE_SIZE;
	if (thread->vir_page_mmap.bits[idx / 8] & (1 << (idx % 8))) {
		mmap_set(&thread->vir_page_mmap, idx, 0);
	} else {
		return -1;
	}
	return 0;
}

/**
 * @brief 为线程分配页
 *
 * @param thread 线程
 * @param pages 页数
 * @return void* 页地址
 */
void *thread_alloc_page(struct task_s *thread, int pages) {
	int i;
	int vir_page_addr, vir_page_addr_more;

	vir_page_addr = thread_alloc_vir_page(thread); // 分配一个虚拟地址的页

	fill_vir_page_table(
		vir_page_addr, alloc_mem_page(),
		SIGN_USER); // 把页添加到当前页目录表系统中，使他可以被使用

	if (pages == 1) { // 如果只有一个页
		memset((void *)vir_page_addr, 0, PAGE_SIZE);
		return (void *)vir_page_addr;
	} else if (pages > 1) {
		for (i = 1; i < pages; i++) {
			vir_page_addr_more =
				thread_alloc_vir_page(thread); // 分配一个虚拟地址的页
			fill_vir_page_table(
				vir_page_addr_more, alloc_mem_page(),
				SIGN_USER); // 把页添加到当前页目录表系统中，使他可以被使用
		}
		memset((void *)vir_page_addr, 0, PAGE_SIZE * pages);
		return (void *)vir_page_addr;
	}
	return NULL;
}

/**
 * @brief 释放线程中的页
 *
 * @param thread 线程
 * @param vaddr 虚拟地址
 * @param pages 页数
 */
void thread_free_page(struct task_s *thread, uint32_t vaddr, int pages) {
	int i;
	int vir_page_addr = vaddr;
	thread_free_vir_page(thread, vir_page_addr);
	clean_vir_page_table(vir_page_addr);
	if (pages == 1) { // 如果只有一个页
		return;
	} else if (pages > 1) {
		for (i = 1; i < pages; i++) {
			vir_page_addr += PAGE_SIZE;
			thread_free_vir_page(thread, vir_page_addr);
			clean_vir_page_table(vir_page_addr);
		}
		return;
	}
}

/**
 * @brief 将开辟好的内存映射到线程空间
 *
 * @param thread 载入的线程
 * @param vaddr 载入的地址
 * @param addr	内存的物理地址
 * @param pages 载入内存的大小(单位：页)
 *
 * @return MemoryResult
 */
MemoryResult thread_use_page(
	struct task_s *thread, uint32_t vaddr, uint32_t addr, int pages) {
	int i	= 0;
	int idx = (vaddr - USER_VIR_MEM_BASE_ADDR) / PAGE_SIZE;
	while (i < pages) {
		if (mmap_get(&thread->vir_page_mmap, idx)) {
			return MEMORY_RESULT_MEMORY_IS_USED;
		}
		mmap_set(&thread->vir_page_mmap, idx, 1);

		uint32_t *pde, *pte;
		// pde = thread->pgdir + (((vaddr & 0xffc00000) >> 10) + ((vaddr &
		// 0x003ff000)>>12) * 4);
		pde			= (uint32_t *)(((uint32_t)thread->pgdir) +
						   ((vaddr & 0xffc00000) >> 22) * 4);
		uint32_t pt = *pde;
		if ((pt & 0x00000001) != 0x00000001) { // 不存在页表
			pt = alloc_mem_page();			   // 分配页表地址
			*pde =
				pt | SIGN_P | SIGN_RW | SIGN_USER; // 填写页目录项为页表的地址
		}

		MEMORY_RESULT_DELIVER_CALL(remap, pt & 0xfffff000, PAGE_SIZE, &pt);

		pte = (uint32_t *)(pt + ((vaddr & 0x003ff000) >> 12) * 4);
		if (((*pte) & 0x00000001) != 0x00000001) {		// 不存在页表项
			*pte = addr | SIGN_P | SIGN_RW | SIGN_USER; // 填写页表项为页的地址
			addr += PAGE_SIZE;
		}

		unmap(pt, PAGE_SIZE);

		idx++;
		i++;
	}
	return MEMORY_RESULT_OK;
}
