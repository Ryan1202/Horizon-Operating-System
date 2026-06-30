use core::{num::NonZeroUsize, ptr::NonNull, slice};

use crate::{
    arch::{PhysAddr, VirtAddr},
    kernel::memory::{
        KLINEAR_BASE, KLINEAR_END,
        dma::{
            DmaAddr,
            source::{BouncePool, software_iotlb::BounceAddr},
        },
        frame::Frame,
        kmalloc::{kfree, kmalloc},
    },
};

#[repr(C)]
pub struct EntryList {
    entry_type: EntryTypeRaw,
    offset: usize,
    size: usize,
    dma_addr: DmaAddr,
}

#[repr(transparent)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct EntryTypeRaw(usize);

pub enum EntryType {
    Array(Option<NonNull<Frame>>),
    Chain(NonNull<EntryList>),
}

mod tags {
    /// 表示当前 Entry 是一个指向下一个 Array 的链表
    pub const CHAIN: usize = 0b01;
    /// 表示整个链表的结束
    pub const END: usize = 0b10;
    pub const MASK: usize = 0b11;
}

impl EntryTypeRaw {
    const EMPTY: Self = Self(0);

    const fn new(ptr: usize, flags: usize) -> Self {
        Self((ptr & !tags::MASK) | (flags & tags::MASK))
    }

    /// 获取 EntryType
    pub const fn get(&self) -> (EntryType, bool) {
        let ptr = (self.0 & !tags::MASK) as *mut ();

        let entry_type = if self.is_chain() {
            EntryType::Chain(
                NonNull::new(ptr)
                    .expect("A chain with a null pointer in EntryList")
                    .cast(),
            )
        } else {
            EntryType::Array(NonNull::new(ptr as *mut Frame))
        };
        (entry_type, self.is_end())
    }

    pub const fn is_chain(&self) -> bool {
        (self.0 & tags::CHAIN) != 0
    }

    pub const fn is_end(&self) -> bool {
        (self.0 & tags::END) != 0
    }

    const fn is_null(&self) -> bool {
        (self.0 & !tags::MASK) == 0
    }
}

impl EntryType {
    pub fn to_raw(&self, is_end: bool) -> EntryTypeRaw {
        let flags = if is_end { tags::END } else { 0 };
        match self {
            Self::Array(Some(ptr)) => EntryTypeRaw::new(ptr.addr().get(), flags),
            Self::Array(None) => EntryTypeRaw::new(0, flags),
            Self::Chain(ptr) => EntryTypeRaw::new(ptr.addr().get(), flags | tags::CHAIN),
        }
    }
}

impl EntryList {
    pub const fn empty() -> Self {
        Self {
            entry_type: EntryTypeRaw::EMPTY,
            offset: 0,
            size: 0,
            dma_addr: DmaAddr::new(0),
        }
    }

    pub const fn clear(&mut self) {
        self.entry_type = EntryTypeRaw::EMPTY;
        self.offset = 0;
        self.size = 0;
        self.dma_addr = DmaAddr::new(0);
    }

    /// 设置 END 标记，仅供内部使用
    fn mark_end(&mut self) {
        let raw = self.entry_type.0;
        self.entry_type = EntryTypeRaw::new(raw, (raw & tags::MASK) | tags::END);
    }

    /// 初始化 EntryList 数组，清空所有 entry，并将最后一个 entry 标记为 END
    ///
    /// # Safety
    ///
    /// `n_entries` 不得大于实际分配的 EntryList 数组长度，否则会导致越界访问
    pub unsafe fn init_array(&mut self, n_entires: usize) {
        let entries = unsafe { slice::from_raw_parts_mut(self as *mut EntryList, n_entires) };

        for entry in entries.iter_mut() {
            entry.clear();
        }
        entries.last_mut().map(|entry| entry.mark_end());
    }

    /// 强行设置当前 entry 为 CHAIN，指向下一个 segment
    ///
    /// # Safety
    ///
    /// `next` 必须是通过 `EntryList::create_segment` 分配的，并且没有被释放过
    ///
    /// 需要确保当前 entry 不含有效数据，否则会导致内存泄漏
    unsafe fn force_set_chain(&mut self, next: NonNull<EntryList>) {
        self.entry_type = EntryType::Chain(next).to_raw(false);
    }

