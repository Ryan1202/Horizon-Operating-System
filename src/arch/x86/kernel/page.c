#include <kernel/page.h>
#include <kernel/func.h>
#include <kernel/memory.h>
#include <kernel/console.h>
#include <device/video.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

void init_page(void)
{
	unsigned int *pdt = (unsigned int *)PDT_PHY_ADDR;
	memset((void *)PDT_PHY_ADDR, 0, PAGE_SIZE);				//清空数据
	pdt[0] = (TBL_PHY_ADDR | SIGN_US | SIGN_RW | SIGN_P);
	pdt[1023] = (PDT_PHY_ADDR | SIGN_US | SIGN_RW | SIGN_P);
	
	int i, j;
	unsigned int *pt = (unsigned int *)TBL_PHY_ADDR;
	unsigned int addr = (0x00 | SIGN_US | SIGN_RW | SIGN_P);
	for(i = 0; i < 1024; i++)
	{
		pt[i] = addr;
		addr += PAGE_SIZE;
	}
	
	//VRAM
	unsigned int *vram_addr = (unsigned int *)(VIDEO_INFO_ADDR + 6);
	unsigned int size = (*(unsigned short *)(VIDEO_INFO_ADDR + 0)) * (*(unsigned short *)(VIDEO_INFO_ADDR + 2)) * (*(unsigned int *)(VIDEO_INFO_ADDR + 4)/8);
	for (i = 1; i <= DIV_ROUND_UP(size, PAGE_SIZE * 1024); i++)
	{
		pdt[i] = (VRAM_PT_PHY_ADDR + (i - 1)*PAGE_SIZE | SIGN_US | SIGN_RW | SIGN_P);
		pt = (unsigned int *)(VRAM_PT_PHY_ADDR + ((i - 1)*PAGE_SIZE));
		addr = (*vram_addr + (i - 1)*PAGE_SIZE*1024 | SIGN_US | SIGN_RW | SIGN_P);
		for(j = 0; j < 1024; j++)
		{
			pt[j] = addr;
			addr += PAGE_SIZE;
		}
	}
	

	
	//设置页列表项起始地址
	write_cr3(pdt);
	//启用分页机制
	unsigned int cr0;
	cr0 = read_cr0();
	cr0 |= CR0_PG;
	write_cr0(cr0);
}

unsigned int *pte_ptr(unsigned int vaddr)
{
	unsigned int *pte = (unsigned int *)(0xffc00000 + (((vaddr & 0xffc00000) >> 10) + ((vaddr & 0x003ff000)>>12) * 4));
	return pte;
}

unsigned int *pde_ptr(unsigned int vaddr)
{
	unsigned int *pde = (unsigned int *)((0xfffff000) + ((vaddr & 0xffc00000)>>22) * 4);
	return pde;
}

unsigned int vir2phy(unsigned int vaddr)
{
	unsigned int *pte = pte_ptr(vaddr);
	unsigned int *phy_addr = (unsigned int *)((*pte + (vaddr & 0x003ff000) * 4) & 0xfffff000);	//获取页表物理地址并去除属性
	phy_addr += vaddr & 0x00000fff;		//加上页表内偏移地址
	return (unsigned int)phy_addr;
}

int __page_link(unsigned long va, unsigned long pa, unsigned long prot)
{
    unsigned long vaddr = (unsigned long )va;
    unsigned long paddr = (unsigned long )pa;
	
	unsigned int *pde = pde_ptr(vaddr);
	unsigned int *pte = pte_ptr(vaddr);

	if (!(*pde & SIGN_P)) {
        unsigned int page_table = (unsigned int)kernel_alloc_page(1);
        if (!page_table) {
            printk(COLOR_RED"kernel no page left!\n");
			return -1;
        }
		*pde = (page_table | prot | SIGN_P);
		memset((void *)((unsigned long)pte & 0xfffff000), 0, PAGE_SIZE);
    }
	*pte = (paddr | prot | SIGN_P);
	
	return 0;
}

int __remap(unsigned int paddr, unsigned int vaddr, size_t size)
{
	unsigned int end = vaddr + size;
	while (vaddr < end)
	{
		if(__page_link(vaddr, paddr, SIGN_RW | SIGN_US)) return -1;
		vaddr += PAGE_SIZE;
		paddr += PAGE_SIZE;
	}
	return 0;
}

