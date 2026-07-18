use ::core::{ffi::c_void, ptr::null_mut};

use alloc::sync::Arc;

use crate::{
    arch::ArchInterrupt,
    kernel::{interrupt::Interrupt, memory::kmalloc::Kmalloc, thread::scheduler::SCHEDULER},
};

pub mod core;
mod ffi;
pub mod manager;
pub(super) mod scheduler;

// 导出符号
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

    let idle = THREAD_MANAGER
        .try_new(c"idle", idle, null_mut())
        .expect("idle thread initialization failed");

    let (idle, _) = Arc::into_raw_with_allocator(idle);
    let idle = unsafe { &*idle };
    // idle 线程有一个单独的状态 Idle
    idle.transition_to(ThreadState::Idle).unwrap();

    SCHEDULER.init(current, idle);
    current.transition_to(ThreadState::Ready).unwrap();
    Thread::prepare_first_thread(current);
}

extern "C" fn idle(_argument: *mut c_void) {
    loop {
        ArchInterrupt::wait();
    }
}
