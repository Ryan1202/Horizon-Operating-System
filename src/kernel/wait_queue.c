/**
 * @file wait_queue.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 等待队列
 * @version 0.1
 * @date 2022-07-20
 */

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
	struct task_s *task		  = get_current_thread();
	int			   old_status = load_interrupt_status();
	disable_interrupt();
	spin_lock(&wq->lock);

	// 把当前线程的list tag直接挂到等待队列的list上
	list_add_tail(&task->wait_queue_tag, &wq->list_head);

	spin_unlock(&wq->lock);
	store_interrupt_status(old_status);
}

/**
 * @brief 获取等待队列中的第一个任务
 *
 * @param wq 等待队列管理结构
 * @return
 */
WaitQueueItem *wait_queue_first(WaitQueue *wqm) {
	if (list_empty(&wqm->list_head)) { return NULL; }
	return list_first_owner(&wqm->list_head, WaitQueueItem, wait_queue_tag);
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
	int old_status = load_interrupt_status();
	disable_interrupt();
	spin_lock(&wqm->lock);

	list_del(&thread->wait_queue_tag);

	if (thread->status == TASK_BLOCKED || thread->status == TASK_WAITING ||
		thread->status == TASK_HANGING) {
		thread_unblock(thread);
	}

	spin_unlock(&wqm->lock);
	store_interrupt_status(old_status);
	return;
}

/**
 * @brief 唤醒等待队列中的所有任务
 *
 * @param wqm 等待队列管理结构
 */
void wait_queue_wakeup_all(WaitQueue *wqm) {
	if (list_empty(&wqm->list_head)) { return; }
	WaitQueueItem *cur, *next;
	struct task_s *thread;

	int old_status = load_interrupt_status();
	disable_interrupt();

	spin_lock(&wqm->lock);
	list_for_each_owner_safe (cur, next, &wqm->list_head, wait_queue_tag) {
		thread = cur;
		list_del(&cur->wait_queue_tag);
		if (thread->status == TASK_BLOCKED || thread->status == TASK_WAITING ||
			thread->status == TASK_HANGING) {
			thread_unblock(thread);
		}
	}

	spin_unlock(&wqm->lock);
	store_interrupt_status(old_status);
	return;
}
