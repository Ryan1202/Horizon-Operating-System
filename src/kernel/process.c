#include <kernel/descriptor.h>
#include <kernel/page.h>
#include <kernel/memory.h>
#include <kernel/func.h>
#include <kernel/app.h>
#include <kernel/process.h>
#include <fs/fs.h>
#include <math.h>
#include <string.h>

void thread_intr_exit(struct intr_stack *proc_stack);
extern list_t thread_ready;
extern list_t thread_all;

void start_process(void *filename)
{
	void *function = filename;
	struct task_s *cur = get_current_thread();
	cur->kstack += sizeof(struct thread_stack);
	struct intr_stack *proc_stack = (struct intr_stack *)cur->kstack;
	proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
	proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
	proc_stack->gs = 0;
	proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
	proc_stack->eip = function;
	proc_stack->cs = SELECTOR_U_CODE;
	proc_stack->eflags = (1 << 1) | (1 << 9) | (0 << 12);
	proc_stack->esp = (void *)((uint32_t)thread_get_page(get_current_thread(), USER_STACK3_ADDR) + PAGE_SIZE);
	proc_stack->ss = SELECTOR_U_STACK;
	thread_intr_exit(proc_stack);
}

void page_dir_activate(struct task_s *thread)
{
	uint32_t pagedir_phy_addr = PDT_PHY_ADDR;
	
	if (thread->pgdir != NULL)
	{
		pagedir_phy_addr = vir2phy((uint32_t)thread->pgdir);
	}
	
	write_cr3((uint32_t *)pagedir_phy_addr);
}

void process_activate(struct task_s *thread)
{
	page_dir_activate(thread);
	if (thread->pgdir)
	{
		update_tss_esp(thread);
	}
}

uint32_t *create_page_dir(void)
{
	uint32_t *page_dir_vaddr = kernel_alloc_pages(1);
	if (page_dir_vaddr == NULL)
	{
		return NULL;
	}
	// 复制第0~511个与创建第1023个页目录表项
	memcpy((void *)(page_dir_vaddr + 0*4), (void *)(0xfffff000 + 0*4), 2048);
	uint32_t new_page_dir_phy_addr = vir2phy((uint32_t)page_dir_vaddr);
	page_dir_vaddr[1023] = new_page_dir_phy_addr | SIGN_USER | SIGN_RW | SIGN_P;
	return page_dir_vaddr;
}

void create_user_vaddr_bitmap(struct task_s *user_prog)
{
	uint32_t pg_cnt = DIV_ROUND_UP((0xffcfffff - USER_START_ADDR) / PAGE_SIZE / 8, PAGE_SIZE);
	user_prog->vir_page_mmap.bits = kernel_alloc_pages(pg_cnt);
	user_prog->vir_page_mmap.len = (0xffcfffff - USER_START_ADDR) / PAGE_SIZE / 8;
	memset(user_prog->vir_page_mmap.bits, 0, user_prog->vir_page_mmap.len);
}

void process_excute(void *filename, struct program_struct *prog)
{
	int i = 0;
	struct task_s *thread = kernel_alloc_pages(1);
	struct prog_segment *p, *next;
	init_thread(thread, prog->name.text, THREAD_DEFAULT_PRIO);
	create_user_vaddr_bitmap(thread);
	thread_create(thread, start_process, filename);
	thread->pgdir = create_page_dir();
	init_thread_memory_manage(thread);
	list_for_each_owner_safe(p, next, &prog->seg_head, list)
	{
		process_load_segment(thread, prog->inode, p->offset, p->filesz, p->memsz, p->vaddr);
	}
	
	// 将该任务加入任务队列
	int old_status = io_load_eflags();
	list_add_tail(&thread->general_tag, &thread_ready);
	list_add_tail(&thread->all_list_tag, &thread_all);
	io_store_eflags(old_status);
}

int process_load_segment(struct task_s *thread, struct index_node *inode, unsigned long offset, unsigned long filesz, unsigned long memsz, unsigned long vaddr)
{
	unsigned long size0 = PAGE_SIZE - (vaddr&0xfff);
	unsigned long page_num = 1;
	if (memsz > size0)
	{
		page_num += DIV_ROUND_UP(memsz - size0, PAGE_SIZE);
	}
	uint32_t *addr = kernel_alloc_pages(page_num);
	int ret = thread_use_page(thread, vaddr, vir2phy((uint32_t)addr), page_num);
	if (ret == -1)
	{
		return -1;
	}
	fs_seek(inode, 0, offset);
	fs_read(inode, (uint8_t *)(addr + (vaddr&0xfff)), filesz);
	if (memsz > filesz)
	{
		memset((void *)(addr + (vaddr&0xfff) + filesz), 0, memsz - filesz);
	}
	return 0;
}