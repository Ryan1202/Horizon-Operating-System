#![no_std]

mod page;

extern "C" {
    fn kmalloc(size: u32) -> *mut core::ffi::c_void;
    fn new_vir();
    fn new_phy();
}
