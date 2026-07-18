#ifndef THREAD_H
#define THREAD_H

#include "kernel/spinlock.h"
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/sync.h>
#include <stdint.h>

#define THREAD_STACK_PAGES 4

typedef void thread_func(void *);

typedef struct Thread {
} Thread;

typedef enum {
	TASK_RUNNING,
	TASK_READY,
	TASK_INTERRUPTIBLE,
	TASK_UNINTERRUPTIBLE,
	TASK_DIED
} task_status_t;

struct intr_stack {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rbp;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
	uint64_t vec_no;
	void (*rip)(void);
	uint64_t cs;
	uint64_t rflags;
	void	*rsp;
	uint64_t ss;
};

struct thread_stack {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t rbx;
	uint64_t rbp;
	void (*rip)(void);
};

struct task_s {
	size_t *kstack;

	uint32_t	  pid;
	char		  name[32];
	task_status_t status;
	spinlock_t	  status_lock;
	uint8_t		  priority;
	uint8_t		  ticks;
	uint32_t	  elapsed_ticks;
	size_t		 *pgdir;
	uint32_t	  stack_magic;
	size_t		  subject_id;

	struct {
		uint8_t need_resched : 1; // 是否需要调度
	} flags;

	struct lock		 child_lock;  // 保护子线程计数器的锁
	int				 child_count; // 当前活跃的子线程数
	struct semaphore child_sem;	  // 子线程完成信号量（初始为0）
	struct task_s	*parent;

	list_t wait_queue_tag;
	list_t general_tag;
	list_t all_list_tag;
};

extern list_t	  thread_all;
extern spinlock_t thread_ready_lock;

extern struct task_s *current_task;

#define THREAD_DEFAULT_PRIO 100

void disable_preempt(void);
void enable_preempt(void);
bool can_preempt(void);
void scheduler_tick(uint16_t elapsed_ms);

struct task_s *get_current_thread();
size_t		   get_current_subject_id();
void		   init_thread(
			  struct task_s *pthread, void *stack_page, char *name, int priority);
// `name` 必须在线程对象存活期间有效；返回值为线程指针，只在线程运行前保证有效
struct Thread *thread_create(char *name, thread_func function, void *func_arg);
// 获取一个线程的强引用，获取到后除非调用 `thread_put` ，否则线程不会自动销毁
struct Thread *thread_get(struct Thread *thread);
// 将线程加入调度器队列，如果失败则返回 `false`
bool		   thread_run(struct Thread *thread);
// 释放一个线程的强引用，如果引用计数变为 `0` 则销毁线程
void		   thread_put(struct Thread *thread);
void		   try_yield();
void		   thread_exit(void) __attribute__((noreturn));

void thread_set_status(task_status_t status);
void thread_wait();
void thread_unblock(struct task_s *pthread);
void thread_wait_children(struct task_s *parent);

void thread_manager_init(void (*main_thread)(void *arg));

#endif
