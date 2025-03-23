/**
 * @file wait_queue.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 等待队列
 * @version 0.1
 * @date 2022-07-20
 */

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
	if (task->wait_queue_tag.next != NULL) {
		printk("Error:Current thread(pid:%d) is in wait queue!\n", task->pid);
		list_del(&task->wait_queue_tag);
	}
	int flags = spin_lock_irqsave(&wq->lock);

	// 把当前线程的list tag直接挂到等待队列的list上
	list_add_tail(&task->wait_queue_tag, &wq->list_head);

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

/**
 * @brief 唤醒等待队列中的第一个任务
 *
 * @param wqm 等待队列管理结构
 */
void wait_queue_wakeup(WaitQueue *wqm) {
	if (list_empty(&wqm->list_head)) { return; }
	struct task_s *thread =
		list_first_owner(&wqm->list_head, struct task_s, wait_queue_tag);
	int flags = spin_lock_irqsave(&wqm->lock);

	list_del(&thread->wait_queue_tag);

	int flags2 = spin_lock_irqsave(&thread->status_lock);
	if (thread->status == TASK_INTERRUPTIBLE ||
		thread->status == TASK_UNINTERRUPTIBLE) {
		spin_unlock_irqrestore(&thread->status_lock, flags2);
		thread_unblock(thread);
	}

	spin_unlock_irqrestore(&wqm->lock, flags);
	return;
}

/**
 * @brief 唤醒等待队列中的所有任务
 *
 * @param wqm 等待队列管理结构
 */
void wait_queue_wakeup_all(WaitQueue *wqm) {
	if (list_empty(&wqm->list_head)) { return; }
	struct task_s *cur, *next;
	struct task_s *thread;

	int old_status = spin_lock_irqsave(&wqm->lock);
	list_for_each_owner_safe (cur, next, &wqm->list_head, wait_queue_tag) {
		thread = cur;
		list_del(&cur->wait_queue_tag);

		int flags2 = spin_lock_irqsave(&thread->status_lock);
		if (thread->status == TASK_INTERRUPTIBLE ||
			thread->status == TASK_UNINTERRUPTIBLE) {
			spin_unlock_irqrestore(&thread->status_lock, flags2);
			thread_unblock(thread);
			schedule();
		}
	}

	spin_unlock_irqrestore(&wqm->lock, old_status);
	return;
}
