use core::{
    ffi::{CStr, c_void},
    ptr::null,
};

use alloc::sync::Arc;

use crate::kernel::{
    interrupt::{self, PreemptPoint},
    thread::{THREAD_MANAGER, Thread, scheduler::SCHEDULER},
};

#[unsafe(export_name = "thread_start")]
extern "C" fn thread_start_c(
    name: *const i8,
    entry: extern "C" fn(*mut c_void),
    argument: *mut c_void,
) -> *const Thread {
    let name = unsafe { CStr::from_ptr(name) };
    let Ok(thread) = THREAD_MANAGER.try_new(name, entry, argument) else {
        return null();
    };

    SCHEDULER
        .enqueue(thread.as_ref())
        .map_or(null(), |_| Arc::into_raw_with_allocator(thread).0)
}

#[unsafe(export_name = "disable_preempt")]
extern "C" fn disable_preempt() {
    // SAFETY: C 调用方负责在同一 CPU 上通过 enable_preempt 配平。
    unsafe { SCHEDULER.disable_preempt() };
}

#[unsafe(export_name = "enable_preempt")]
extern "C" fn enable_preempt() {
    // SAFETY: C 调用方必须已经在同一 CPU 上调用过 disable_preempt。
    unsafe { SCHEDULER.enable_preempt() };
}

#[unsafe(no_mangle)]
extern "C" fn can_preempt() -> bool {
    SCHEDULER.can_preempt() && interrupt::in_thread()
}

#[unsafe(no_mangle)]
extern "C" fn scheduler_tick(elapsed_ms: u16) {
    SCHEDULER.tick(elapsed_ms);
}

#[unsafe(no_mangle)]
extern "C" fn try_yield() {
    let Some(point) = PreemptPoint::new() else {
        return;
    };

    SCHEDULER.try_yield(point);
}
