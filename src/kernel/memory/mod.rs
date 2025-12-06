use core::ffi::c_void;

pub mod phy;
pub mod vir;

unsafe extern "C" {
    static VIR_BASE: *const c_void;
}

pub static mut VIR_BASE_ADDR: usize = 0;

unsafe extern "C" {
    fn page_link(vaddr: usize, paddr: usize, page_count: u16, cache_type: PageCacheType) -> bool;
}

#[repr(u8)]
pub enum PageCacheType {
    WriteBack = 0,
    WriteCombine = 1,
    WriteThrough = 2,
    Uncached = 3,
    UncachedMinus = 4,
}
