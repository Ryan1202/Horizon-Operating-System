use core::{
    ptr::null_mut,
    sync::atomic::{AtomicBool, AtomicPtr, AtomicU8, Ordering},
};

use crate::kernel::thread::Thread;

pub static CPU_RUN_STATE: CpuRunState = CpuRunState::new();

pub struct CpuRunState {
    /// 当前正在运行的线程。线程引用本身由调度器替换，线程对象必须在 CPU 上保持有效。
    current_thread: AtomicPtr<Thread>,
    irq_depth: AtomicU8,
    preempt_count: AtomicU8,
    need_reschedule: AtomicBool,
}

impl CpuRunState {
    const fn new() -> Self {
        Self {
            current_thread: AtomicPtr::new(null_mut()),
            irq_depth: AtomicU8::new(0),
            preempt_count: AtomicU8::new(0),
            need_reschedule: AtomicBool::new(false),
        }
    }

    pub(super) fn initialize(&self, current_thread: &'static Thread) {
        self.current_thread.store(
            current_thread as *const Thread as *mut Thread,
            Ordering::Relaxed,
        );
    }

    pub(super) fn current_thread(&self) -> &Thread {
        unsafe {
            self.current_thread
                .load(Ordering::Relaxed)
                .as_ref()
                .unwrap()
        }
    }

    fn switch_thread(&self, next: &'static Thread) -> &Thread {
        unsafe {
            self.current_thread
                .swap(next as *const Thread as *mut Thread, Ordering::Relaxed)
                .as_ref()
                .unwrap()
        }
    }

    pub(super) fn disable_preemption(&self) {
        self.preempt_count.fetch_add(1, Ordering::Relaxed);
    }

    pub(super) fn enable_preemption(&self) {
        let _ = self
            .preempt_count
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |count| {
                count.checked_sub(1)
            });
    }

    pub(super) fn preempt_count(&self) -> u8 {
        self.preempt_count.load(Ordering::Relaxed)
    }

    pub(super) fn request_reschedule(&self) {
        self.need_reschedule.store(true, Ordering::Relaxed);
    }

    pub(super) fn clear_reschedule(&self) {
        self.need_reschedule.store(false, Ordering::Relaxed);
    }
}

/// 抢占保护器，默认禁止在其生命周期内发生线程切换，除非通过仅暴露给调度器的接口主动切换上下文。
pub struct PreemptGuard {
    run_state: &'static CpuRunState,
}

impl PreemptGuard {
    pub fn new() -> Self {
        CPU_RUN_STATE.disable_preemption();

        Self {
            run_state: &CPU_RUN_STATE,
        }
    }

    /// 切换上下文到下一个线程，并返回当前线程的引用。
    ///
    /// # Safety
    ///
    /// `next` 必须是被调度器独占的线程，且在调用期间禁止其他抢占。
    pub(super) unsafe fn switch_thread(&mut self, next: &'static Thread) -> &Thread {
        let current = self.run_state.switch_thread(next);

        unsafe { Thread::switch_context(current, next) };

        current
    }

    pub fn run_state(&self) -> &CpuRunState {
        self.run_state
    }

    pub fn preemptable(&self) -> bool {
        self.run_state.preempt_count() == 1
    }
}

impl Drop for PreemptGuard {
    fn drop(&mut self) {
        self.run_state.enable_preemption();
    }
}
