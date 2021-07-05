#include <kernel/ards.h>
#include <kernel/memory.h>
#include <kernel/console.h>
#include <kernel/page.h>
#include <string.h>
#include <math.h>

struct ards *ards;
struct mmap phy_page_mmap;
struct mmap vir_page_mmap;
struct memory_manage *memory_manage;

uint32_t memory_total_size;

void init_memory(void)
{
	uint16_t ards_nr =  *((uint16_t *)ARDS_NR);	//ards 结构数
	ards = (struct ards *) ARDS_ADDR;	//ards 地址
	int i;
	for(i = 0; i < ards_nr; i++){
		//寻找可用最大内存
		if(ards->type == 1){
			//冒泡排序获得最大内存
			if(ards->base_low+ards->length_low > memory_total_size){
				memory_total_size = ards->base_low+ards->length_low;
			}
		}	
		ards++;
	}
	
	int page_bytes = (memory_total_size-PHY_MEM_BASE_ADDR)/(PAGE_SIZE*8);
	
	phy_page_mmap.bits = (unsigned char *)PHY_MEM_MMAP;
	phy_page_mmap.len = page_bytes;
	
	vir_page_mmap.bits = (unsigned char *)VIR_MEM_MMAP;
	vir_page_mmap.len = PHY_MEM_MMAP_SIZE-PHY_MEM_BASE_ADDR/(PAGE_SIZE*8);
	
	memset(phy_page_mmap.bits, 0, phy_page_mmap.len);
	memset(vir_page_mmap.bits, 0, vir_page_mmap.len);
	
	unsigned int memory_manage_pages = (sizeof(struct memory_manage) + PAGE_SIZE - 1) / PAGE_SIZE;
	memory_manage = (struct memory_manage *)kernel_alloc_page(memory_manage_pages);
	memset(memory_manage, 0, memory_manage_pages*PAGE_SIZE);
	for(i = 0; i < MEMORY_BLOCKS; i++){	
		memory_manage->free_blocks[i].size = 0;	//大小是页的数量
		memory_manage->free_blocks[i].flags = 0;
	}
}

int get_memory_size(void)
{
	return memory_total_size / 1024 / 1024;
}

int mmap_search(struct mmap *btmp, unsigned int cnt)
{
	int index_byte = 0;
	int index_bit = 0;
	while ((btmp->bits[index_byte] == 0xff) && (index_byte < btmp->len))
	{
		index_byte++;
	}
	if(index_byte == btmp->len)
	{
		return -1;
	}
	
	while ((unsigned char)(1<<index_bit) & btmp->bits[index_byte])
	{
		index_bit++;
	}
	int index_start = index_byte * 8 + index_bit;
	if(cnt == 1)
	{
		return index_start;
	}
	
	int bit_left = btmp->len * 8 - index_start;
	int next_bit = bit_left + 1;
	int count = 0;
	while (bit_left-- > 0)
	{
		if (!(btmp->bits[next_bit / 8] & 1<<(next_bit % 8)))
		{
			count++;
		} else
		{
			count = 0;
		}
		if(count == cnt)
		{
			return next_bit - cnt + 1;
		}
		next_bit++;
	}
	return index_start;
}

void mmap_set(struct mmap *btmp, unsigned int bit_index, int value)
{
	unsigned int byte_idx = bit_index / 8;    // 向下取整用于索引数组下标
	unsigned int bit_odd  = bit_index % 8;    // 取余用于索引数组内的位

/* 一般都会用个0x1这样的数对字节中的位操作,
 * 将1任意移动后再取反,或者先取反再移位,可用来对位置0操作。*/
   if (value) {		      // 如果value为1
      btmp->bits[byte_idx] |= (1 << bit_odd);
   } else {		      // 若为0
      btmp->bits[byte_idx] &= ~(1 << bit_odd);
   }
}

unsigned long alloc_vaddr(size_t size)
{
	size = ((size + PAGE_SIZE - 1) & ~(PAGE_SIZE-1));
	if (!size)
		return 0;
	
	int pages = size / PAGE_SIZE;

	/* 扫描获取请求的页数 */
	int idx = mmap_search(&vir_page_mmap, pages);
	if (idx == -1)
		return 0;

	int i;
	/* 把已经扫描到的位置1，表明已经分配了 */
	for (i = 0; i < pages; i++) {
		mmap_set(&vir_page_mmap, idx + i, 1);
	}

	/* 返还转换好的虚拟地址 */
	return 0x10000 + idx * PAGE_SIZE; 
}

