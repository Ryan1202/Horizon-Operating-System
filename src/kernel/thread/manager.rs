use core::{
    ffi::{CStr, c_void},
    ptr,
};

use alloc::vec::Vec;

use crate::{
    kernel::{
        memory::{MemoryError, kmalloc::Kmalloc},
        thread::{
            ThreadArc,
            core::{KernelThreadEntry, Thread, ThreadState},
        },
    },
    lib::rust::spinlock::Spinlock,
};

pub static THREAD_MANAGER: ThreadManager = ThreadManager {
    all: Spinlock::new(Vec::new_in(Kmalloc::default())),
};

pub struct ThreadManager {
    all: Spinlock<Vec<ThreadArc, Kmalloc>>,
}

impl ThreadManager {
    /// 创建并持有一个已注册但尚不可调度的内核线程。
    pub fn try_new(
        &self,
        name: &'static CStr,
        entry: KernelThreadEntry,
        argument: *mut c_void,
    ) -> Result<ThreadArc, MemoryError> {
        let thread = Thread::new_kernel(name, entry, argument)?;
        let thread = ThreadArc::try_new_in(thread, Kmalloc::default())
            .map_err(|_| MemoryError::OutOfMemory)?;

        thread
            .as_ref()
            .transition_to(ThreadState::Registered)
            .expect("new manager-owned thread must be in New state");

        self.all.lock().push(thread.clone());

        Ok(thread)
    }

    /// 移除已经退出调度系统的线程，并返回 manager 持有的强引用。
    ///
    /// 返回值必须在 manager 锁外、且不再运行于该线程的内核栈上时释放。
    pub(super) fn remove(&self, thread: &Thread) -> ThreadArc {
        let mut all = self.all.lock();
        let position = all
            .iter()
            .position(|candidate| ptr::eq(candidate.as_ref(), thread))
            .expect("dead thread must be registered in ThreadManager");

        all.swap_remove(position)
    }
}
