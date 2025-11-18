mod block;
mod buddy;
mod page;
mod slub;

pub use page::Page;

extern "C" {
    fn kmalloc(size: u32) -> *mut core::ffi::c_void;

}
