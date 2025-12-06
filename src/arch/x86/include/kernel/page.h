#ifndef _PAGE_H
#define _PAGE_H

#include <kernel/thread.h>
#include <stdint.h>

#ifdef ARCH_X86
#define KERNEL_LINEAR_SIZE 0x20000000 // 512MB内核线性空间大小
#else
#error "Unsupported architecture"
#endif

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

typedef enum {
	PAGE_CACHE_WRITE_BACK	  = 0,
	PAGE_CACHE_WRITE_COMBINE  = 1,
	PAGE_CACHE_WRITE_THROUGH  = 2,
	PAGE_CACHE_UNCACHED		  = 3,
	PAGE_CACHE_UNCACHED_MINUS = 4,
} PageCacheType;

void		 setup_page(void);
uint32_t	*pte_ptr(uint32_t vaddr);
uint32_t	*pde_ptr(uint32_t vaddr);
uint32_t	 vir2phy(uint32_t vaddr);
MemoryResult remap(uint32_t in_paddr, size_t in_size, uint32_t *out_vaddr);
void		 unmap(uint32_t vaddr, size_t size);
int			 alloc_vir_pages(int count);
int			 free_vir_page(int vir_addr);
void		*kernel_alloc_pages(int pages);
void		*kernel_alloc_continuous_pages(int pages);
void		 kernel_free_page(int vaddr, int pages);
void		 fill_vir_page_table(uint32_t vaddr, uint32_t paddr, uint8_t sign);
void		 clean_vir_page_table(uint32_t vaddr);
uint32_t	 alloc_mem_page(void);
uint32_t	 alloc_mem_pages(int pages);
uint32_t	 free_mem_page(int address);
void		*thread_get_page(struct task_s *thread, uint32_t vaddr);
uint32_t	 thread_alloc_vir_page(struct task_s *thread);
uint32_t	 thread_free_vir_page(struct task_s *thread, uint32_t addr);
void		*thread_alloc_page(struct task_s *thread, int pages);
void		 thread_free_page(struct task_s *thread, uint32_t vaddr, int pages);
MemoryResult thread_use_page(
	struct task_s *thread, uint32_t vaddr, uint32_t addr, int pages);

void *ioremap(size_t paddr, size_t size, uint8_t cache_type);

extern size_t pdt_phy_addr;

#endif