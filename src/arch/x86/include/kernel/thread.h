#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include <kernel/list.h>

typedef void thread_func(void *);

typedef enum{
	TASK_RUNNING,
	TASK_READY,
	TASK_BLOCKED,
	TASK_WAITING,
	TASK_HANGING,
	TASK_DIED
} task_status_t;

struct intr_stack {
	uint32_t vec_no;
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp_dummy;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;
	
	uint32_t err_code;
	void (*eip)(void);
	uint32_t cs;
	uint32_t eflags;
	void *esp;
	uint32_t ss;
};

struct thread_stack {
	uint32_t ebp;
	uint32_t ebx;
	uint32_t edi;
	uint32_t esi;
	
	void (*eip)(thread_func *func, void *func_arg);
	
	void (*unused_retaddr);
	thread_func *function;
	void *func_arg;
};

struct task_s {
	uint32_t *stack;
	
	uint32_t pid;
	char name[32];
	task_status_t status;
	uint8_t priority;
	uint8_t ticks;
	uint32_t elapsed_ticks;
	uint32_t *pgdir;
	uint32_t stack_magic;
	
	list_t general_tag;
	list_t all_list_tag;
};

struct task_s *get_current_thread();
struct task_s *thread_start(char *name, int priority, thread_func function, void *func_arg);
void thread_block(task_status_t status);
void thread_unblock(struct task_s *pthread);
void init_task(void);
void schedule(void);

#endif