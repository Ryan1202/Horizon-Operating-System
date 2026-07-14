use core::{
    ffi::{CStr, c_void},
    ptr::null,
};

use alloc::sync::Arc;

use crate::kernel::thread::{THREAD_MANAGER, Thread};

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

    THREAD_MANAGER
        .scheduler
        .enqueue(thread.as_ref())
        .map_or(null(), |_| Arc::into_raw_with_allocator(thread).0)
}
