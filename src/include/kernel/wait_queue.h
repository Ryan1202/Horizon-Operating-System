#ifndef _WAIT_QUEUE_H
#define _WAIT_QUEUE_H

#include "types.h"
#include <kernel/list.h>
#include <kernel/spinlock.h>

typedef struct {
	spinlock_t lock;
	list_t	   list_head;
} WaitQueue;

void		   wait_queue_init(WaitQueue *wq);
bool		   wait_queue_empty(WaitQueue *wq);
void		   wait_queue_add(WaitQueue *wq);
void		   wait_queue_del(WaitQueue *wq);
struct task_s *wait_queue_first(WaitQueue *wq);
void		   wait_queue_wakeup_thread(WaitQueue *wq, struct task_s *thread);
void		   wait_queue_wakeup(WaitQueue *wq);
void		   wait_queue_wakeup_all(WaitQueue *wq);

#endif