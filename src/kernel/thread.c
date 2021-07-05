#include <kernel/thread.h>
#include <kernel/page.h>
#include <kernel/memory.h>
#include <kernel/func.h>
#include <string.h>

struct task_s *main_thread;
list_t thread_ready;
list_t thread_all;

struct task_s *get_current_thread()
{
	uint32_t sp;
	GET_REG("esp", sp);
	return (struct task_s *)(sp & 0xfffff000);
}

static void kernel_thread(thread_func *function, void *func_arg)
{
	io_sti();
	function(func_arg);
}

void thread_create(struct task_s *pthread, thread_func *function, void *func_arg)
{
	pthread->stack -= sizeof(struct intr_stack);
	pthread->stack -= sizeof(struct thread_stack);
	struct thread_stack *kthread_stack = (struct thread_stack *)pthread->stack;
	kthread_stack->eip = kernel_thread;
	kthread_stack->function = function;
	kthread_stack->func_arg = func_arg;
	kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

void init_thread(struct task_s *pthread, char *name, int priority)
{
	memset(pthread, 0, sizeof(sizeof(struct task_s)));
	strcpy(pthread->name, name);
	if(pthread == main_thread)
	{
		pthread->status = TASK_RUNNING;
	}
	else
	{
		pthread->status = TASK_WAITING;
	}
	pthread->priority = priority;
	pthread->stack = (uint32_t *)((uint32_t)pthread + PAGE_SIZE);
	pthread->ticks = priority;
	pthread->elapsed_ticks = 0;
	pthread->pgdir = NULL;
	pthread->stack_magic = 0x10000000;
}

struct task_s *thread_start(char *name, int priority, thread_func function, void *func_arg)
{
	struct task_s *thread = kernel_alloc_page(1);
	
	init_thread(thread, name, priority);
	thread_create(thread, function, func_arg);
	if(list_find(&thread->general_tag, &thread_ready))
	{
		printk("thread %s:start error!\n", name);
		while(1);
	}
	list_add_tail(&thread->general_tag, &thread_ready);
	if(list_find(&thread->all_list_tag, &thread_all))
	{
		printk("thread %s:start error!\n", name);
		while(1);
	}
	list_add_tail(&thread->all_list_tag, &thread_all);
	// __asm__ __volatile__(
	// 	"movl %0, %%esp; \
	// 	pop %%ebp; \
	// 	pop %%ebx; \
	// 	pop %%edi; \
	// 	pop %%esi; \
	// 	ret"
	// 	::"g"(thread->stack)
	// 	:"memory"
	// );
	return thread;
}

void thread_block(task_status_t status)
{
	int old_status = io_load_eflags();
	if((status != TASK_BLOCKED) && (status != TASK_WAITING) && (status != TASK_HANGING))
	{
		printk("error");
		while(1);
	}
	struct task_s *cur_thread = get_current_thread();
	cur_thread->status = status;
	schedule();
	io_store_eflags(old_status);
}

void thread_unblock(struct task_s *pthread)
{
	int old_status = io_load_eflags();
	if((pthread->status != TASK_BLOCKED) && (pthread->status != TASK_WAITING) && (pthread->status != TASK_HANGING))
	{
		printk("error");
		while(1);
	}
	if (pthread->status != TASK_READY)
	{
		if(list_find(&pthread->general_tag, &thread_ready))
		{
			printk("error");
			while(1);
		}
		list_add_before(&pthread->general_tag, thread_ready.next);
		pthread->status = TASK_READY;
	}
	io_store_eflags(old_status);
}

static void make_main_thread(void)
{
	main_thread = get_current_thread();
	init_thread(main_thread, "System", 1000);

	if(list_find(&main_thread->all_list_tag, &thread_all))
	{
		printk("thread main:start error!\n");
		while(1);
	}
	list_add_tail(&main_thread->all_list_tag, &thread_all);
}

void init_task(void)
{
	list_init(&thread_ready);
	list_init(&thread_all);
	make_main_thread();
}

void schedule(void)
{
	struct task_s *cur = get_current_thread();
	if (cur->status == TASK_RUNNING)
	{
		if (list_find(&cur->general_tag, &thread_ready))
		{
			printk("error!\n");
		while(1);
		}
		list_add_tail(&cur->general_tag, &thread_ready);
		cur->ticks = cur->priority;
		cur->status = TASK_READY;
	}
	struct task_s *next = list_first_owner(&thread_ready, struct task_s, general_tag);
	list_del(thread_ready.next);
	next->status = TASK_RUNNING;
	switch_to(cur, next);
}