#ifndef _MEMORY_H
#define _MEMORY_H

#include <stdint.h>
#include <stddef.h>

#define	PHY_MEM_BASE_ADDR	0x1000000
#define	PHY_MEM_MMAP		0x210000
#define	PHY_MEM_MMAP_SIZE	0x20000
#define VIR_MEM_BASE_ADDR	0x1000000
#define	VIR_MEM_MMAP		0x230000
#define	VIR_MEM_MMAP_SIZE	0x10000

#define KERN_VIR_MEM_BASE_ADDR  0x00000000
#define USER_VIR_MEM_BASE_ADDR  0x80000000

#define MEMORY_BLOCKS 0x1000
#define MEMORY_BLOCK_FREE 0			//内存信息块空闲
#define MEMORY_BLOCK_USING 1		//内存信息块使用中
#define MEMORY_BLOCK_MODE_SMALL 0	//小块内存描述1024一下的内存块
#define MEMORY_BLOCK_MODE_BIG 1		//大块内存描述4kb为单位的内存块

extern struct mmap phy_page_mmap;
extern struct mmap vir_page_mmap;

struct mmap
{
	int len;
	unsigned char *bits;
};

struct memory_block 
{
	unsigned int address;
	int size;
	int flags;
	int mode;
};

struct memory_manage
{
	struct memory_block free_blocks[MEMORY_BLOCKS];
};

void init_memory(void);
int get_memory_size(void);
int mmap_search(struct mmap *btmp, unsigned int cnt);
void mmap_set(struct mmap *btmp, unsigned int bit_index, int value);
unsigned long alloc_vaddr(size_t size);
void *kmalloc(uint32_t size);
int kfree(void *address);

#endif