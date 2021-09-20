#include <kernel/descriptor.h>
#include <kernel/process.h>
#include <kernel/page.h>
#include <kernel/memory.h>
#include <kernel/func.h>
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
	proc_stack->eflags = (1 << 1) | (1 << 9);
	proc_stack->esp = (void *)((uint32_t)thread_get_vaddr(USER_STACK3_ADDR) + PAGE_SIZE);
	proc_stack->ss = SELECTOR_U_DATA;
	thread_intr_exit(proc_stack);
}

void page_dir_activate(struct task_s *thread)
{
	uint32_t pagedir_phy_addr = PDT_PHY_ADDR;
	
	if (thread->pgdir != NULL)
	{
		pagedir_phy_addr = vir2phy(thread->pgdir);
	}
	
	write_cr3(pagedir_phy_addr);
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
	uint32_t *page_dir_vaddr = kernel_alloc_page(1);
	if (page_dir_vaddr == NULL)
	{
		return NULL;
	}
	memcpy((void *)page_dir_vaddr, 0xfffff000, 2048);
	uint32_t new_page_dir_phy_addr = vir2phy(page_dir_vaddr);
	page_dir_vaddr[1023] = new_page_dir_phy_addr | SIGN_US | SIGN_RW | SIGN_P;
	return page_dir_vaddr;
}

void create_user_vaddr_bitmap(struct task_s *user_prog)
{
	user_prog->vir_page_mmap.bits = kernel_alloc_page(VIR_MEM_MMAP_SIZE/PAGE_SIZE);
	user_prog->vir_page_mmap.len = VIR_MEM_MMAP_SIZE;
	memset(user_prog->vir_page_mmap.bits, 0, user_prog->vir_page_mmap.len);
}

void process_excute(void *filename, char *name)
{
	struct task_s *thread = kernel_alloc_page(1);
	init_thread(thread, name, THREAD_DEFAULT_PRIO);
	create_user_vaddr_bitmap(thread);
	thread_create(thread, start_process, filename);
	thread->pgdir = create_page_dir();
	init_thread_memory_manage(thread);
	
	int old_status = io_load_eflags();
	list_add_tail(&thread->general_tag, &thread_ready);
	list_add_tail(&thread->all_list_tag, &thread_all);
	io_store_eflags(old_status);
}