#include <driver/timer_dm.h>
#include <kernel/list.h>
#include <kernel/periodic_task.h>
#include <kernel/thread.h>

LIST_HEAD(periodic_task_lh);

void periodic_task(void *arg) {
	PeriodicTask *periodic_task;
	while (true) {
		list_for_each_owner (
			periodic_task, &periodic_task_lh, period_task_list) {
			periodic_task->func(periodic_task->arg);
		}
		schedule();
	}
}

void periodic_task_add(PeriodicTask *periodic_task) {
	list_add_tail(&periodic_task->period_task_list, &periodic_task_lh);
}

void periodic_task_del(PeriodicTask *periodic_task) {
	list_del(&periodic_task->period_task_list);
}