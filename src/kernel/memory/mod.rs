use core::ffi::c_void;

pub mod phy;
pub mod vir;

unsafe extern "C" {
    static VIR_BASE: *const c_void;
}

pub static mut VIR_BASE_ADDR: usize = 0;
