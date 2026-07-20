use ::core::{ffi::c_void, ptr::null_mut};

use alloc::sync::Arc;

use crate::{
    arch::ArchInterrupt,
    kernel::{interrupt::Interrupt, memory::kmalloc::Kmalloc, thread::scheduler::scheduler},
};

mod completion;
pub mod core;
mod ffi;
pub mod manager;
pub(super) mod scheduler;
pub mod wait_queue;

// 导出符号
pub use completion::Completion;
pub use core::{KernelThreadEntry, Thread, ThreadId, ThreadState};
pub use manager::{THREAD_MANAGER, ThreadManager};
pub use wait_queue::{WaitCondition, WaitQueue};

pub type ThreadArc = Arc<Thread, Kmalloc>;

#[unsafe(no_mangle)]
extern "C" fn thread_manager_init(entry: KernelThreadEntry) {
    let current_arc = THREAD_MANAGER
        .try_new(c"main", entry, null_mut())
        .expect("thread manager initialization failed");
    let idle_arc = THREAD_MANAGER
        .try_new(c"idle", idle, null_mut())
        .expect("idle thread initialization failed");

    let current_ptr = Arc::as_ptr(&current_arc);
    let idle_ptr = Arc::as_ptr(&idle_arc);

    // SAFETY: ThreadManager 在两个线程可被调度期间持有强引用。
    let current = unsafe { &*current_ptr };
    let idle = unsafe { &*idle_ptr };

    // idle 线程有一个单独的状态 Idle
    idle.transition_to(ThreadState::Idle).unwrap();

    scheduler().init(current, idle);
    current.transition_to(ThreadState::Ready).unwrap();

    Thread::prepare_first_thread(current);
}

extern "C" fn idle(_argument: *mut c_void) {
    loop {
        ArchInterrupt::wait();
    }
}
