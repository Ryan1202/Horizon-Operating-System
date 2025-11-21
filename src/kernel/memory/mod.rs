use core::ffi::c_void;

mod physical;

unsafe extern "C" {
    static VIR_BASE: *const c_void;
}

pub static mut VIR_BASE_ADDR: usize = 0;
