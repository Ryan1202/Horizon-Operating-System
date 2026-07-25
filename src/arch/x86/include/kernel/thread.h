#ifndef THREAD_H
#define THREAD_H

#include <kernel/memory.h>
#include <stdint.h>

typedef void thread_func(void *);

typedef struct Thread Thread;

void disable_preempt(void);
void enable_preempt(void);
bool can_preempt(void);
void scheduler_tick(uint16_t elapsed_ms);

// `name` 必须在线程对象存活期间有效；返回值为线程指针，只在线程运行前保证有效
struct Thread *thread_create(char *name, thread_func function, void *func_arg);
// 获取一个线程的强引用，获取到后除非调用 `thread_put` ，否则线程不会自动销毁
struct Thread *thread_get(struct Thread *thread);
// 将线程加入调度器队列，如果失败则返回 `false`
bool		   thread_run(struct Thread *thread);
// 释放一个线程的强引用，如果引用计数变为 `0` 则销毁线程
void		   thread_put(struct Thread *thread);
// 等待 owning handle 指向的线程退出，不消费该引用
void           thread_join(struct Thread *thread);
void		   try_yield();
void		   thread_exit(void) __attribute__((noreturn));

void thread_manager_init(void (*main_thread)(void *arg));

#endif
