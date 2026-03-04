use core::{
    ffi::c_void,
    fmt::{Arguments, Write},
    ptr::addr_of,
};

use crate::{
    ConsoleOutput,
    arch::VirtAddr,
    kernel::memory::{frame::FrameError, page::PageTableError},
};

pub mod addr;
pub mod arch;
pub mod frame;
pub mod kmalloc;
pub mod page;
pub mod slub;
pub mod vmalloc;

unsafe extern "C" {
    static VIR_BASE: *const c_void;
}

#[inline(always)]
pub fn vir_base_addr() -> VirtAddr {
    VirtAddr::new(addr_of!(VIR_BASE) as usize)
}

const KLINEAR_SIZE: usize = 0x2000_0000;
const KMEMORY_END: VirtAddr = VirtAddr::new(0xff80_0000);

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
    InvalidAddress(VirtAddr),
    InvalidSize(usize),
    FrameError(FrameError),
    PageTableError(PageTableError),
}

impl MemoryError {
    pub fn log_error(&self, args: Arguments) {
        let mut output = ConsoleOutput;
        writeln!(output, "{}: MemoryError: {:?}", args, self).ok();
    }
}

impl From<FrameError> for MemoryError {
    fn from(value: FrameError) -> Self {
        MemoryError::FrameError(value)
    }
}

impl From<PageTableError> for MemoryError {
    fn from(value: PageTableError) -> Self {
        MemoryError::PageTableError(value)
    }
}
