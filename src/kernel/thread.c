/**
 * @file thread.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 线程相关
 * @version 0.1
 * @date 2021-02
 *
 */
#include "types.h"
#include <driver/timer/timer_dm.h>
#include <kernel/console.h>
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/sync.h>
#include <kernel/thread.h>
#include <objects/permission.h>
#include <stdint.h>
#include <string.h>

struct task_s *current_task, *dead_task = NULL, *prev;
struct task_s *main_thread;

list_t	   thread_ready;
spinlock_t thread_ready_lock;
list_t	   thread_all;
spinlock_t thread_all_lock;

struct lock pid_lock;
uint32_t	new_pid = 0;

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
void kernel_thread(thread_func *function, void *func_arg) {
	io_sti();
	function(func_arg);
	thread_exit();
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

	pthread->priority = priority;
	pthread->kstack =
		(size_t *)((size_t)stack_page + THREAD_STACK_PAGES * PAGE_SIZE);
	pthread->ticks				= timer_get_schedule_tick(priority);
	pthread->elapsed_ticks		= 0;
	pthread->pgdir				= NULL;
	pthread->stack_magic		= 0x10000000;
	pthread->subject_id			= SUBJECT_ID_SYSTEM;
	pthread->flags.need_resched = 0;
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

	while (cur_thread->status == TASK_INTERRUPTIBLE) {
		try_yield();
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

	if (pthread != current_task) {
		if (pthread->status != TASK_READY) { pthread->status = TASK_READY; }
	} else {
		pthread->status = TASK_RUNNING;
	}
	spin_unlock_irqrestore(&pthread->status_lock, flags);
}

/**
 * @brief 创建内核主线程
 *
 */
static void make_main_thread(void) {
	main_thread = kzalloc(sizeof(struct task_s));
	init_thread(main_thread, NULL, "System", THREAD_DEFAULT_PRIO);
	current_task	 = main_thread;
	main_thread->pid = alloc_pid();

	if (list_find(&main_thread->all_list_tag, &thread_all)) {
		printk("[Thread Error] Main thread is alredy in thread list!\n");
		list_del(&main_thread->all_list_tag);
	}
	list_add_tail(&main_thread->all_list_tag, &thread_all);
}

/**
 * @brief 初始化任务管理
 *
 */
void init_task(void) {
	list_init(&thread_ready);
	list_init(&thread_all);
	spinlock_init(&thread_ready_lock);
	spinlock_init(&thread_all_lock);

	lock_init(&pid_lock);
	make_main_thread();
}
