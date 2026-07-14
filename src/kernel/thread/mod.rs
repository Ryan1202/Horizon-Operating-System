use alloc::sync::Arc;

use crate::kernel::{memory::kmalloc::Kmalloc, thread::run_state::CPU_RUN_STATE};

pub mod core;
mod ffi;
pub mod manager;
mod run_state;
mod scheduler;

use ::core::ptr::null_mut;
pub use core::{KernelThreadEntry, Thread, ThreadId, ThreadState};
pub use manager::{THREAD_MANAGER, ThreadManager};

pub type ThreadArc = Arc<Thread, Kmalloc>;

#[unsafe(no_mangle)]
extern "C" fn thread_manager_init(entry: KernelThreadEntry) {
    let current_thread = THREAD_MANAGER
        .try_new(c"main", entry, null_mut())
        .expect("Initialize Thread Management failed!");

    let current_thread = Arc::into_raw_with_allocator(current_thread).0;
    // SAFETY: `current_thread` 是当前 CPU 上独占的线程，且在 CPU 上的线程切换期间保持有效。
    let current_thread = unsafe { &*current_thread };

    CPU_RUN_STATE.disable_preemption();
    CPU_RUN_STATE.initialize(current_thread);

    THREAD_MANAGER.scheduler.init();

    current_thread.transition_to(ThreadState::Ready).unwrap();

    Thread::prepare_first_thread(current_thread);
}
