#[cfg(target_arch = "x86")]
pub mod x86 {
    pub mod kernel {
        pub mod page;
    }
}

#[cfg(target_arch = "x86")]
pub use x86::kernel::page::{
    PAGE_SIZE, PageLevelEntry,
    addr::{PhyAddr, VirtAddr},
};
