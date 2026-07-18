use ::core::ptr::null_mut;

use alloc::sync::Arc;

use crate::kernel::memory::kmalloc::Kmalloc;

pub mod core;
mod ffi;
pub mod manager;
pub(super) mod scheduler;

use scheduler::SCHEDULER;

pub use core::{KernelThreadEntry, Thread, ThreadId, ThreadState};
pub use manager::{THREAD_MANAGER, ThreadManager};

pub type ThreadArc = Arc<Thread, Kmalloc>;

#[unsafe(no_mangle)]
extern "C" fn thread_manager_init(entry: KernelThreadEntry) {
    let current = THREAD_MANAGER
        .try_new(c"main", entry, null_mut())
        .expect("thread manager initialization failed");

    let current = Arc::into_raw_with_allocator(current).0;
    // SAFETY: ThreadManager 持有强引用，并且在上下文切换期间
    // 当前 CPU 独占初始线程。
    let current = unsafe { &*current };

    SCHEDULER.init(current);
    current.transition_to(ThreadState::Ready).unwrap();
    Thread::prepare_first_thread(current);
}
