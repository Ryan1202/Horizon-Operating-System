#ifndef _WAIT_QUEUE_H
#define _WAIT_QUEUE_H

#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>

typedef struct {
	spinlock_t lock;
	list_t	   list_head;
} wait_queue_manager_t;

typedef struct {
	struct task_s *thread;
	void		  *private_data;
	list_t		   list;
} wait_queue_t;

wait_queue_manager_t *create_wait_queue(void);
void				  wait_queue_init(wait_queue_manager_t *wqm);
wait_queue_t		 *wait_queue_add(wait_queue_manager_t *wqm, uint32_t size);
wait_queue_t		 *wait_queue_first(wait_queue_manager_t *wqm);
void				  wait_queue_wakeup(wait_queue_manager_t *wqm);
void				  wait_queue_wakeup_all(wait_queue_manager_t *wqm);

#endif