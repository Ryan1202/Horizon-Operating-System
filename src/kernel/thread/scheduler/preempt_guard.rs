use core::sync::atomic::Ordering;

use crate::kernel::thread::{Thread, ThreadArc, scheduler::Scheduler};

#[must_use = "the scheduler preemption count must be restored"]
pub struct PreemptGuard<'a> {
    pub(super) scheduler: &'a Scheduler,
    can_switch: bool,
}

impl<'a> PreemptGuard<'a> {
    pub fn new(scheduler: &'a Scheduler) -> Self {
        Self {
            scheduler,
            can_switch: unsafe { scheduler.disable_preempt() } == 0,
        }
    }

    /// 接管首次上下文切换时由前一个线程传递过来的抢占计数。
    ///
    /// # Safety
    ///
    /// 当前 CPU 的抢占计数必须恰好包含一次尚未配平的上下文切换计数。
    pub(super) unsafe fn from_first_switch(scheduler: &'a Scheduler) -> Self {
        assert_eq!(
            scheduler.preempt_count.load(Ordering::Relaxed),
            1,
            "invalid preempt count on first switch"
        );
        Self {
            scheduler,
            can_switch: true,
        }
    }

    /// 检查当前是否可以切换上下文
    pub fn can_switch(&self) -> bool {
        self.can_switch
    }

    /// 获取当前正在运行的线程
    pub fn current(&self) -> &'static Thread {
        self.scheduler.current()
    }

    /// 切换到指定的线程上下文
    ///
    /// # Safety
    ///
    /// `next` 必须由调度器独占，并且已经从就绪队列转换为 Running，
    /// 并且需要关闭中断确保中间不会发生其他线程切换以及在中间状态被打断
    pub(in crate::kernel::thread) unsafe fn switch_thread(
        &mut self,
        next: &'static Thread,
    ) -> Option<ThreadArc> {
        let current = self.current();

        self.scheduler.prepare_switch(current, next);
        unsafe { Thread::switch_context(current, next) };

        self.scheduler.finish_switch(self)
    }

    /// 永久切离已经进入 Dead 状态的当前线程。
    ///
    /// # Safety
    ///
    /// 调用期间必须关闭中断，next 必须已经转换为 Running。
    pub(super) unsafe fn exit_to(self, next: &'static Thread) -> ! {
        let current = self.current();

        self.scheduler.prepare_switch(current, next);
        unsafe { Thread::switch_context(current, next) };
        panic!("Dead thread resumed after context switch")
    }
}

impl Drop for PreemptGuard<'_> {
    fn drop(&mut self) {
        unsafe { self.scheduler.enable_preempt() };
    }
}
