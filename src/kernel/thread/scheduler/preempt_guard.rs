use core::mem;

use crate::{
    cpu_local,
    kernel::{
        interrupt::PreemptPoint,
        memory::percpu::{PerCpuReadWrite, PerCpuScalar},
        thread::scheduler::{RESCHED, Scheduler},
    },
};

cpu_local!(
    /// 抢占计数，当计数为 0 时，当前线程可以被抢占
    pub static PREEMPT_COUNT: u8 = 0;
);

#[must_use = "the scheduler preemption count must be restored"]
pub struct PreemptGuard {
    can_switch: bool,
}

impl !Send for PreemptGuard {}
impl !Sync for PreemptGuard {}

impl PreemptGuard {
    pub fn new() -> Self {
        let previous = PREEMPT_COUNT.fetch_add(1);

        Self {
            can_switch: previous == 0,
        }
    }

    /// 将 guard 转换为由当前 CPU 抢占计数表示的 raw 状态。
    ///
    /// 该操作只允许紧邻上下文切换使用；调用后必须由目标栈通过
    /// `from_raw` 恢复唯一的 RAII guard。
    pub(super) fn into_raw(self) {
        assert!(
            self.can_switch,
            "only a switch-capable guard can be transferred"
        );
        assert_eq!(
            PREEMPT_COUNT.read(),
            1,
            "raw preempt handoff requires exactly one outstanding guard"
        );
        mem::forget(self);
    }

    /// 接管上下文切换时由前一个线程栈传递的 raw 抢占状态。
    ///
    /// # Safety
    ///
    /// 当前 CPU 的抢占计数必须恰好包含一次由 `into_raw` 留下的未配平计数，
    /// 且该 raw 状态尚未被其他 guard 接管。
    pub(super) unsafe fn from_raw() -> Self {
        assert_eq!(PREEMPT_COUNT.read(), 1, "invalid raw preempt handoff");
        Self { can_switch: true }
    }

    /// 检查当前是否可以切换上下文
    pub fn can_switch(&self) -> bool {
        self.can_switch
    }

    /// 永久切离已经进入 Dead 状态的当前线程。
    pub fn exit_current(self) -> ! {
        assert!(
            crate::kernel::interrupt::in_thread(),
            "thread_exit outside thread context"
        );
        assert!(
            self.can_switch(),
            "thread_exit while preemption is disabled"
        );

        Scheduler::exit_current(self)
    }

    pub fn try_preempt(self, _point: PreemptPoint) {
        if RESCHED.read() == 0 || !self.can_switch() {
            return;
        }

        let _ = Scheduler::schedule(self);
    }

    pub fn try_yield(self, _point: PreemptPoint) {
        if !self.can_switch() {
            return;
        }

        let _ = Scheduler::schedule(self);
    }
}

impl Drop for PreemptGuard {
    fn drop(&mut self) {
        unsafe { enable_preempt() };
    }
}

pub fn can_preempt() -> bool {
    PREEMPT_COUNT.read() == 0
}

pub unsafe fn disable_preempt() {
    assert!(PREEMPT_COUNT.read() < u8::MAX, "preempt count overflow");
    PREEMPT_COUNT.increase();
}

pub unsafe fn enable_preempt() {
    assert!(PREEMPT_COUNT.read() > 0, "unbalanced enable_preempt");
    let previous = PREEMPT_COUNT.fetch_sub(1);

    // 如果之前的计数为 1，说明当前线程已经可以被抢占了，并且有调度请求挂起，那么就尝试进行抢占。
    if previous == 1 && RESCHED.read() != 0 {
        if let Some(point) = PreemptPoint::new() {
            point.try_preempt(PreemptGuard::new());
        }
    }
}
