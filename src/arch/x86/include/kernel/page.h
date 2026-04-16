#ifndef _PAGE_H
#define _PAGE_H

#include <kernel/thread.h>
#include <stdint.h>

#ifdef ARCH_X86
#define KERNEL_LINEAR_SIZE 0x20000000 // 512MB内核线性空间大小
#else
#error "Unsupported architecture"
#endif

#define PML4_BASE  0x3000
#define PDPT0_BASE 0x4000
#define PDT0_BASE  0x5000
#define PDPT_KBASE 0x6000

#define PDT_KBASE 0x7000

#define PAGE_SIZE 1024 * 4 // 页大小

#define SIGN_P	0x01 // 存在
#define SIGN_RW 0x02 // 读写

#define SIGN_US	  0x04 // 普通/超级用户
#define SIGN_USER 0x04 // 用户内存
#define SIGN_SYS  0x00 // 系统内存

#define SIGN_PWT 0x08  // 页级写穿
#define SIGN_PCD 0x10  // 页级高速缓存
#define SIGN_A	 0x20  // 访问
#define SIGN_D	 0x40  // 脏页
#define SIGN_PAT 0x80  // 页表属性
#define SIGN_G	 0x100 // 全局
#define SIGN_AVL 0x200 // 软件可用

#define SIGN_HUGE (1 << 7)

typedef enum {
	PAGE_CACHE_WRITE_BACK	  = 0,
	PAGE_CACHE_WRITE_COMBINE  = 1,
	PAGE_CACHE_WRITE_THROUGH  = 2,
	PAGE_CACHE_UNCACHED		  = 3,
	PAGE_CACHE_UNCACHED_MINUS = 4,
} PageCacheType;

size_t		 vir2phy(size_t vaddr);
MemoryResult remap(size_t in_paddr, size_t in_size, size_t *out_vaddr);
void		 unmap(size_t vaddr, size_t size);
void		*kmalloc_pages(int pages);
int			 kfree_pages(size_t vaddr);

void assign_frames(size_t paddr, size_t page_cnt);

// void		*thread_get_page(struct task_s *thread, uint32_t vaddr);
// uint32_t	 thread_alloc_vir_page(struct task_s *thread);
// uint32_t	 thread_free_vir_page(struct task_s *thread, uint32_t addr);
// void		*thread_alloc_page(struct task_s *thread, int pages);
// void		 thread_free_page(struct task_s *thread, uint32_t vaddr, int pages);
// MemoryResult thread_use_page(
// 	struct task_s *thread, uint32_t vaddr, uint32_t addr, int pages);

void *ioremap(size_t paddr, size_t size, uint8_t cache_type);

#endif
