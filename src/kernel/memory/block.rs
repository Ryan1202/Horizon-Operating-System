use core::{cell::SyncUnsafeCell, ffi::c_void, mem::MaybeUninit};

use crate::{
    kernel::memory::{
        buddy::BuddyAllocator,
        page::{Page, page_align_up},
    },
    lib::rust::spinlock::Spinlock,
};

#[repr(C)]
pub struct E820Ards {
    pub base_addr: u64,
    pub length: u64,
    pub block_type: u32,
}

#[unsafe(no_mangle)]
pub static mut PREALLOCATED_END_PHY: usize = 0;

/// Buddy 分配器的虚拟地址存储位置
///
/// 使用SyncUnsafeCell实现内部可变性，由Spinlock真正保证线程安全。
/// 为了简化获取过程，MaybeUninit并不被Spinlock包裹，所以只能在单线程环境下初始化保证安全
static PAGE_MANAGER_VIR: SyncUnsafeCell<MaybeUninit<Spinlock<BuddyAllocator>>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());

unsafe extern "C" {
    static VIR_BASE: *const c_void;
}

pub static mut VIR_BASE_ADDR: usize = 0;

#[unsafe(no_mangle)]
pub unsafe extern "C" fn page_early_init(
    blocks: *mut E820Ards,
    block_count: u16,
    kernel_start: usize,
    kernel_end: usize,
) {
    unsafe {
        // 都向后对齐到页
        // 只是为了看着稍微舒服一点
        PREALLOCATED_END_PHY = page_align_up(PREALLOCATED_END_PHY.max(kernel_end));

        VIR_BASE_ADDR = &VIR_BASE as *const _ as usize;

        // {
        //     let page_manager = page_manager();
        //     let mut guard = page_manager.lock();

        //     let ptr = with_exposed_provenance_mut(PREALLOCATED_END_PHY + VIR_BASE_ADDR);

        //     *guard =
        //         NonNull::new(ptr).expect("Failed to create NonNull pointer for PAGE_MANAGER_VIR: PAGE_MANAGER_VIR should not be null!");
        // }

        Page::init(blocks, block_count, (kernel_start, PREALLOCATED_END_PHY));

        PREALLOCATED_END_PHY += page_align_up(size_of::<BuddyAllocator>());
    }
}

pub fn page_manager<'a>() -> &'a mut Spinlock<BuddyAllocator> {
    unsafe { (*PAGE_MANAGER_VIR.get()).assume_init_mut() }
}

#[unsafe(no_mangle)]
pub extern "C" fn page_init() {
    unsafe {
        let page_manager = PAGE_MANAGER_VIR.get();
        *page_manager = MaybeUninit::new(Spinlock::new(BuddyAllocator::empty()));
        (*page_manager).assume_init_mut().lock().init();
    }
}

// 内核启动早期分配的页都是不会释放的，如页表结构等
#[unsafe(no_mangle)]
pub unsafe extern "C" fn early_allocate_pages(count: u8) -> usize {
    unsafe {
        let addr = PREALLOCATED_END_PHY;
        PREALLOCATED_END_PHY += (count as usize) * 0x1000;
        addr
    }
}
