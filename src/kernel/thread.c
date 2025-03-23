/**
 * @file thread.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 线程相关
 * @version 0.1
 * @date 2021-02
 *
 */
#include "kernel/driver_interface.h"
#include "kernel/spinlock.h"
#include <driver/timer_dm.h>
#include <kernel/console.h>
#include <kernel/func.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/process.h>
#include <kernel/sync.h>
#include <kernel/thread.h>
#include <math.h>
#include <objects/permission.h>
#include <stdint.h>
#include <string.h>

struct task_s *current_task, *dead_task = NULL;

struct task_s *main_thread;
list_t		   thread_ready;
spinlock_t	   thread_ready_lock;
LIST_HEAD(thread_all);
struct lock pid_lock;
uint32_t	new_pid = 0;

extern struct task_s *task_idle;

/**
 * @brief 分配pid
 *
 * @return uint32_t pid
 */
static uint32_t alloc_pid(void) {
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
struct task_s *get_current_thread() {
	// uint32_t sp;
	// GET_REG("esp", sp);
	// return (struct task_s *)(sp & 0xfffff000);
	return current_task;
}

size_t get_current_subject_id() {
	return get_current_thread()->subject_id;
}

/**
 * @brief 运行内核线程
 *
 * @param function
 * @param func_arg
 */
static void kernel_thread(thread_func *function, void *func_arg) {
	io_sti();
	function(func_arg);
	thread_exit();
}

/**
 * @brief 创建线程
 *
 * @param pthread 线程结构
 * @param function 线程入口函数
 * @param func_arg 参数
 */
void thread_create(
	struct task_s *pthread, thread_func *function, void *func_arg) {
	pthread->pid = alloc_pid();
	pthread->kstack -= sizeof(struct intr_stack);
	pthread->kstack -= sizeof(struct thread_stack);
	struct thread_stack *kthread_stack = (struct thread_stack *)pthread->kstack;
	kthread_stack->eip				   = kernel_thread;
	kthread_stack->function			   = function;
	kthread_stack->func_arg			   = func_arg;
	kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi =
		kthread_stack->edi					= 0;
}

/**
 * @brief 初始化线程结构
 *
 * @param pthread 线程结构
 * @param name 线程名
 * @param priority 优先级
 */
void init_thread(
	struct task_s *pthread, void *stack_page, char *name, int priority) {
	memset(pthread, 0, sizeof(struct task_s));
	strcpy(pthread->name, name);
	if (pthread == main_thread) {
		pthread->status = TASK_RUNNING;
	} else {
		pthread->status = TASK_READY;
	}
	spinlock_init(&pthread->status_lock);

	lock_init(&pthread->child_lock);
	pthread->child_count = 0;
	sema_init(&pthread->child_sem, 0);

	pthread->priority	   = priority;
	pthread->kstack		   = (uint32_t *)((uint32_t)stack_page + PAGE_SIZE);
	pthread->ticks		   = timer_get_schedule_tick(priority);
	pthread->elapsed_ticks = 0;
	pthread->pgdir		   = NULL;
	pthread->stack_magic   = 0x10000000;
	pthread->subject_id	   = SUBJECT_ID_SYSTEM;
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
struct task_s *thread_start(
	char *name, int priority, thread_func function, void *func_arg,
	struct task_s *parent) {
	struct task_s *thread	  = kmalloc(sizeof(struct task_s));
	void		  *stack_page = kernel_alloc_pages(1);

	init_thread(thread, stack_page, name, priority);

	if (parent != NULL) {
		thread->parent = parent;
		lock_acquire(&parent->child_lock);
		parent->child_count++;
		lock_release(&parent->child_lock);
	}
	thread_create(thread, function, func_arg);
	printk("[Thread] %s: %#x\n", thread->name, thread);

	if (thread->general_tag.next != NULL) {
		printk("thread %s:start error!\n", name);
		while (1)
			;
	}
	list_add_tail(&thread->general_tag, &thread_ready);

	if (list_find(&thread->all_list_tag, &thread_all)) {
		printk("thread %s:start error!\n", name);
		while (1)
			;
	}
	list_add_tail(&thread->all_list_tag, &thread_all);
	return thread;
}

void thread_exit(void) {
	struct task_s *cur = get_current_thread();
	cur->status		   = TASK_DIED;

	if (cur->parent != NULL) {
		struct task_s *parent = cur->parent;
		lock_acquire(&parent->child_lock);
		parent->child_count--;
		if (parent->child_count == 0) {
			sema_up(&parent->child_sem); // 所有子线程完成，唤醒父线程
		}
		lock_release(&parent->child_lock);
	}

	if (cur->general_tag.next != NULL) list_del(&cur->general_tag);
	list_del(&cur->all_list_tag);

	// 切换线程
	spin_lock_irqsave(&thread_ready_lock);
	printk("[Thread Exit] %s, parent:%s,", cur->name, cur->parent->name);

	struct task_s *next;
	/**
	 * 其实本来想正常切换到队列中下一个线程的，但不知道为什么会导致父线程无法退出，
	 * 所以优先切换到父线程。
	 */
	if (cur->parent != NULL) {
		next = cur->parent;
		if (next->general_tag.next != NULL) { list_del(&cur->general_tag); }
		cur->ticks = cur->priority;
	} else {
		next = list_first_owner(&thread_ready, struct task_s, general_tag);
		if (next != cur) list_del(&next->general_tag);
		else {
			printk("[No ready task!]");
			next = task_idle;
		}
	}

	// 进程将要退出，不需要恢复中断状态了
	spin_unlock(&thread_ready_lock);
	printk("switch to:%s\n", next->name);

	// 3. 切换线程
	dead_task = cur;
	// 激活页表并跳转
	process_activate(next);
	current_task = next;
	switch_to((int *)cur, (int *)next);
}

void thread_wait_children(struct task_s *parent) {
	// 等待子线程计数器归零
	while (1) {
		lock_acquire(&parent->child_lock);
		if (parent->child_count == 0) {
			lock_release(&parent->child_lock);
			break;
		}
		lock_release(&parent->child_lock);
		sema_down(&parent->child_sem); // 阻塞等待信号量
	}
}

/**
 * @brief 阻塞当前线程
 *
 * @param status 线程的目标状态(
 * TASK_INTERRUPTIBLE:可中断阻塞
 * TASK_UNINTERRUPTIBLE:不可中断阻塞)
 */
void thread_set_status(task_status_t status) {
	struct task_s *cur_thread = get_current_thread();
	int			   flags	  = spin_lock_irqsave(&cur_thread->status_lock);
	cur_thread->status		  = status;
	spin_unlock_irqrestore(&cur_thread->status_lock, flags);
}

void thread_wait() {
	struct task_s *cur_thread = get_current_thread();
	int			   flags	  = spin_lock_irqsave(&cur_thread->status_lock);
	task_status_t  status	  = cur_thread->status;
	spin_unlock_irqrestore(&cur_thread->status_lock, flags);

	while (cur_thread->wait_queue_tag.next != NULL) {
		schedule();
	}
}

/**
 * @brief 取消阻塞线程
 *
 * @param pthread 线程结构
 */
void thread_unblock(struct task_s *pthread) {
	int flags = spin_lock_irqsave(&pthread->status_lock);
	if ((pthread->status != TASK_INTERRUPTIBLE) &&
		(pthread->status != TASK_UNINTERRUPTIBLE)) {
		spin_unlock_irqrestore(&pthread->status_lock, flags);
		return;
	}

	if (pthread->status != TASK_READY) {
		if (pthread->general_tag.next != NULL) {
			list_del(&pthread->general_tag);
		}
		list_add_before(&pthread->general_tag, thread_ready.next);

		pthread->status = TASK_READY;
	}
	spin_unlock_irqrestore(&pthread->status_lock, flags);
}

/**
 * @brief 创建内核主线程
 *
 */
static void make_main_thread(void) {
	main_thread		 = kmalloc(sizeof(struct task_s));
	void *stack_page = kernel_alloc_pages(1);
	init_thread(main_thread, stack_page, "System", THREAD_DEFAULT_PRIO);
	current_task	 = main_thread;
	main_thread->pid = alloc_pid();

	if (list_find(&main_thread->all_list_tag, &thread_all)) {
		printk("thread main:start error!\n");
		while (1)
			;
	}
	list_add_tail(&main_thread->all_list_tag, &thread_all);
}

/**
 * @brief 初始化任务管理
 *
 */
void init_task(void) {
	list_init(&thread_ready);
	lock_init(&pid_lock);
	spinlock_init(&thread_ready_lock);
	make_main_thread();
}

/**
 * @brief 调度
 *
 */
void schedule(void) {
	int			   old_status;
	struct task_s *cur = get_current_thread();

	old_status = save_and_disable_interrupt();

	// 1. 判断当前线程是否需要加入到thread_ready
	if (cur->status == TASK_RUNNING) {
		if (cur->general_tag.next != NULL) {
			printk(
				"Error:Current thread(pid:%d) is in thread_ready list!\n",
				cur->pid);
			list_del(&cur->general_tag);
		}
		cur->status = TASK_READY;
		list_add_tail(&cur->general_tag, &thread_ready);
		cur->ticks = cur->priority;
	}

	// 2. 获取下一个线程，如果没有则使用idle线程
	struct task_s *next =
		list_first_owner(&thread_ready, struct task_s, general_tag);
	if (next != cur) list_del(&next->general_tag);
	else next = task_idle;
	// 4. 改变状态并加入到thread_ready
	next->status = TASK_RUNNING;

	// 5. 切换线程
	// 激活页表并跳转
	process_activate(next);
	current_task = next;
	switch_to((int *)cur, (int *)next);
	// 从其他线程切回来之后，检查上一个线程是否已经结束
	if (dead_task != NULL) {
		size_t stack_page = (size_t)dead_task->kstack & ~(PAGE_SIZE - 1);
		kernel_free_page(stack_page, 1);
		kfree(dead_task);
		dead_task = NULL;
	}
	if (cur->status == TASK_READY) cur->status = TASK_RUNNING;

	store_interrupt_status(old_status);
}

/**
 * @brief 初始化用户线程的内存管理
 *
 * @param thread 线程结构
 */
void init_thread_memory_manage(struct task_s *thread) {
	int		 i;
	uint32_t pages = DIV_ROUND_UP(sizeof(struct memory_manage), PAGE_SIZE);

	thread->memory_manage = (struct memory_manage *)kernel_alloc_pages(pages);
	memset(thread->memory_manage, 0, sizeof(struct memory_manage));
	for (i = 0; i < MEMORY_BLOCKS; i++) {
		thread->memory_manage->free_blocks[i].size	= 0;
		thread->memory_manage->free_blocks[i].flags = MEMORY_BLOCK_FREE;
	}
}