    /// 分配 `capacity` 个连续 EntryList。
    ///
    /// 前 capacity-1 个初始化为零（frame=0，无标志），最后一个设 END。
    pub fn create_segment(capacity: usize) -> Option<NonNull<EntryList>> {
        let size = NonZeroUsize::new(capacity.checked_mul(size_of::<EntryList>())?)?;
        let head = kmalloc::<EntryList>(size)?;

        let entries = unsafe { core::slice::from_raw_parts_mut(head.as_ptr(), capacity) };
        for entry in entries.iter_mut() {
            entry.clear();
        }
        // 最后一个 slot 标记数组结束
        if capacity > 0 {
            entries[capacity - 1].mark_end();
        }

        Some(head)
    }

    pub fn is_empty(&self) -> bool {
        self.entry_type.is_null()
    }

    /// 设置页帧（Array 类型），保留 END 标志。
    ///
    /// 仅在当前 EntryList 为 Array(None) 时成功
    pub fn set_frame(&mut self, frame: NonNull<Frame>, offset: usize, size: usize) -> Option<()> {
        if let (EntryType::Array(None), is_end) = self.get_type() {
            self.entry_type = EntryType::Array(Some(frame)).to_raw(is_end);
            self.offset = offset;
            self.size = size;
            Some(())
        } else {
            None
        }
    }

    /// 强行设置页帧（Array 类型），保留 END 标志。
    ///
    /// 不检查当前 EntryList 的类型，直接覆盖。
    pub fn force_set_frame(&mut self, frame: NonNull<Frame>, offset: usize, size: usize) {
        let (_, is_end) = self.get_type();
        self.entry_type = EntryType::Array(Some(frame)).to_raw(is_end);
        self.offset = offset;
        self.size = size;
    }

    /// 从线性映射地址设置缓冲区，自动拆分为 Frame + offset。
    ///
    /// 仅在当前 EntryList 为 Array(None) 时成功
    pub fn set_buffer(&mut self, buffer: *const u8, size: usize) -> Option<()> {
        let vaddr = VirtAddr::new(buffer.addr());
        let page_vaddr = vaddr.page_align_down();
        let offset = vaddr.page_offset();

        debug_assert!(
            page_vaddr >= KLINEAR_BASE && vaddr + size < KLINEAR_END,
            "set_buffer only accepts linear-mapped addresses"
        );

        let paddr = PhysAddr::new(page_vaddr - KLINEAR_BASE);
        let frame = Frame::get_raw(paddr.to_frame_number());

        self.set_frame(frame, offset, size)
    }

    /// 当前 entry 设 `CHAIN`，分配新 segment，返回新 segment head
    ///
    /// 仅在当前 entry 为 `END` 时成功
    pub fn extend(&mut self, new_capacity: usize) -> Option<NonNull<EntryList>> {
        match self.entry_type.get() {
            (EntryType::Array(Some(frame)), true) => {
                let mut new_head = Self::create_segment(new_capacity)?;

                unsafe {
                    *new_head.as_mut() = EntryList {
                        entry_type: EntryType::Array(Some(frame)).to_raw(false),
                        offset: self.offset,
                        size: self.size,
                        dma_addr: self.dma_addr,
                    };
                    self.force_set_chain(new_head);
                }
                Some(new_head)
            }
            (EntryType::Array(None), true) => {
                let new_head = Self::create_segment(new_capacity)?;
                unsafe { self.force_set_chain(new_head) };
                Some(new_head)
            }
            _ => {
                // 当前 entry 不是 `Array(None)` 或者不是 `END`，无法扩展
                None
            }
        }
    }

    pub const fn get_type(&self) -> (EntryType, bool) {
        self.entry_type.get()
    }

    pub const fn is_chain(&self) -> bool {
        self.entry_type.is_chain()
    }

    pub const fn is_end(&self) -> bool {
        self.entry_type.is_end()
    }

    pub const fn dma_addr(&self) -> DmaAddr {
        self.dma_addr
    }

    pub const fn set_dma_addr(&mut self, dma_addr: DmaAddr) {
        self.dma_addr = dma_addr;
    }

