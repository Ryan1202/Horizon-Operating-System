#ifndef THREAD_H
#define THREAD_H

#include <kernel/list.h>
#include <kernel/memory.h>
#include <stdint.h>

typedef void thread_func(void *);

typedef enum {
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
	void	*esp;
	uint32_t ss;
};

struct thread_stack {
	uint32_t ebp;
	uint32_t ebx;
	uint32_t edi;
	uint32_t esi;

	void (*eip)(thread_func *func, void *func_arg);

	void(*unused_retaddr);
	thread_func *function;
	void		*func_arg;
};

struct task_s {
	uint32_t *kstack;

	uint32_t	  pid;
	char		  name[32];
	task_status_t status;
	uint8_t		  priority;
	uint8_t		  ticks;
	uint32_t	  elapsed_ticks;
	uint32_t	 *pgdir;
	uint32_t	  stack_magic;

	uint8_t *end_flag;

	struct mmap			  vir_page_mmap;
	struct memory_manage *memory_manage;

	list_t general_tag;
	list_t all_list_tag;
};

#define THREAD_DEFAULT_PRIO 100

extern list_t thread_all;

struct task_s *get_current_thread();
void		   init_thread(struct task_s *pthread, char *name, int priority);
void		   thread_create(
			  struct task_s *pthread, thread_func *function, void *func_arg);
struct task_s *thread_start(
	char *name, int priority, thread_func function, void *func_arg);
void thread_exit(void);
void thread_block(task_status_t status);
void thread_unblock(struct task_s *pthread);
void init_task(void);
void schedule(void);
void init_thread_memory_manage(struct task_s *thread);
void thread_set_end_flag(struct task_s *pthread, uint8_t *flag);

#endif