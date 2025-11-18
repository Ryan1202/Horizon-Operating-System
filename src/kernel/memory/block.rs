use core::{
    ffi::c_void,
    ptr::{null_mut, with_exposed_provenance_mut},
};

use crate::kernel::memory::{buddy::BuddyAllocator, page::page_align_up, Page};

#[repr(C)]
pub struct E820Ards {
    pub base_addr: u64,
    pub length: u64,
    pub block_type: u32,
}

#[no_mangle]
pub static mut PREALLOCATED_END_PHY: usize = 0;

static mut PAGE_MANAGER_VIR: *mut BuddyAllocator = null_mut();

extern "C" {
    static VIR_BASE: *const c_void;
}

pub static mut VIR_BASE_ADDR: usize = 0;

#[no_mangle]
pub unsafe extern "C" fn page_early_init(
    blocks: *mut E820Ards,
    block_count: u16,
    kernel_start: usize,
    kernel_end: usize,
) {
    // 都向后对齐到页
    // 只是为了看着稍微舒服一点
    PREALLOCATED_END_PHY = page_align_up(PREALLOCATED_END_PHY.max(kernel_end));

    VIR_BASE_ADDR = &VIR_BASE as *const _ as usize;

    PAGE_MANAGER_VIR = with_exposed_provenance_mut(PREALLOCATED_END_PHY + VIR_BASE_ADDR);
    Page::init(blocks, block_count, (kernel_start, kernel_end));

    PREALLOCATED_END_PHY += page_align_up(size_of::<BuddyAllocator>());
}

pub unsafe fn page_manager() -> Option<&'static mut BuddyAllocator> {
    PAGE_MANAGER_VIR.as_mut()
}

#[no_mangle]
pub unsafe extern "C" fn page_init() {
    let page_manager = page_manager().unwrap();
    page_manager.init();
}

// 内核启动早期分配的页都是不会释放的，如页表结构等
#[no_mangle]
pub unsafe extern "C" fn early_allocate_pages(count: u8) -> usize {
    let addr = PREALLOCATED_END_PHY;
    PREALLOCATED_END_PHY += (count as usize) * 0x1000;
    addr
}
