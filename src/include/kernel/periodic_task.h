#ifndef _PERIODIC_TASK_H
#define _PERIODIC_TASK_H

#include "kernel/list.h"

typedef struct PeriodicTask {
	list_t period_task_list;

	void (*func)(void *arg);
	void *arg;
} PeriodicTask;

void periodic_task(void *arg);
void periodic_task_add(PeriodicTask *periodic_task);
void periodic_task_del(PeriodicTask *periodic_task);

#endif