/**
 * @file wait_queue.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 等待队列
 * @version 0.1
 * @date 2022-07-20
 */

#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/wait_queue.h>

/**
 * @brief 创建一个等待队列
 *
 * @return wait_queue_manager_t* 等待队列的管理结构
 */
wait_queue_manager_t *create_wait_queue(void) {
	wait_queue_manager_t *wqm =
		(wait_queue_manager_t *)kmalloc(sizeof(wait_queue_manager_t));
	return wqm;
}

/**
 * @brief 初始化等待队列
 *
 * @param wqm 等待队列管理结构
 */
void wait_queue_init(wait_queue_manager_t *wqm) {
	spinlock_init(&wqm->lock);
	list_init(&wqm->list_head);
	return;
}

/**
 * @brief 检查等待队列是否为空
 *
 * @param wqm 等待队列管理结构
 * @return true
 * @return false
 */
bool wait_queue_empty(wait_queue_manager_t *wqm) {
	return list_empty(&wqm->list_head);
}

/**
 * @brief 添加当前任务到等待队列
 *
 * @param wqm 等待队列管理结构
 * @param size private_data的大小
 *
 * @return wait_queue_t *
 */
wait_queue_t *wait_queue_add(wait_queue_manager_t *wqm, uint32_t size) {
	wait_queue_t *wq = (wait_queue_t *)kmalloc(sizeof(wait_queue_t));
	wq->thread		 = get_current_thread();
	int old_status	 = load_interrupt_status();
	disable_interrupt();
	spin_lock(&wqm->lock);

	list_add_tail(&wq->list, &wqm->list_head);

	spin_unlock(&wqm->lock);
	store_interrupt_status(old_status);
	if (size != 0) {
		wq->private_data = kmalloc(size);
	} else {
		wq->private_data = NULL;
	}
	return wq;
}

/**
 * @brief 获取等待队列中的第一个任务
 *
 * @param wqm 等待队列管理结构
 * @return wait_queue_t*
 */
wait_queue_t *wait_queue_first(wait_queue_manager_t *wqm) {
	if (list_empty(&wqm->list_head)) { return NULL; }
	return list_first_owner(&wqm->list_head, wait_queue_t, list);
}

/**
 * @brief 唤醒等待队列中的第一个任务
 *
 * @param wqm 等待队列管理结构
 */
void wait_queue_wakeup(wait_queue_manager_t *wqm) {
	if (list_empty(&wqm->list_head)) { return; }
	wait_queue_t  *wq = list_first_owner(&wqm->list_head, wait_queue_t, list);
	struct task_s *thread	  = wq->thread;
	int			   old_status = load_interrupt_status();
	disable_interrupt();
	spin_lock(&wqm->lock);

	list_del(&wq->list);
	if (wq->private_data != NULL) { kfree(wq->private_data); }
	kfree(wq);

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
void wait_queue_wakeup_all(wait_queue_manager_t *wqm) {
	if (list_empty(&wqm->list_head)) { return; }
	wait_queue_t  *cur, *next;
	struct task_s *thread;

	int old_status = load_interrupt_status();
	disable_interrupt();

	spin_lock(&wqm->lock);
	list_for_each_owner_safe (cur, next, &wqm->list_head, list) {
		thread = cur->thread;
		list_del(&cur->list);
		if (cur->private_data != NULL) { kfree(cur->private_data); }
		kfree(cur);
		if (thread->status == TASK_BLOCKED || thread->status == TASK_WAITING ||
			thread->status == TASK_HANGING) {
			thread_unblock(thread);
		}
	}

	spin_unlock(&wqm->lock);
	store_interrupt_status(old_status);
	return;
}
