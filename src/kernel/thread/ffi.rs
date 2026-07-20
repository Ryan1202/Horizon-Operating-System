use core::{
    ffi::{CStr, c_void},
    ptr::null,
};

use alloc::sync::Arc;

use crate::kernel::{
    interrupt::{self, PreemptPoint},
    memory::kmalloc::Kmalloc,
    thread::{THREAD_MANAGER, Thread, ThreadArc, scheduler::scheduler},
};

/// 创建一个由 ThreadManager 持有、尚未加入运行队列的线程。
///
/// 返回值是 borrowed pointer；若 C 需要在线程运行后继续持有它，必须在
/// thread_run 前调用 thread_get 获取 owning handle。
#[unsafe(export_name = "thread_create")]
extern "C" fn thread_create_c(
    name: *const i8,
    entry: extern "C" fn(*mut c_void),
    argument: *mut c_void,
) -> *const Thread {
    let name = unsafe { CStr::from_ptr(name) };
    let Ok(thread) = THREAD_MANAGER.try_new(name, entry, argument) else {
        return null();
    };

    Arc::as_ptr(&thread)
}

/// 为 thread_create 返回的 borrowed pointer 获取一个 owning handle。
#[unsafe(export_name = "thread_get")]
extern "C" fn thread_get_c(thread: *const Thread) -> *const Thread {
    let Some(thread_ref) = (unsafe { thread.as_ref() }) else {
        return null();
    };

    assert!(interrupt::in_thread(), "thread_get outside thread context");
    assert_eq!(
        thread_ref.state(),
        crate::kernel::thread::ThreadState::Registered,
        "borrowed thread pointer must be retained before thread_run"
    );

    // SAFETY: Registered 线程由 ThreadManager 持有，pointer 来自对应的 Arc。
    unsafe { Arc::<Thread, Kmalloc>::increment_strong_count_in(thread, Kmalloc::default()) };
    thread
}

/// 将一个由 thread_create 创建的 Registered 线程加入运行队列。
#[unsafe(export_name = "thread_run")]
extern "C" fn thread_run_c(thread: *const Thread) -> bool {
    let Some(thread) = (unsafe { thread.as_ref() }) else {
        return false;
    };

    assert!(interrupt::in_thread(), "thread_run outside thread context");
    scheduler().enqueue(thread).is_ok()
}

/// 释放一个由 thread_get 返回的 owning handle。
#[unsafe(export_name = "thread_put")]
extern "C" fn thread_put_c(thread: *const Thread) {
    if thread.is_null() {
        return;
    }

    assert!(interrupt::in_thread(), "thread_put outside thread context");

    // SAFETY: C 调用方必须传入一个 owning handle，并且每个 handle 只调用一次。
    let thread: ThreadArc = unsafe { Arc::from_raw_in(thread, Kmalloc::default()) };
    drop(thread);
}

#[unsafe(export_name = "thread_exit")]
extern "C" fn thread_exit_c() -> ! {
    scheduler().exit_self()
}

#[unsafe(export_name = "disable_preempt")]
extern "C" fn disable_preempt() {
    // SAFETY: C 调用方负责在同一 CPU 上通过 enable_preempt 配平。
    unsafe { scheduler().disable_preempt() };
}

#[unsafe(export_name = "enable_preempt")]
extern "C" fn enable_preempt() {
    // SAFETY: C 调用方必须已经在同一 CPU 上调用过 disable_preempt。
    unsafe { scheduler().enable_preempt() };
}

#[unsafe(no_mangle)]
extern "C" fn can_preempt() -> bool {
    scheduler().can_preempt() && interrupt::in_thread()
}

#[unsafe(no_mangle)]
extern "C" fn scheduler_tick(elapsed_ms: u16) {
    scheduler().tick(elapsed_ms);
}

#[unsafe(no_mangle)]
extern "C" fn try_yield() {
    let Some(point) = PreemptPoint::new() else {
        return;
    };

    scheduler().try_yield(point);
}
