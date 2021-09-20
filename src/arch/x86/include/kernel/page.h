#ifndef	_PAGE_H
#define	_PAGE_H

#include <stdint.h>
#include <kernel/thread.h>

#define PDT_PHY_ADDR		0x201000	//页目录表物理地址

#define TBL_PHY_ADDR		0x202000
#define VRAM_PT_PHY_ADDR	0x203000
	
#define PAGE_SIZE			1024*4		//页大小

#define SIGN_P				0x01		//存在
#define SIGN_RW				0x02		//读写
#define SIGN_US				0x04		//普通/超级用户
#define SIGN_PWT			0x08		//页级写穿
#define SIGN_PCD			0x10		//页级高速缓存
#define SIGN_A				0x20		//访问
#define SIGN_D				0x40		//脏页
#define SIGN_PAT			0x80		//页表属性
#define	SIGN_G				0x100		//全局
#define SIGN_AVL			0x200		//软件可用

void init_page(void);
unsigned int *pte_ptr(unsigned int vaddr);
unsigned int *pde_ptr(unsigned int vaddr);
unsigned int vir2phy(unsigned int vaddr);
void *remap(unsigned int paddr, size_t size);
int alloc_vir_page(void);
int free_vir_page(int vir_addr);
void *kernel_alloc_page(int pages);
void kernel_free_page(int vaddr, int pages);
void fill_vir_page_table(int vir_address);
void clean_vir_page_table(int vir_address);
int alloc_mem_page(void);
int free_mem_page(int address);
void *thread_get_vaddr(uint32_t vaddr);
int thread_alloc_vir_page(struct task_s *thread);
int thread_free_vir_page(struct task_s *thread, uint32_t addr);
void *thread_alloc_page(struct task_s *thread, int pages);
void thread_free_page(struct task_s *thread, int vaddr, int pages);

#endif