    pub const fn size(&self) -> usize {
        self.size
    }

    pub const fn offset(&self) -> usize {
        self.offset
    }

    pub fn virt_addr(&self) -> Option<VirtAddr> {
        self.phys_addr()?
            .try_to_virt()
            .map(|vaddr| vaddr + self.offset)
    }

    pub fn phys_addr(&self) -> Option<PhysAddr> {
        match self.get_type().0 {
            EntryType::Array(Some(frame)) => unsafe {
                Some(frame.as_ref().start_addr() + self.offset)
            },
            _ => None,
        }
    }

    pub fn iter(&self) -> EntryIter<'_> {
        EntryIter { next: Some(self) }
    }

    pub fn iter_mut(&mut self) -> EntryIterMut<'_> {
        EntryIterMut { next: Some(self) }
    }

    /// 尝试释放 bounce buffer，如果当前 entry 不是 bounce buffer，则不做任何操作
    fn try_free_bounce(frame: NonNull<Frame>, offset: usize) {
        let pool = BouncePool::get_pool();
        let paddr = unsafe { frame.as_ref() }.start_addr() + offset;
        if let Some(bounce_addr) = BounceAddr::new(paddr) {
            pool.deallocate(bounce_addr);
        }
    }

    /// 释放当前 array 的内存，不负责释放 bounce buffer
    ///
    /// `head`: array 的首地址
    ///
    /// # Safety
    ///
    /// 必须已经释放了所有的 bounce buffer，否则会导致内存泄漏
    unsafe fn destroy_array(head: NonNull<EntryList>) {
        if let Err(e) = kfree(head) {
            printk!(
                "WARN: Failed to free EntryList segment at {:p}: {:?}",
                head.as_ptr(),
                e
            );
        }
    }

    /// 沿 CHAIN 遍历释放所有 segment。
    ///
    /// # Safety
    ///
    /// `head` 必须是通过 `EntryList::create_segment` 分配的，并且没有被释放过
    pub unsafe fn destroy_all(head: NonNull<EntryList>) {
        let mut current = head;
        let mut head = head;
        loop {
            let entry = unsafe { current.as_ref() };

            current = match entry.get_type() {
                (entry_type, true) => {
                    if let EntryType::Array(Some(frame)) = entry_type {
                        Self::try_free_bounce(frame, entry.offset);
                    }
                    unsafe { Self::destroy_array(head) };
                    break;
                }
                (EntryType::Chain(next_head), false) => {
                    unsafe { Self::destroy_array(head) };
                    head = next_head;
                    head
                }
                (EntryType::Array(Some(frame)), false) => {
                    Self::try_free_bounce(frame, entry.offset);
                    unsafe { current.add(1) }
                }
                (EntryType::Array(None), false) => unsafe { current.add(1) },
            };
        }
    }
}

pub struct EntryIter<'a> {
    next: Option<&'a EntryList>,
}

impl<'a> Iterator for EntryIter<'a> {
    type Item = &'a EntryList;

    fn next(&mut self) -> Option<Self::Item> {
        let entry = self.next.take()?;

        self.next = match entry.get_type() {
            (EntryType::Array(_), true) => None,
            (EntryType::Array(_), false) => unsafe { (entry as *const EntryList).add(1).as_ref() },
            (EntryType::Chain(next_head), false) => Some(unsafe { next_head.as_ref() }),
            (EntryType::Chain(_), true) => {
                unreachable!("A chain entry cannot be the end of the list")
            }
        };
        Some(entry)
    }
}

pub struct EntryIterMut<'a> {
    next: Option<&'a mut EntryList>,
}

impl<'a> Iterator for EntryIterMut<'a> {
    type Item = &'a mut EntryList;

    fn next(&mut self) -> Option<Self::Item> {
        let entry = self.next.take()?;

        self.next = match entry.get_type() {
            (EntryType::Array(_), true) => None,
            (EntryType::Array(_), false) => unsafe { (entry as *mut EntryList).add(1).as_mut() },
            (EntryType::Chain(mut next_head), false) => Some(unsafe { next_head.as_mut() }),
            (EntryType::Chain(_), true) => {
                unreachable!("A chain entry cannot be the end of the list")
            }
        };
        Some(entry)
    }
}
