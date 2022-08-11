/**
 * @file thread.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 线程相关
 * @version 0.1
 * @date 2021-02
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <kernel/console.h>
#include <kernel/func.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/process.h>
#include <kernel/sync.h>
#include <kernel/thread.h>
#include <math.h>
#include <string.h>

struct task_s *main_thread;
list_t thread_ready;
list_t thread_all;
struct lock pid_lock;
uint32_t new_pid = 0;

/**
 * @brief 分配pid
 * 
 * @return uint32_t pid
 */
static uint32_t alloc_pid(void)
{
	lock_acquire(&pid_lock);
	new_pid++;
	lock_release(&pid_lock);
	return new_pid;
}

/**
 * @brief 获取当前线程结构
 * 
 * @return struct task_s* 
 */
struct task_s *get_current_thread()
{
	uint32_t sp;
	GET_REG("esp", sp);
	return (struct task_s *)(sp & 0xfffff000);
}

/**
 * @brief 运行内核线程
 * 
 * @param function 
 * @param func_arg 
 */
static void kernel_thread(thread_func *function, void *func_arg)
{
	io_sti();
	function(func_arg);
}

/**
 * @brief 创建线程
 * 
 * @param pthread 线程结构
 * @param function 线程入口函数
 * @param func_arg 参数
 */
void thread_create(struct task_s *pthread, thread_func *function, void *func_arg)
{
	pthread->pid = alloc_pid();
	pthread->kstack -= sizeof(struct intr_stack);
	pthread->kstack -= sizeof(struct thread_stack);
	struct thread_stack *kthread_stack = (struct thread_stack *)pthread->kstack;
	kthread_stack->eip = kernel_thread;
	kthread_stack->function = function;
	kthread_stack->func_arg = func_arg;
	kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

/**
 * @brief 初始化线程结构
 * 
 * @param pthread 线程结构
 * @param name 线程名
 * @param priority 优先级
 */
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
	pthread->kstack = (uint32_t *)((uint32_t)pthread + PAGE_SIZE);
	pthread->ticks = priority;
	pthread->elapsed_ticks = 0;
	pthread->pgdir = NULL;
	pthread->stack_magic = 0x10000000;
}

/**
 * @brief 创建并运行一个线程
 * 
 * @param name 线程名
 * @param priority 优先级
 * @param function 入口函数
 * @param func_arg 参数
 * @return struct task_s* 创建好的线程
 */
struct task_s *thread_start(char *name, int priority, thread_func function, void *func_arg)
{
	struct task_s *thread = kernel_alloc_pages(1);
	
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

/**
 * @brief 阻塞当前线程
 * 
 * @param status 线程的目标状态(
 * TASK_BLOCKED:阻塞
 * TASK_WAITING:等待
 * TASK_HANGING:挂起)
 */
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

/**
 * @brief 取消阻塞线程
 * 
 * @param pthread 线程结构
 */
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

/**
 * @brief 创建内核主线程
 * 
 */
static void make_main_thread(void)
{
	main_thread = get_current_thread();
	init_thread(main_thread, "System", 10);

	if(list_find(&main_thread->all_list_tag, &thread_all))
	{
		printk("thread main:start error!\n");
		while(1);
	}
	list_add_tail(&main_thread->all_list_tag, &thread_all);
}

/**
 * @brief 初始化任务管理
 * 
 */
void init_task(void)
{
	list_init(&thread_ready);
	list_init(&thread_all);
	lock_init(&pid_lock);
	make_main_thread();
}

/**
 * @brief 调度
 * 
 */
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
	struct task_s *next;
	next = list_first_owner(&thread_ready, struct task_s, general_tag);
	list_del(thread_ready.next);
	next->status = TASK_RUNNING;
	
	process_activate(next);
	
	switch_to((int *)cur, (int *)next);
}

/**
 * @brief 初始化用户线程的内存管理
 * 
 * @param thread 线程结构
 */
void init_thread_memory_manage(struct task_s *thread)
{
	int i;
	uint32_t pages = DIV_ROUND_UP(sizeof(struct memory_manage), PAGE_SIZE);
	
	thread->memory_manage = (struct memory_manage *)kernel_alloc_pages(pages);
	memset(thread->memory_manage, 0, sizeof(struct memory_manage));
	for(i = 0; i < MEMORY_BLOCKS; i++){	
		thread->memory_manage->free_blocks[i].size = 0;
		thread->memory_manage->free_blocks[i].flags = MEMORY_BLOCK_FREE;
	}
}
