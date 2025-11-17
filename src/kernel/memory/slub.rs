// Basic SLUB-like structures for the kernel (简单骨架)
// - 忽略 NUMA
// - 使用从 C 导入的自旋锁（见 src/include/kernel/spinlock.h）
// 这是最小实现：结构体和基本方法（init/alloc/free stub），便于后续扩展。

use core::{num::NonZeroU16, ptr::NonNull};

use crate::{
    kernel::memory::{
        block::{page_manager, VIR_BASE_ADDR},
        buddy::PageOrder,
        page::{PageAllocator, ZoneType, PAGE_SIZE},
        slub::mem_cache::MemCaches,
        Page,
    },
    lib::rust::{list::ListNode, spinlock::Spinlock},
};

mod config;
mod mem_cache;

pub(self) const MIN_OBJECT_SIZE: usize = 8;
pub(self) const MAX_OBJECT_SIZE: usize = 4096;
pub(self) const MAX_PAGE_ORDER: PageOrder = PageOrder::new(3); // 最大 8 页
pub(self) const MIN_PARTIAL: u8 = 5;
pub(self) const MAX_PARTIAL: u8 = 10;

pub(self) const ALIGN: usize = core::mem::size_of::<usize>();

pub struct Slub {
    pub list: ListNode<Slub>,
    /// 全局自旋锁保护全局操作
    inner: Spinlock<SlubInner>,
    /// object总数量
    object_num: NonZeroU16,
    /// 分配的页结构体指针
    pages: NonNull<Page>,
}

/// Slub 内部数据结构
pub struct SlubInner {
    /// 空闲对象头
    freelist: *mut FreeNode,
    /// 正在使用的对象数量
    inuse: u16,
}

pub struct FreeNode {
    pub next: *mut FreeNode,
}
const _: () = assert!(size_of::<FreeNode>() <= MIN_OBJECT_SIZE);

#[derive(Debug)]
pub enum SlubError {
    TooLargeSize,
    TooMuchWaste,
    PageAllocationFailed,
}

impl Slub {
    // 通用初始化逻辑
    unsafe fn init_slab(
        zone_type: ZoneType,
        object_num: NonZeroU16,
        object_size: NonZeroU16,
        order: PageOrder,
        slab_addr: Option<NonNull<Slub>>,
    ) -> Result<NonNull<Slub>, SlubError> {
        // 分配页
        let pages = page_manager()
            .unwrap()
            .allocate_pages(zone_type, order)
            .ok_or(SlubError::PageAllocationFailed)?;

        let page_addr = pages
            .read()
            .addr
            .map_addr(|paddr| VIR_BASE_ADDR + paddr)
            .cast::<Slub>();

        let (slub, free_nodes) = if let Some(addr) = slab_addr {
            // 使用传入的 Slub 地址，free node 在页地址的起始处
            (addr, NonNull::new_unchecked(page_addr.cast::<FreeNode>()))
        } else {
            // 获取页地址作为 Slub 起始地址, free node 紧随其后
            (
                NonNull::new_unchecked(page_addr),
                NonNull::new_unchecked(page_addr.add(1).cast::<FreeNode>()),
            )
        };

        // 填写 Page 中的 Slub 字段
        for i in 0..order.to_count() {
            let page = pages.add(i).as_mut();
            page.slub = slub.as_ptr();
        }

        FreeNode::init(free_nodes, object_num, object_size);

        let inner = Spinlock::new(SlubInner::new(free_nodes.as_ptr()));

        slub.write(Slub {
            list: ListNode::new(),
            inner,
            object_num,
            pages,
        });

        Ok(slub)
    }

    pub fn new_embedded(
        zone_type: ZoneType,
        object_size: NonZeroU16,
        object_num: NonZeroU16,
        order: PageOrder,
    ) -> Result<NonNull<Self>, SlubError> {
        unsafe { Self::init_slab(zone_type, object_num, object_size, order, None) }
    }

    pub fn allocate<T>(&mut self) -> Option<NonNull<T>> {
        let mut inner = self.inner.lock();

        if inner.freelist.is_null() {
            return None;
        }

        unsafe {
            let node = inner.freelist;
            inner.freelist = (*node).next;
            inner.inuse += 1;

            Some(NonNull::new_unchecked(node.cast::<T>()))
        }
    }

    pub fn free<T>(&mut self, obj: *mut T) {
        let mut inner = self.inner.lock();

        unsafe {
            let node = obj.cast::<FreeNode>();
            (*node).next = inner.freelist;
            inner.freelist = node;
            inner.inuse -= 1;
        }
    }
}

impl SlubInner {
    pub const fn new(freelist: *mut FreeNode) -> Self {
        SlubInner { freelist, inuse: 0 }
    }
}

impl Spinlock<SlubInner> {
    pub fn get_inuse_relaxed(&self) -> u16 {
        let inuse = self.get_relaxed().inuse;
        inuse
    }
}

impl FreeNode {
    pub fn init(ptr: NonNull<Self>, object_num: NonZeroU16, object_size: NonZeroU16) {
        let mut current = ptr;

        unsafe {
            for _ in 0..(object_num.get() - 1) {
                let next = current.byte_add(object_size.get() as usize);
                current.write(FreeNode {
                    next: next.as_ptr(),
                });
                current = next;
            }

            current.write(FreeNode {
                next: core::ptr::null_mut(),
            });
        }
    }
}

const fn reserved_space(embed: bool) -> usize {
    if embed {
        core::mem::size_of::<Slub>()
    } else {
        0
    }
}

fn calculate_order(size: NonZeroU16, embed: bool) -> Result<PageOrder, SlubError> {
    let mut size = size.get().next_multiple_of(size_of::<usize>() as u16);
    size = size.min(MAX_OBJECT_SIZE as u16).max(MIN_OBJECT_SIZE as u16);

    let mut order = 0;
    let reserved_space = reserved_space(embed);

    for fraction in [16, 8, 4, 2] {
        for _order in 0..=MAX_PAGE_ORDER.val() {
            let slab_size = (PAGE_SIZE << _order) - reserved_space;

            let remain = slab_size % (size as usize);
            if remain < (slab_size / fraction) {
                order = _order;
                break;
            }
        }

        if order < MAX_PAGE_ORDER.val() {
            return Ok(PageOrder::new(order as u8));
        }
    }

    if size <= MAX_OBJECT_SIZE as u16 {
        Err(SlubError::TooMuchWaste)
    } else {
        Err(SlubError::TooLargeSize)
    }
}

fn calculate_sizes(size: NonZeroU16, embed: bool) -> (NonZeroU16, NonZeroU16, PageOrder) {
    let order = calculate_order(size, embed).unwrap();

    let reserved_space = reserved_space(embed);
    let slab_size = (PAGE_SIZE << order.val()) - reserved_space;

    let object_size = size.get().next_multiple_of(size_of::<usize>() as u16) as usize;
    let object_num = (slab_size / object_size) as u16;

    (
        NonZeroU16::new(object_size as u16).unwrap(),
        NonZeroU16::new(object_num).unwrap(),
        order,
    )
}

#[no_mangle]
pub unsafe extern "C" fn mem_caches_init() {
    MemCaches::init();
}
