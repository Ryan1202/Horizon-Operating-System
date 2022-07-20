/**
 * @file sync.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 参考《操作系统真象还原》的实现
 * @version 0.1
 * @date 2021-07
 */
#include <kernel/console.h>
#include <kernel/func.h>
#include <kernel/sync.h>

void sema_init(struct semaphore *psema, uint8_t value)
{
    psema->value = value;
    list_init(&psema->waiters);
}

void lock_init(struct lock *plock)
{
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_init(&plock->semaphore, 1);
}

void sema_down(struct semaphore *psema)
{
    int old_status = io_load_eflags();
    struct task_s *cur_thread = get_current_thread();
    while(psema->value == 0)
    {
        if (list_find(&cur_thread->general_tag, &psema->waiters))
        {
            printk("sema_down: thread blocked has benn in waiters_list\n");
        }

        list_add_tail(&cur_thread->general_tag, &psema->waiters);
        thread_block(TASK_BLOCKED);
    }
    psema->value--;
    io_store_eflags(old_status);
}

void sema_up(struct semaphore *psema)
{
    int old_status = io_load_eflags();
    if(!list_empty(&psema->waiters))
    {
        struct task_s *thread_blocked = list_owner(&psema->waiters, struct task_s, general_tag);
        thread_unblock(thread_blocked);
    }
    psema->value++;
    io_store_eflags(old_status);
}

void lock_acquire(struct lock *plock)
{
    if(plock->holder != get_current_thread())
    {
        sema_down(&plock->semaphore);
        plock->holder = get_current_thread();
        plock->holder_repeat_nr = 1;
    }
    else
    {
        plock->holder_repeat_nr++;
    }
}

void lock_release(struct lock *plock)
{
    if(plock->holder_repeat_nr > 1)
    {
        plock->holder_repeat_nr--;
        return;
    }
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);
}