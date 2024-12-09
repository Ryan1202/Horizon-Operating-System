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
#include <kernel/page.h>
#include <kernel/thread.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

extern struct VesaDisplayInfo vesa_display_info;

void setup_page(void) {
	uint32_t *pdt = (uint32_t *)PDT_PHY_ADDR;
	memset((void *)PDT_PHY_ADDR, 0, PAGE_SIZE); // 清空数据
	pdt[0] =
		(TBL_PHY_ADDR | SIGN_RW | SIGN_SYS |
		 SIGN_P); // 第0个页(前4MB内存):GDT、BIOS、内核主程序
	pdt[1023] =
		(PDT_PHY_ADDR | SIGN_RW | SIGN_SYS |
		 SIGN_P); // 第1023个页(最后4MB内存):页表

	// 映射低4MB内存
	uint32_t  i, j;
	uint32_t *pt   = (uint32_t *)TBL_PHY_ADDR;
	uint32_t  addr = (0x00 | SIGN_RW | SIGN_SYS | SIGN_P);
	// 一个页4KB，一个页表有1024个页表项
	for (i = 0; i < 1024; i++) {
		pt[i] = addr;
		addr += PAGE_SIZE;
	}

	pdt[1] = (DMA_PT_PHY_ADDR1 | SIGN_RW | SIGN_SYS | SIGN_P);
	pdt[2] = (DMA_PT_PHY_ADDR2 | SIGN_RW | SIGN_SYS | SIGN_P);
	pt	   = (uint32_t *)DMA_PT_PHY_ADDR1;
	addr   = (0x400000 | SIGN_RW | SIGN_SYS | SIGN_P);
	for (i = 0; i < 1024; i++) {
		pt[i] = addr;
		addr += PAGE_SIZE;
	}
	pt	 = (uint32_t *)DMA_PT_PHY_ADDR2;
	addr = (0x800000 | SIGN_RW | SIGN_SYS | SIGN_P);
	for (i = 0; i < 1024; i++) {
		pt[i] = addr;
		addr += PAGE_SIZE;
	}

	// VRAM
	uint32_t vram_addr = (uint32_t)vesa_display_info.vram;
	uint32_t size	   = vesa_display_info.width * vesa_display_info.height *
					(vesa_display_info.BitsPerPixel / 8);
	for (i = 1; i <= DIV_ROUND_UP(size, PAGE_SIZE * 1024); i++) {
		pdt[i] =
			((VRAM_PT_PHY_ADDR + (i - 1) * PAGE_SIZE) | SIGN_RW | SIGN_SYS |
			 SIGN_P);
		pt = (uint32_t *)(VRAM_PT_PHY_ADDR + ((i - 1) * PAGE_SIZE));
		addr =
			((vram_addr + (i - 1) * PAGE_SIZE * 1024) | SIGN_RW | SIGN_SYS |
			 SIGN_P);
		for (j = 0; j < 1024; j++) {
			pt[j] = addr;
			addr += PAGE_SIZE;
		}
	}

	write_cr3(pdt);
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
int alloc_vir_page(void) {
	int idx;
	int vir_addr;
	idx = mmap_search(&vir_page_mmap, 1);
	if (idx != -1) {
		mmap_set(&vir_page_mmap, idx, 1);
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
	int i;
	int vir_page_addr, vir_page_addr_more;

	int old_status = io_load_eflags();
	io_cli();

	vir_page_addr = alloc_vir_page(); // 分配一个虚拟地址的页

	fill_vir_page_table(
		vir_page_addr,
		SIGN_SYS); // 把页添加到当前页目录表系统中，使他可以被使用

	if (pages == 1) { // 如果只有一个页
		memset((void *)vir_page_addr, 0, PAGE_SIZE);

		io_store_eflags(old_status);
		return (void *)vir_page_addr;
	} else if (pages > 1) {
		for (i = 1; i < pages; i++) {
			vir_page_addr_more = alloc_vir_page(); // 分配一个虚拟地址的页
			fill_vir_page_table(
				vir_page_addr_more,
				SIGN_SYS); // 把页添加到当前页目录表系统中，使他可以被使用
		}
		memset((void *)vir_page_addr, 0, PAGE_SIZE * pages);
		io_store_eflags(old_status);

		return (void *)vir_page_addr;
	}
	return NULL;
}

/**
 * @brief 释放内核页
 *
 * @param vaddr 虚拟地址
 * @param pages 页数
 */
void kernel_free_page(int vaddr, int pages) {
	int i;
	int vir_page_addr = vaddr;

	int old_status = io_load_eflags();
	io_cli();

	free_vir_page(vir_page_addr);
	clean_vir_page_table(vir_page_addr);
	if (pages == 1) { // 如果只有一个页
		io_store_eflags(old_status);

		return;
	} else if (pages > 1) {
		for (i = 1; i < pages; i++) {
			vir_page_addr += PAGE_SIZE;
			free_vir_page(vir_page_addr);
			clean_vir_page_table(vir_page_addr);
		}
		io_store_eflags(old_status);

		return;
	}
}

/**
 * @brief 创建指定虚拟内存的页表项
 *
 * @param vaddr 虚拟内存地址
 * @param sign 是用户内存还是系统内存(SIGN_SYS:系统内存,SIGN_USER:用户内存)
 */
void fill_vir_page_table(uint32_t vaddr, uint8_t sign) {
	uint32_t *pde, *pte;
	pde = pde_ptr(vaddr);
	if (((*pde) & 0x00000001) != 0x00000001) { // 不存在页表
		uint32_t pt = alloc_mem_page();		   // 分配页表地址
		pt |= SIGN_RW | SIGN_P | (sign & SIGN_US);
		*pde = pt; // 填写页目录项为页表的地址
	}
	pte = pte_ptr(vaddr);
	if (((*pte) & 0x00000001) != 0x00000001) { // 不存在页表项
		uint32_t page = alloc_mem_page();	   // 分配页地址
		page |= SIGN_RW | SIGN_P | (sign & SIGN_US);
		*pte = page; // 填写页表项为页的地址
	}
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

/**
 * @brief 释放一个物理页
 *
 * @param address 物理地址
 * @return uint32_t 成功为0，失败为-1
 */
uint32_t free_mem_page(int address) {
	int addr = address;
	int idx;

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

	fill_vir_page_table(vaddr, SIGN_USER);
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
		vir_page_addr,
		SIGN_USER); // 把页添加到当前页目录表系统中，使他可以被使用

	if (pages == 1) { // 如果只有一个页
		memset((void *)vir_page_addr, 0, PAGE_SIZE);
		return (void *)vir_page_addr;
	} else if (pages > 1) {
		for (i = 1; i < pages; i++) {
			vir_page_addr_more =
				thread_alloc_vir_page(thread); // 分配一个虚拟地址的页
			fill_vir_page_table(
				vir_page_addr_more,
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
		if (((*pte) & 0x00000001) != 0x00000001) { // 不存在页表项
			*pte = addr | SIGN_P | SIGN_RW | SIGN_USER; // 填写页表项为页的地址
			addr += PAGE_SIZE;
		}

		unmap(pt, PAGE_SIZE);

		idx++;
		i++;
	}
	return MEMORY_RESULT_OK;
}
