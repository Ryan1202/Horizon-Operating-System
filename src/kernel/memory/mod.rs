use crate::{
    arch::{PhysAddr, VirtAddr},
    kernel::memory::{frame::FrameError, page::PageTableError},
};

pub mod addr;
pub mod arch;
pub mod frame;
pub mod kmalloc;
pub mod page;
pub mod slub;
pub mod vmalloc;

// 线性映射区 64TB
pub const KLINEAR_SIZE: usize = 0x0000_4000_0000_0000;
pub const KLINEAR_BASE: VirtAddr = VirtAddr::new(0xFFFF_8800_0000_0000);
pub const KLINEAR_END: VirtAddr = KLINEAR_BASE + KLINEAR_SIZE;

// vmalloc区 32TB
pub const VMALLOC_SIZE: usize = 0x0000_2000_0000_0000;
pub const VMALLOC_BASE: VirtAddr = VirtAddr::new(0xFFFF_D000_0000_0000);
pub const VMALLOC_END: VirtAddr = VMALLOC_BASE + VMALLOC_SIZE;

// vmemmap区 4TB
pub const VMEMMAP_SIZE: usize = 0x0000_0400_0000_0000;
pub const VMEMMAP_BASE: VirtAddr = VirtAddr::new(0xFFFF_F800_0000_0000);
pub const VMEMMAP_END: VirtAddr = VMEMMAP_BASE + VMEMMAP_SIZE;

// 内核代码区 512MB
pub const KERNEL_SIZE: usize = 0x0000_0000_2000_0000;
pub const KERNEL_BASE: VirtAddr = VirtAddr::new(0xFFFF_FFFF_8000_0000);
pub const KERNEL_END: VirtAddr = KERNEL_BASE + KERNEL_SIZE;

const ROOT_PT_LINEAR: *const usize = 0xFFFF_8800_0000_3000 as *const usize;
const EARLY_ROOT_PT_VIR: *const usize = 0xFFFFFFFF80003000 as *const usize;
const EARLY_ROOT_PT_PHY: PhysAddr = PhysAddr::new(0x3000);

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
    UnavailableFrame,
    InvalidAddress(VirtAddr),
    InvalidSize(usize),
    FrameError(FrameError),
    PageTableError(PageTableError),
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