void *remap(unsigned int paddr, size_t size)
{
	uint32_t i, j, *pdt, *pt;
	for(i = 0; i < 1024; i++)
	{
		pdt = 0xfffff000;
		if(pdt[i])
		{
			for(j = 0; j < 1024; j++)
			{
				pt = 0xffc00000 + i*0x1000;
				if(pt[j] == ((paddr + size - 1)&0x003ff000))
				{
					return i*0x40000 + j*0x1000 + (paddr & 0x0fff);
				}
			}
		}
	}
	
	if (!paddr || !size) {
        return NULL;
    }
	
	unsigned long vaddr = alloc_vaddr(size);
    if (vaddr == -1) {
        printk("alloc virtual addr for IO remap failed!\n");
        return NULL;
    }
	
	unsigned phy_addr = vaddr;
	io_cli();
	
	if (__remap(paddr, vaddr, size)) {
        // free_vaddr(vaddr, size);
        vaddr = 0;
    }
	
	io_sti();
	
	return (void *)((phy_addr & 0xfffff000) + (paddr & 0x0fff));
}

int alloc_vir_page(void)
{
	int idx;
	int vir_addr;
	idx = mmap_search(&vir_page_mmap, 1);
	if(idx != -1){
		mmap_set(&vir_page_mmap, idx, 1);
	}else{
		return -1;
	}
	vir_addr = idx*0x1000 +  VIR_MEM_BASE_ADDR;
	return vir_addr;
}

int free_vir_page(int vir_addr)
{
	int idx;
	idx = (vir_addr - VIR_MEM_BASE_ADDR) / 0x1000;
	if(vir_page_mmap.bits[idx / 8] & (1 << (idx % 8)))
	{
		mmap_set(&vir_page_mmap, idx, 0);
	}else{
		return -1;
	}
	return 0;
}

void *kernel_alloc_page(int pages)
{
	int i;
	int vir_page_addr, vir_page_addr_more;
	
	int old_status = io_load_eflags();
	io_cli();
	
	vir_page_addr = alloc_vir_page();	//分配一个虚拟地址的页
	
	fill_vir_page_table(vir_page_addr);		//把页添加到当前页目录表系统中，使他可以被使用
	
	if(pages == 1){	//如果只有一个页
		memset((void *)vir_page_addr,0,PAGE_SIZE);
		
		io_store_eflags(old_status);
		return (void *)vir_page_addr;
	}else if(pages > 1){
		for(i = 1; i < pages; i++){
			vir_page_addr_more = alloc_vir_page();	//分配一个虚拟地址的页
			fill_vir_page_table(vir_page_addr_more);		//把页添加到当前页目录表系统中，使他可以被使用	
		}
		memset((void *)vir_page_addr,0,PAGE_SIZE*pages);
		io_store_eflags(old_status);
		
		return (void *)vir_page_addr;
	}else{
		return NULL;
	}
	return NULL;
}

void kernel_free_page(int vaddr, int pages)
{
	int i;
	int vir_page_addr = vaddr;
	
	int old_status = io_load_eflags();
	io_cli();

	free_vir_page(vir_page_addr);
	clean_vir_page_table(vir_page_addr);
	if(pages == 1){	//如果只有一个页
		io_store_eflags(old_status);
	
		return;
	}else if(pages > 1){
		for(i = 1; i < pages; i++){
			vir_page_addr += PAGE_SIZE;
			free_vir_page(vir_page_addr);
			clean_vir_page_table(vir_page_addr);
		}
		io_store_eflags(old_status);
		
		return;
	}
}

void fill_vir_page_table(int vir_address)
{
	int *pde, *pte;
	pde = pde_ptr(vir_address);
	if(((*pde)&0x00000001) != 0x00000001){	//不存在页表
		uint32_t pt = alloc_mem_page();	//分配页表地址
		pt |= 0x00000007;
		*pde = pt;		//填写页目录项为页表的地址
	}
	pte = pte_ptr(vir_address);
	if(((*pte)&0x00000001) != 0x00000001){	//不存在页表项
		uint32_t page = alloc_mem_page();	//分配页地址
		page |= 0x00000007;
		*pte = page;	//填写页表项为页的地址
	}
}

void clean_vir_page_table(int vir_address)
{
	int *pte;
	unsigned int page_phy_addr;
	pte = pte_ptr(vir_address);
	page_phy_addr = *pte;	//获得页表项中页的物理地址
	*pte = 0;	//清空页表项
	page_phy_addr &= 0xfffff000;	//保留高22位，对齐
	free_mem_page(page_phy_addr);	//释放对应的物理页
}

int alloc_mem_page(void)
{
	int idx;
	int mem_addr;
	idx = mmap_search(&phy_page_mmap, 1);
	if(idx != -1){
		mmap_set(&phy_page_mmap, idx, 1);
	}else{
		return -1;
	}
	mem_addr = idx*0x1000 + PHY_MEM_BASE_ADDR;

	return mem_addr;
}

int free_mem_page(int address)
{
	int addr = address;
	int idx;

	idx = (addr-PHY_MEM_BASE_ADDR)/0x1000;
	if(phy_page_mmap.bits[idx / 8] & (1 << idx % 8)){
		mmap_set(&phy_page_mmap, idx, 0);
	}else{
		return -1;
	}
	return 0;
}
