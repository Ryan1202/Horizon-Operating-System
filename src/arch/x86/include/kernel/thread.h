#ifndef THREAD_H
#define THREAD_H

#include "kernel/spinlock.h"
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/sync.h>
#include <stdint.h>

#define THREAD_STACK_PAGES 4

typedef void thread_func(void *);

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

extern uint32_t	  preempt_count;
extern list_t	  thread_all;
extern spinlock_t thread_ready_lock;

extern struct task_s *current_task;

#define THREAD_DEFAULT_PRIO 100

#define PREEMPT_COUNT_MASK	0xff0000
#define HARDIRQ_COUNT_MASK	0xff00
#define SOFTIRQ_COUNT_MASK	0xff
#define PREEMPT_COUNT_SHIFT 16
#define HARDIRQ_COUNT_SHIFT 8
#define SOFTIRQ_COUNT_SHIFT 0

#define softirq_count() \
	((preempt_count & SOFTIRQ_COUNT_MASK) >> SOFTIRQ_COUNT_SHIFT)
#define hardirq_count() \
	((preempt_count & HARDIRQ_COUNT_MASK) >> HARDIRQ_COUNT_SHIFT)
#define preempt_count() \
	((preempt_count & PREEMPT_COUNT_MASK) >> PREEMPT_COUNT_SHIFT)

#define in_softirq() ((preempt_count & SOFTIRQ_COUNT_MASK) != 0)
#define in_hardirq() ((preempt_count & HARDIRQ_COUNT_MASK) != 0)
#define can_preempt() \
	((preempt_count & (PREEMPT_COUNT_MASK | HARDIRQ_COUNT_MASK)) == 0)

static inline bool need_resched(void) {
	return current_task != NULL && current_task->flags.need_resched;
}

static inline void hardirq_enter(void) {
	preempt_count += 1 << HARDIRQ_COUNT_SHIFT;
}

static inline void hardirq_exit(void) {
	preempt_count -= 1 << HARDIRQ_COUNT_SHIFT;
}

static inline void softirq_enter(void) {
	preempt_count += 1 << SOFTIRQ_COUNT_SHIFT;
}

static inline void softirq_exit(void) {
	preempt_count -= 1 << SOFTIRQ_COUNT_SHIFT;
}

static inline void disable_preempt(void) {
	preempt_count += 1 << PREEMPT_COUNT_SHIFT;
}

static inline void enable_preempt(void) {
	preempt_count -= 1 << PREEMPT_COUNT_SHIFT;
}

struct task_s *get_current_thread();
size_t		   get_current_subject_id();
void		   init_thread(
			  struct task_s *pthread, void *stack_page, char *name, int priority);
void thread_create(
	struct task_s *pthread, thread_func *function, void *func_arg);
struct task_s *thread_start(
	char *name, int priority, thread_func function, void *func_arg,
	struct task_s *parent);
void thread_exit(void);
void thread_set_status(task_status_t status);
void thread_wait();
void thread_unblock(struct task_s *pthread);
void init_task(void);
void schedule(void);
void thread_wait_children(struct task_s *parent);

#endif
