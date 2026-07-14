use core::ffi::{CStr, c_void};

use alloc::vec::Vec;

use crate::{
    kernel::{
        memory::{MemoryError, kmalloc::Kmalloc},
        thread::{
            ThreadArc,
            core::{KernelThreadEntry, Thread, ThreadState},
            run_state::PreemptGuard,
            scheduler::Scheduler,
        },
    },
    lib::rust::spinlock::Spinlock,
};

pub static THREAD_MANAGER: ThreadManager = ThreadManager {
    all: Spinlock::new(Vec::new_in(Kmalloc::default())),
    scheduler: Scheduler::new(),
};

pub struct ThreadManager {
    all: Spinlock<Vec<ThreadArc, Kmalloc>>,
    pub scheduler: Scheduler,
}

impl ThreadManager {
    /// 创建并注册一个不可调度的内核线程。
    ///
    /// manager 会先取得强引用，再把状态从 `New` 改为 `Registered`。只有后续
    /// scheduler API 能继续执行 `Registered -> Ready`，因此调用方不能制造一个
    /// 已进入运行队列但没有 owner 的线程。
    pub fn try_new(
        &self,
        name: &'static CStr,
        entry: KernelThreadEntry,
        argument: *mut c_void,
    ) -> Result<ThreadArc, MemoryError> {
        let thread = Thread::new_kernel(name, entry, argument)?;
        let thread = ThreadArc::try_new_in(thread, Kmalloc::default())
            .map_err(|_| MemoryError::OutOfMemory)?;

        let mut all = self.all.lock();

        thread
            .as_ref()
            .transition_to(ThreadState::Registered)
            .expect("newly manager-owned thread must be in New state");

        all.push(thread.clone());

        Ok(thread)
    }

    /// 尝试直接让出 CPU 给下一个可运行线程
    pub fn schedule(&self) {
        let mut guard = PreemptGuard::new();

        let run_state = guard.run_state();
        if guard.preemptable() {
            run_state.clear_reschedule();

            self.do_schedule(&mut guard);
        } else {
            run_state.request_reschedule();
        }
    }

    /// 调度器选择下一个可运行线程并切换到它。
    fn do_schedule(&self, guard: &mut PreemptGuard) {
        let next = self.scheduler.next_eligible();
        if let Some(next) = next {
            let current = guard.run_state().current_thread();
            let next = unsafe { next.as_ref() };

            debug_assert_ne!(
                current as *const Thread, next as *const Thread,
                "Current Thread should not be the same as Next eligible Thread"
            );

            self.scheduler
                .enqueue(current)
                .expect("Current Thread should be state Running");
            self.scheduler
                .dequeue(next, ThreadState::Running)
                .expect("Next eligible Thread should be able to transition to state Running");

            unsafe {
                guard.switch_thread(next);
            }
        } else {
            todo!()
        }
    }
}
