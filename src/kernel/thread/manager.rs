use core::ffi::{CStr, c_void};

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
}