void *kmalloc(uint32_t size)
{
	int i;
	uint32_t address;
	uint32_t break_size;//要打碎成什么大小
	uint32_t break_cnt;//要打碎成几块
	void *new_address;
	
	//大于1024字节就用页
	if(size >= 2048){
		int pages = DIV_ROUND_UP(size, PAGE_SIZE);	//一共占多少个页
		for(i = 0; i < MEMORY_BLOCKS; i++){
			if(memory_manage->free_blocks[i].flags == MEMORY_BLOCK_FREE){	//找到
				address = (uint32_t )kernel_alloc_page(pages);	//分配页
				memory_manage->free_blocks[i].address = address;	
				memory_manage->free_blocks[i].size = pages;	//大小是页的数量
				memory_manage->free_blocks[i].flags = MEMORY_BLOCK_USING;
				memory_manage->free_blocks[i].mode = MEMORY_BLOCK_MODE_BIG;
				//printk("Found pages ");
				//printk("Alloc:%x idx:%d\n", address,i);
				return (void *)address;
			}
		}
	}else if(0 < size &&size <= 2048){	//size <= 2048
		//对齐判断，要打散成多大
		if(0 < size && size <= 32){
			break_size = 32;
		}else if(32 < size && size <= 64){
			break_size = 64;
		}else if(64 < size && size <= 128){
			break_size = 128;
		}else if(128 < size && size <= 256){
			break_size = 256;
		}else if(256 < size && size <= 512){
			break_size = 512;
		}else if(512 < size && size <= 1024){
			break_size = 1024;
		}else if(1024 < size && size <= 2048){
			break_size = 2048;
		}
		//第一次寻找，如果在块中没有找到，就打散一个页
		for(i = 0; i < MEMORY_BLOCKS; i++){
			if(memory_manage->free_blocks[i].size == break_size && memory_manage->free_blocks[i].flags == MEMORY_BLOCK_FREE){	//找到
				address = memory_manage->free_blocks[i].address;
				memory_manage->free_blocks[i].flags = MEMORY_BLOCK_USING;
				//printk("Found broken ");
				//printk("Alloc:%x idx:%d\n", address,i);
				return (void *)address;
			}
		}
		//如果都没有找到，分配一个页，然后打散
		//分配一个页，用来被打散
		new_address = kernel_alloc_page(1);
		break_cnt = PAGE_SIZE/break_size;
		
		//打散成break_cnt个
		for(i = 0; i < MEMORY_BLOCKS; i++){
			if(memory_manage->free_blocks[i].flags == MEMORY_BLOCK_FREE){	//找到一个可以被使用的
				//地址增加
				
				//设置最终地址
				memory_manage->free_blocks[i].address = (uint32_t)new_address;
				new_address += break_size;
				//设置size
				memory_manage->free_blocks[i].size = break_size;
				//设置为可以分配
				memory_manage->free_blocks[i].flags = MEMORY_BLOCK_FREE;
				//设置为小块模式
				memory_manage->free_blocks[i].mode = MEMORY_BLOCK_MODE_SMALL;
				break_cnt--;
				if(break_cnt <= 0){
					break;
				}
			}
		}
		//打散后的寻找
		for(i = 0; i < MEMORY_BLOCKS; i++){
			if(memory_manage->free_blocks[i].size == break_size && memory_manage->free_blocks[i].flags == MEMORY_BLOCK_FREE){	//找到
				address = memory_manage->free_blocks[i].address;
				memory_manage->free_blocks[i].flags = MEMORY_BLOCK_USING;
				//printk("Found new broken ");
				//printk("Alloc:%x idx:%d\n", address,i);
				return (void *)address;
			}
		}
	}
	//size=0或者没有找到
	return NULL;	//失败
}

int kfree(void *address)
{
	int i;
	uint32_t addr = (uint32_t )address;
	for(i = 0; i < MEMORY_BLOCKS; i++){
		if(memory_manage->free_blocks[i].address == addr && memory_manage->free_blocks[i].flags == MEMORY_BLOCK_USING){	//找到
			if(memory_manage->free_blocks[i].mode == MEMORY_BLOCK_MODE_BIG){
				kernel_free_page(memory_manage->free_blocks[i].address, memory_manage->free_blocks[i].size);
				memory_manage->free_blocks[i].size = 0;		//只有大块才需要重新设置size
			}else if(memory_manage->free_blocks[i].mode == MEMORY_BLOCK_MODE_SMALL){
				//小块内存就清空就是了
				memset((void *)memory_manage->free_blocks[i].address, 0, memory_manage->free_blocks[i].size);
				//存在一种情况，那就是所有被打散的内存都被释放后，可能需要释放那个页，目前还没有考虑它
				//小块不需要设置大小，因为就是打散了的块
			}
			memory_manage->free_blocks[i].flags = MEMORY_BLOCK_FREE;
			
			//printk("Free:%x idx:%d\n", address,i);
			return 0;
		}
	}
	
	return -1;	//失败
}
