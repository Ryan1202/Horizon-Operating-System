use core::{
    ffi::c_void,
    fmt::{Arguments, Write},
    ptr::addr_of,
};

use crate::{ConsoleOutput, kernel::memory::frame::FrameError};

pub mod frame;
pub mod kmalloc;
pub mod page;
pub mod slub;
pub mod vmalloc;

unsafe extern "C" {
    static VIR_BASE: *const c_void;
}

#[inline(always)]
pub fn vir_base_addr() -> usize {
    addr_of!(VIR_BASE) as usize
}

const KLINEAR_SIZE: usize = 0x2000_0000;

unsafe extern "C" {
    fn page_link(vaddr: usize, paddr: usize, page_count: u16, cache_type: PageCacheType) -> bool;
    fn page_unlink(vaddr: usize, page_count: u16);
}

#[derive(Clone, Copy, Debug)]
#[repr(u8)]
pub enum PageCacheType {
    WriteBack = 0,
    WriteCombine = 1,
    WriteThrough = 2,
    Uncached = 3,
    UncachedMinus = 4,
}

#[derive(Debug, Clone)]
pub enum MemoryError {
    OutOfMemory,
    AddressConflict,
    MultipleFree,
    InvalidAddress(usize),
    InvalidSize(usize),
    FrameError(FrameError),
}

impl MemoryError {
    pub fn log_error(&self, args: Arguments) {
        let mut output = ConsoleOutput;
        writeln!(output, "{}: MemoryError: {:?}", args, self).ok();
    }
}
