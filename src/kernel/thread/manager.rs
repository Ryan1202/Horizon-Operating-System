use core::{mem::ManuallyDrop, pin::Pin};

use alloc::sync::Arc;

use crate::{
    kernel::{
        memory::{MemoryError, kmalloc::Kmalloc},
        thread::{
            ThreadArc,
            core::{Thread, ThreadState},
        },
    },
    lib::rust::{list::ListHead, spinlock::Spinlock},
};

pub static THREAD_MANAGER: ThreadManager = ThreadManager {
    all: Spinlock::new(ListHead::empty()),
};

pub struct ThreadManager {
    all: Spinlock<ListHead<Thread>>,
}

impl ThreadManager {
    pub(super) fn init() {
        unsafe {
            let all = Pin::new_unchecked(&THREAD_MANAGER.all);
            all.lock_pinned().as_mut().init_pinned();
        }
    }

    /// 创建并持有一个已注册但尚不可调度的内核线程
    pub fn register(&self, thread: Thread) -> Result<ThreadArc, MemoryError> {
        let mut thread = ThreadArc::try_new_in(thread, Kmalloc::default())
            .map_err(|_| MemoryError::OutOfMemory)?;

        {
            let thread = Arc::get_mut(&mut thread).unwrap();

            thread.init();

            thread
                .transition_to(ThreadState::Registered)
                .expect("new manager-owned thread must be in New state");

            unsafe {
                let all = Pin::new_unchecked(&self.all);
                all.lock_pinned()
                    .as_mut()
                    .get_unchecked_mut()
                    .add_tail(thread.get_thread_node());
            };
        }

        let _ = ManuallyDrop::new(thread.clone());

        Ok(thread)
    }

    /// 移除已经退出调度系统的线程，并返回 manager 持有的强引用
    ///
    /// 返回值必须在 manager 锁外、且不再运行于该线程的内核栈上时释放
    pub(super) fn remove(&self, thread: &Thread) -> ThreadArc {
        assert!(
            thread.state() == ThreadState::Dead,
            "only exited thread can be removed"
        );
        unsafe {
            // SAFETY: THREAD_MANAGER 是全局变量，是 Pin 的，且在整个系统生命周期内不会被释放
            let all = Pin::new_unchecked(&self.all);
            let mut all = all.lock_pinned();

            all.as_mut().delete_pinned(thread.get_thread_node());

            // SAFETY: 只要 thread 在 ThreadManager 的 all 链表中，它就一定是由 ThreadManager 持有的 Arc
            Arc::from_raw_in(thread, Kmalloc::default())
        }
    }
}
