// Basic SLUB-like structures for the kernel (简单骨架)
// - 忽略 NUMA
// - 使用从 C 导入的自旋锁（见 src/include/kernel/spinlock.h）
// 这是最小实现：结构体和基本方法（init/alloc/free stub），便于后续扩展。

use core::{
    num::{NonZeroU16, NonZeroUsize},
    ptr::NonNull,
};

use crate::{
    kernel::memory::{
        block::page_manager,
        buddy::{BuddyPage, PageOrder},
        page::{PageAllocator, ZoneType, PAGE_SIZE},
        slub::mem_cache::{MemCache, MemCaches},
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
    pub slub_type: SlubType,
    /// object总数量
    object_num: NonZeroU16,
    /// 分配的页结构体指针
    pages: NonNull<Page>,
    /// 所属内存区域类型
    zone_type: ZoneType,
}

pub struct SlubHead {
    pub list: ListNode<Slub>,
    lock: Spinlock<SlubInner>,
}

pub enum SlubType {
    Head(SlubHead),
    Body(NonNull<Slub>),
}

/// Slub 内部数据结构
pub struct SlubInner {
    /// 空闲对象头
    freelist: Option<NonNull<FreeNode>>,
    /// 正在使用的对象数量
    inuse: u16,
}

pub struct FreeNode {
    pub next: Option<NonNull<FreeNode>>,
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
    ) -> Result<NonNull<Slub>, SlubError> {
        // 分配页
        let pages = page_manager()
            .unwrap()
            .allocate_pages(zone_type, order)
            .ok_or(SlubError::PageAllocationFailed)?;

        let start_addr = pages.as_ref().start_addr();
        let free_nodes = {
            pages
                .with_addr(NonZeroUsize::new(start_addr).unwrap())
                .cast::<FreeNode>()
        };

        let mut first_page = pages;

        first_page.write(Page::Slub(Slub {
            slub_type: SlubType::Head(SlubHead::new(Some(free_nodes))),
            object_num,
            pages,
            zone_type,
        }));

        let mut slub = match first_page.as_mut() {
            Page::Slub(slub) => NonNull::new_unchecked(slub),
            _ => unreachable!(),
        };

        // 填写 Page 中的 Slub
        for i in 0..order.to_count() {
            let page = pages.add(i);

            let slub_info = Slub {
                slub_type: SlubType::Body(slub),
                object_num,
                pages,
                zone_type,
            };
            page.write(Page::Slub(slub_info));
        }

        FreeNode::init(free_nodes, object_num, object_size);

        let slub_ref = unsafe { slub.as_mut() };
        match &mut slub_ref.slub_type {
            SlubType::Head(head) => head.list.init(),
            _ => unreachable!(),
        }

        Ok(slub)
    }

    pub fn new(
        zone_type: ZoneType,
        object_size: NonZeroU16,
        object_num: NonZeroU16,
        order: PageOrder,
    ) -> Result<NonNull<Self>, SlubError> {
        unsafe { Self::init_slab(zone_type, object_num, object_size, order) }
    }

    pub unsafe fn destroy(mut ptr: NonNull<Self>, cache_info: &MemCache) {
        let order = cache_info.order;

        let slub = ptr.as_mut();
        let page = slub.pages;

        page.write(Page::Buddy(BuddyPage {
            list: ListNode::new(),
            page,
            order,
            zone_type: slub.zone_type,
        }));

        page_manager().unwrap().free_pages(page).unwrap();
    }
}

impl SlubHead {
    const fn new(freelist: Option<NonNull<FreeNode>>) -> Self {
        Self {
            list: ListNode::new(),
            lock: Spinlock::new(SlubInner { freelist, inuse: 0 }),
        }
    }

    pub fn get_inuse_relaxed(&self) -> u16 {
        let inuse = self.lock.get_relaxed().inuse;
        inuse
    }

    pub fn allocate<T>(&mut self) -> Option<NonNull<T>> {
        let mut inner = self.lock.lock();

        let freelist = inner.freelist?;

        let node = freelist;
        inner.freelist = unsafe { node.read() }.next;
        inner.inuse += 1;

        Some(node.cast())
    }

    pub fn free<T>(&mut self, obj: NonNull<T>) {
        let mut inner = self.lock.lock();

        let node = obj.cast::<FreeNode>();

        unsafe {
            node.write(FreeNode {
                next: inner.freelist,
            });
        }

        inner.freelist = Some(node);
        inner.inuse -= 1;
    }
}

impl FreeNode {
    pub fn init(ptr: NonNull<Self>, object_num: NonZeroU16, object_size: NonZeroU16) {
        let mut current = ptr;

        unsafe {
            for _ in 0..(object_num.get() - 1) {
                let next = current.byte_add(object_size.get() as usize);
                current.write(FreeNode { next: Some(next) });
                current = next;
            }

            current.write(FreeNode { next: None });
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
