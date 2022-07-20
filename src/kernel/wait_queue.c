/**
 * @file wait_queue.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 等待队列
 * @version 0.1
 * @date 2022-07-20
 */

#include <kernel/memory.h>
#include <kernel/wait_queue.h>

/**
 * @brief 创建一个等待队列
 * 
 * @return wait_queue_manager_t* 等待队列的管理结构
 */
wait_queue_manager_t *create_wait_queue(void)
{
	wait_queue_manager_t *wqm = (wait_queue_manager_t *)kmalloc(sizeof(wait_queue_manager_t));
	return wqm;
}

/**
 * @brief 初始化等待队列
 * 
 * @param wqm 等待队列管理结构
 */
void wait_queue_init(wait_queue_manager_t *wqm)
{
	spinlock_init(&wqm->lock);
	list_init(&wqm->list_head);
	return;
}

/**
 * @brief 添加当前任务到等待队列
 * 
 * @param wqm 等待队列管理结构
 */
void wait_queue_add(wait_queue_manager_t *wqm)
{
	if (list_empty(&wqm->list_head))
	{
		return;
	}
	wait_queue_t *wq = (wait_queue_t *)kmalloc(sizeof(wait_queue_t));
	wq->thread = get_current_thread();
	spin_lock(&wqm->lock);
	list_add_tail(&wq->list, &wqm->list_head);
	spin_unlock(&wqm->lock);
	thread_block(TASK_WAITING);
	return;
}

/**
 * @brief 唤醒等待队列中的第一个任务
 * 
 * @param wqm 等待队列管理结构
 */
void wait_queue_wakeup(wait_queue_manager_t *wqm)
{
	if (list_empty(&wqm->list_head))
	{
		return;
	}
	wait_queue_t *wq = list_first_owner(&wqm->list_head, wait_queue_t, list);
	struct task_s *thread = wq->thread;
	spin_lock(&wqm->lock);
	list_del(&wq->list);
	kfree(wq);
	spin_unlock(&wqm->lock);
	thread_unblock(thread);
	return;
}

/**
 * @brief 唤醒等待队列中的所有任务
 * 
 * @param wqm 等待队列管理结构
 */
void wait_queue_wakeup_all(wait_queue_manager_t *wqm)
{
	if (list_empty(&wqm->list_head))
	{
		return;
	}
	wait_queue_t *cur, *next;
	struct task_s *thread;
	spin_lock(&wqm->lock);
	list_for_each_owner_safe(cur, next, &wqm->list_head, list)
	{
		thread = cur->thread;
		list_del(&cur->list);
		kfree(cur);
		thread_unblock(thread);
	}
	spin_unlock(&wqm->lock);
	return;
}
