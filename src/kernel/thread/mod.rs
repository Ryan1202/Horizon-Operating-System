use ::core::{ffi::c_void, ptr::null_mut};

use alloc::sync::Arc;

use crate::{
    arch::ArchInterrupt,
    kernel::{
        interrupt::Interrupt,
        memory::kmalloc::Kmalloc,
        thread::scheduler::{PreemptGuard, Scheduler, scheduler},
    },
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
    ThreadManager::init();

    let current = Thread::new_kernel(c"main", entry, null_mut())
        .and_then(|thread| THREAD_MANAGER.register(thread))
        .expect("thread manager initialization failed");
    let idle = Thread::new_kernel(c"idle", idle, null_mut())
        .and_then(|thread| THREAD_MANAGER.register(thread))
        .expect("idle thread initialization failed");

    // SAFETY: ThreadManager 在两个线程可被调度期间持有强引用。
    let current = unsafe { &*Arc::as_ptr(&current) };
    let idle = unsafe { &*Arc::as_ptr(&idle) };

    // idle 线程有一个单独的状态 Idle
    idle.transition_to(ThreadState::Idle).unwrap();

    let guard = PreemptGuard::new();
    scheduler(&guard).init(current, idle);
    current.transition_to(ThreadState::Ready).unwrap();

    Scheduler::start_first(guard, current)
}

extern "C" fn idle(_argument: *mut c_void) {
    loop {
        ArchInterrupt::wait();
    }
}
