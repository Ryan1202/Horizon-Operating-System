//! C 驱动兼容入口；DeviceIrq 为不透明、独占管理的 Rust handle

use crate::{
    arch::request_device_irq,
    kernel::{
        interrupt::irq::{IrqError, IrqHandle, IrqHandler, IrqNumber, IrqSharing},
        memory::kmalloc::Kmalloc,
    },
};
use alloc::{boxed::Box, sync::Arc};
use core::{
    ffi::{c_int, c_void},
    ptr::null_mut,
};

struct Callback {
    function: unsafe extern "C" fn(*mut c_void),
    arg: *mut c_void,
}

// SAFETY: C 调用者保证 arg 活到同步注销返回，并自行同步设备的共享状态。
unsafe impl Send for Callback {}
unsafe impl Sync for Callback {}

impl IrqHandler for Callback {
    fn handle(&self, _: IrqNumber) -> Option<()> {
        unsafe { (self.function)(self.arg) };
        // 旧 C void 回调没有 handled 返回值；兼容接口将执行视为已处理。
        Some(())
    }
}

// 数值与 `include/kernel/driver.h` 的 DriverResult 保持一致
fn error(error: IrqError) -> c_int {
    match error {
        IrqError::InvalidIrqNumber(_) | IrqError::InvalidHardwareIrq(_) => 7,
        IrqError::NotFound => 3,
        IrqError::Busy | IrqError::ExclusiveViolation => 13,
        IrqError::OutOfMemory(_) => 8,
        IrqError::Unsupported => 12,
        IrqError::InvalidArgument => 16,
        _ => 18,
    }
}

/// # Safety
///
/// `out` 可写，回调及 `arg` 在注销返回前有效；同一 handle 的管理调用须串行化
#[unsafe(no_mangle)]
pub unsafe extern "C" fn register_device_irq(
    out: *mut *mut IrqHandle,
    _physical_device: *mut c_void,
    arg: *mut c_void,
    irq: c_int,
    domain: *const c_void,
    function: Option<unsafe extern "C" fn(*mut c_void)>,
    mode: c_int,
) -> c_int {
    if out.is_null() {
        return 10;
    }
    unsafe { out.write(null_mut()) };

    let Some(function) = function else {
        return 10;
    };

    let sharing = match mode {
        0 => IrqSharing::Shared,
        1 => IrqSharing::Exclusive,
        _ => return 16,
    };

    let callback = match Arc::try_new_in(Callback { function, arg }, Kmalloc::default()) {
        Ok(callback) => callback,
        Err(_) => return 8,
    };

    let handle = match request_device_irq(irq, domain, sharing, callback) {
        Ok(handle) => handle,
        Err(reason) => return error(reason),
    };

    match Box::<_, Kmalloc>::try_new_in(handle, Kmalloc::default()) {
        Ok(handle) => {
            unsafe { out.write(Box::into_raw_with_allocator(handle).0) };
            0
        }
        Err(_) => 8,
    }
}

/// # Safety
///
/// handle 是尚未注销且由调用者独占管理的 register_device_irq 返回值
#[unsafe(no_mangle)]
pub unsafe extern "C" fn enable_device_irq(handle: *mut IrqHandle) -> c_int {
    let Some(handle) = (unsafe { handle.as_mut() }) else {
        return 10;
    };

    handle.enable_irq();

    0
}

/// # Safety
///
/// 与 enable_device_irq 相同；返回前同步排空此 action
#[unsafe(no_mangle)]
pub unsafe extern "C" fn disable_device_irq(handle: *mut IrqHandle) -> c_int {
    let Some(handle) = (unsafe { handle.as_mut() }) else {
        return 10;
    };

    handle.disable_irq();

    0
}

/// # Safety
/// 独占消费有效 handle，返回后不能再使用它
#[unsafe(no_mangle)]
pub unsafe extern "C" fn unregister_device_irq(handle: *mut IrqHandle) -> c_int {
    if handle.is_null() {
        return 10;
    }

    unsafe { drop(Box::<_, Kmalloc>::from_raw_in(handle, Kmalloc::default())) };

    0
}
