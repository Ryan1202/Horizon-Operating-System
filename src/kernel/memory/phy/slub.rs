// Basic SLUB-like structures for the kernel (简单骨架)
// - 忽略 NUMA
// - 使用从 C 导入的自旋锁（见 src/include/kernel/spinlock.h）
// 这是最小实现：结构体和基本方法（init/alloc/free stub），便于后续扩展。

use core::{
    num::{NonZeroU16, NonZeroUsize},
    ptr::NonNull,
};

use crate::{
    container_of_enum,
    kernel::memory::{
        VIR_BASE_ADDR,
        phy::{
            page::{
                PAGE_SIZE, Page, PageAllocator, PageError, ZoneType,
                buddy::{BuddyPage, PageOrder},
                page_manager,
            },
            slub::mem_cache::{MemCache, MemCaches},
        },
    },
    lib::rust::{list::ListNode, spinlock::Spinlock},
};

pub(super) mod config;
mod mem_cache;
mod mem_cache_node;

pub(self) const MIN_OBJECT_SIZE: usize = 8;
pub(self) const MAX_OBJECT_SIZE: usize = 4096;
pub(self) const MAX_PAGE_ORDER: PageOrder = PageOrder::new(3); // 最大 8 页
pub(self) const MIN_PARTIAL: u8 = 5;
pub(self) const MAX_PARTIAL: u8 = 10;

pub(self) const ALIGN: usize = core::mem::size_of::<usize>();

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct ObjectSize(pub NonZeroU16);

#[repr(C)]
pub struct Slub {
    pub slub_type: SlubType,
    /// object总数量
    object_size: NonZeroU16,
    /// 所属内存区域类型
    zone_type: ZoneType,
}

#[repr(C)]
pub struct SlubHead {
    pub list: ListNode<Slub>,
    lock: Spinlock<SlubInner>,
}

#[repr(C)]
pub enum SlubType {
    Head(SlubHead),
    Body(NonNull<Slub>),
}

/// Slub 内部数据结构
#[repr(C)]
pub struct SlubInner {
    /// 空闲对象头
    freelist: Option<NonNull<FreeNode>>,
    /// 正在使用的对象数量
    inuse: u16,
}

#[repr(C)]
pub struct FreeNode {
    pub next: Option<NonNull<FreeNode>>,
}
const _: () = assert!(size_of::<FreeNode>() <= MIN_OBJECT_SIZE);

#[derive(Debug)]
pub enum SlubError {
    /// 请求的对象大小超过允许的最大值`MAX_OBJECT_SIZE`
    ///
    /// `TooLargeSize(size: usize)`
    TooLargeSize(usize),

    /// 请求的对象大小导致内存浪费过多(超过1/2组页)
    ///
    /// `TooMuchWaste(waste: usize, size: usize)`
    TooMuchWaste(usize, usize),

    /// 分配页失败（可能是当前Zone内存不足）
    ///
    /// `PageAllocationFailed(zone_type: ZoneType, requsted_order: usize)`
    PageAllocationFailed(ZoneType, PageOrder),

    /// `Slub`调用`Buddy`释放页时，传入了非`Page::Buddy`类型的页
    ///
    /// `IncorrectAddressToFree(addr: NonNull<Page>)`
    IncorrectAddressToFree(NonNull<Page>),
}

impl Slub {
    // 通用初始化逻辑
    fn init_slab(
        zone_type: ZoneType,
        object_num: NonZeroU16,
        object_size: NonZeroU16,
        order: PageOrder,
    ) -> Result<NonNull<Slub>, SlubError> {
        // 分配页
        let pages = page_manager()
            .lock()
            .allocate_pages(zone_type, order)
            .ok_or(SlubError::PageAllocationFailed(zone_type, order))?;

        let start_addr = unsafe { pages.as_ref().start_addr() + VIR_BASE_ADDR };
        let free_nodes = {
            pages
                .with_addr(
                    NonZeroUsize::new(start_addr)
                        .expect("start_addr allocated from page should be non zero!"),
                )
                .cast::<FreeNode>()
        };

        let mut first_page = pages;

        unsafe {
            first_page.write(Page::Slub(Slub {
                slub_type: SlubType::Head(SlubHead::new(Some(free_nodes))),
                object_size,
                zone_type,
            }))
        };

        let mut slub = match unsafe { first_page.as_mut() } {
            Page::Slub(slub) => NonNull::from_mut(slub),
            _ => unreachable!(),
        };

        // 填写 Page 中的 Slub
        for i in 1..order.to_count() {
            let page = unsafe { pages.add(i) };

            let slub_info = Slub {
                slub_type: SlubType::Body(slub),
                object_size,
                zone_type,
            };
            unsafe { page.write(Page::Slub(slub_info)) };
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
        object_size: ObjectSize,
        object_num: NonZeroU16,
        order: PageOrder,
    ) -> Result<NonNull<Self>, SlubError> {
        Self::init_slab(zone_type, object_num, object_size.0, order)
    }

    pub fn destroy(mut ptr: NonNull<Self>, cache_info: &MemCache) -> Result<(), SlubError> {
        let order = cache_info.order;

        let slub = unsafe { ptr.as_mut() };
        let page = container_of_enum!(NonNull::from_mut(slub), Page, Slub.0);

        unsafe {
            page.write(Page::Buddy(BuddyPage {
                list: ListNode::new(),
                order,
                zone_type: slub.zone_type,
            }))
        };

        page_manager().lock().free_pages(page).map_err(|e| match e {
            PageError::IncorrectPageType => SlubError::IncorrectAddressToFree(page),
        })
    }

    #[inline]
    pub fn free(&mut self, obj: NonNull<u8>) {
        match &mut self.slub_type {
            SlubType::Head(head) => head.free(obj),
            SlubType::Body(body_slub) => unsafe { body_slub.as_mut() }.free(obj),
        }
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

    /// 返回一个类型为`T`的对象的指针，内存大小在创建`Slub`时已确定
    ///
    /// 如果该`Slub`没有剩余可用对象，则返回`None`
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

fn calculate_order(size: ObjectSize) -> Result<PageOrder, SlubError> {
    let mut size = size.0.get().next_multiple_of(size_of::<usize>() as u16);
    size = size.min(MAX_OBJECT_SIZE as u16).max(MIN_OBJECT_SIZE as u16);

    let mut order = 0;

    for fraction in [16, 8, 4, 2] {
        for _order in 0..MAX_PAGE_ORDER.val() {
            let slab_size = PAGE_SIZE << _order;

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
        Err(SlubError::TooMuchWaste(
            (PAGE_SIZE << order) % (size as usize),
            size as usize,
        ))
    } else {
        Err(SlubError::TooLargeSize(size as usize))
    }
}

fn calculate_sizes(size: NonZeroU16) -> Result<(ObjectSize, NonZeroU16, PageOrder), SlubError> {
    let object_size = NonZeroU16::new(size.get().next_multiple_of(size_of::<usize>() as u16))
        .expect("`object_size` should be non zero after alignment!");
    let object_size = ObjectSize(object_size);
    let order = calculate_order(object_size)?;

    let slab_size = PAGE_SIZE << order.val();

    let object_num = (slab_size / object_size.0.get() as usize) as u16;

    Ok((
        object_size,
        NonZeroU16::new(object_num).expect("`object_num` should be non zero after calculation!"),
        order,
    ))
}

#[unsafe(no_mangle)]
pub extern "C" fn mem_caches_init() {
    MemCaches::init();
}
