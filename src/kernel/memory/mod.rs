use core::{
    ffi::c_void,
    fmt::{Arguments, Write},
};

use crate::{ConsoleOutput, kernel::memory::phy::frame::FrameError};

pub mod page;
pub mod phy;
pub mod vir;

unsafe extern "C" {
    static VIR_BASE: *const c_void;
}

pub static mut VIR_BASE_ADDR: usize = 0;

unsafe extern "C" {
    fn page_link(vaddr: usize, paddr: usize, page_count: u16, cache_type: PageCacheType) -> bool;
    fn page_unlink(vaddr: usize, page_count: u16);
    fn vir2phys(vaddr: usize) -> usize;
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

#[derive(Debug)]
pub enum MemoryError {
    OutOfMemory,
    AddressConflict,
    DoubleRelease,
    InvalidSize(usize),
    PageError(FrameError),
}

impl MemoryError {
    pub fn log_error(&self, args: Arguments) {
        let mut output = ConsoleOutput;
        writeln!(output, "{}: MemoryError: {:?}", args, self).ok();
    }
}
