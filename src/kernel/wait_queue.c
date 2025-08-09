/**
 * @file wait_queue.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 等待队列
 * @version 0.1
 * @date 2022-07-20
 */

#include "kernel/console.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/wait_queue.h>

/**
 * @brief 初始化等待队列
 *
 * @param wq 等待队列管理结构
 */
void wait_queue_init(WaitQueue *wq) {
	spinlock_init(&wq->lock);
	list_init(&wq->list_head);
	return;
}

/**
 * @brief 检查等待队列是否为空
 *
 * @param wq 等待队列管理结构
 * @return true
 * @return false
 */
bool wait_queue_empty(WaitQueue *wq) {
	return list_empty(&wq->list_head);
}

/**
 * @brief 添加当前任务到等待队列
 *
 * @param wq 等待队列管理结构
 *
 * @return
 */
void wait_queue_add(WaitQueue *wq) {
	struct task_s *task = get_current_thread();
	if (list_in_list(&task->wait_queue_tag)) {
		printk("Error:Current thread(pid:%d) is in wait queue!\n", task->pid);
		list_del(&task->wait_queue_tag);
	}
	int flags = spin_lock_irqsave(&wq->lock);

	// 把当前线程的list tag直接挂到等待队列的list上
	list_add_tail(&task->wait_queue_tag, &wq->list_head);

	spin_unlock_irqrestore(&wq->lock, flags);
}

void wait_queue_del(WaitQueue *wq) {
	struct task_s *task = get_current_thread();
	if (list_in_list(&task->wait_queue_tag)) {
		printk("Error:Current thread(pid:%d) is in wait queue!\n", task->pid);
		list_del(&task->wait_queue_tag);
	}
	int flags = spin_lock_irqsave(&wq->lock);
	if (list_in_list(&task->wait_queue_tag)) list_del(&task->wait_queue_tag);
	spin_unlock_irqrestore(&wq->lock, flags);
}

/**
 * @brief 获取等待队列中的第一个任务
 *
 * @param wq 等待队列管理结构
 * @return
 */
struct task_s *wait_queue_first(WaitQueue *wqm) {
	if (list_empty(&wqm->list_head)) { return NULL; }
	return list_first_owner(&wqm->list_head, struct task_s, wait_queue_tag);
}

void wait_queue_wakeup_thread(WaitQueue *wq, struct task_s *thread) {
	int flags = spin_lock_irqsave(&wq->lock);

	list_del(&thread->wait_queue_tag);

	if (thread != get_current_thread()) thread_unblock(thread);

	spin_unlock_irqrestore(&wq->lock, flags);
	return;
}

/**
 * @brief 唤醒等待队列中的第一个任务
 *
 * @param wqm 等待队列管理结构
 */
void wait_queue_wakeup(WaitQueue *wq) {
	if (list_empty(&wq->list_head)) { return; }
	struct task_s *thread =
		list_first_owner(&wq->list_head, struct task_s, wait_queue_tag);

	wait_queue_wakeup_thread(wq, thread);

	return;
}

/**
 * @brief 唤醒等待队列中的所有任务
 *
 * @param wqm 等待队列管理结构
 */
void wait_queue_wakeup_all(WaitQueue *wq) {
	if (list_empty(&wq->list_head)) { return; }
	struct task_s *cur, *next;
	struct task_s *thread;

	int old_status = spin_lock_irqsave(&wq->lock);
	list_for_each_owner_safe (cur, next, &wq->list_head, wait_queue_tag) {
		thread = cur;
		list_del(&cur->wait_queue_tag);

		thread_unblock(thread);
	}

	spin_unlock_irqrestore(&wq->lock, old_status);
	return;
}
