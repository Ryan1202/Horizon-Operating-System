use core::{cell::SyncUnsafeCell, mem::MaybeUninit, num::NonZeroU16, ops::Range, ptr::NonNull};

use ::alloc::boxed::Box;

use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    impl_addr,
    kernel::memory::{
        MemoryError,
        dma::Device,
        frame::{
            buddy::{FrameOrder, MAX_ORDER},
            options::FrameAllocOptions,
            zone::ZoneType,
        },
        kmalloc::{Atomic, Kmalloc},
        page::{PageTableOps, current_root_pt, options::PageAllocOptions},
    },
    lib::rust::spinlock::Spinlock,
};

const SLOT_SIZE: u16 = 2048;
const MAX_ALLOC_SLOTS: u16 = 128;
const TOTAL_SIZE: usize = 2 * 1024 * 1024; // 2 MiB
const TOTAL_SLOTS: usize = TOTAL_SIZE / SLOT_SIZE as usize;

const _: () = assert!(
    TOTAL_SIZE <= MAX_ORDER.to_size(),
    "SWIOTLB: TOTAL_SIZE exceeds maximum frame order size"
);

const TOTAL_AREAS: usize = 4;
const SLOT_PER_AREA: usize = TOTAL_SLOTS / TOTAL_AREAS;

pub struct BouncePool {
    vaddr_base: VirtAddr,
    paddr_base: BounceAddr,
    total_slots: u16,
    areas: Box<[IotlbArea; TOTAL_AREAS], Kmalloc<Atomic>>,
}

pub struct IotlbArea {
    lock: Spinlock<AreaInner>,
}

pub struct AreaInner {
    /// 已分配的 Slot 数量
    used: u16,
    /// 缓存上次分配的槽位索引，优化下次分配的搜索
    index: u16,
    /// 当前 Area 的首个 Slot 在整个池中的偏移量
    slot_offset: u32,
    /// 当前 Area 的槽位元数据
    slots: &'static mut [SlotMeta],
}

#[derive(Clone)]
pub struct SlotMeta {
    /// 当前 Slot 映射的原始物理地址
    origin_addr: PhysAddr,
    /// 自当前 Slot 起始的剩余大小，单位为字节
    origin_size: u32,
    /// 当前 Slot 及其后续连续空闲 Slot 的数量，None 表示已分配
    n_slots: Option<NonZeroU16>,
    /// Slot 内部用于对齐的填充槽位数量，位于前部
    padding_slots: u16,
    /// 当前 Slot 是否为分配区域的头部
    allocation_head: bool,
}

/// 该结构体用于表示 Bounce Pool 中的物理地址
#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct BounceAddr(usize);

static BOUNCE_POOL: SyncUnsafeCell<MaybeUninit<BouncePool>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());

impl_addr!(BounceAddr);

impl BounceAddr {
    /// 创建一个新的 BounceAddr，确保地址在 Bounce Pool 的范围内
    ///
    /// # Safety
    ///
    /// 调用者必须确保 BouncePool 已初始化，否则行为未定义。
    pub fn new(addr: PhysAddr) -> Option<Self> {
        BouncePool::get_pool()
            .contains(addr)
            .then(|| BounceAddr(addr.as_usize()))
    }

    /// 转换成物理地址
    pub const fn as_paddr(&self) -> PhysAddr {
        PhysAddr::new(self.0)
    }

    /// 对齐到当前 BounceAddr 所在的槽位的起始地址
    pub const fn align_to_slot(&self) -> BounceAddr {
        let offset = self.0 % SLOT_SIZE as usize;
        BounceAddr(self.0 - offset)
    }
}

impl const Default for SlotMeta {
    fn default() -> Self {
        SlotMeta {
            origin_addr: PhysAddr::new(0),
            origin_size: 0,
            n_slots: Some(NonZeroU16::new(1).unwrap()),
            padding_slots: 0,
            allocation_head: false,
        }
    }
}

impl SlotMeta {
    pub const fn is_free(&self) -> bool {
        self.n_slots.is_some()
    }

    pub const fn origin_addr(&self) -> PhysAddr {
        self.origin_addr
    }

    pub const fn origin_size(&self) -> usize {
        self.origin_size as usize
    }

    /// 将头部 Slot 标记为已分配，并设置相关的元数据
    ///
    /// `origin_addr`: 分配的原始物理地址
    ///
    /// `origin_size`: 分配的原始物理地址大小
    ///
    /// `padding_slots`: 为了满足对齐要求而保留的 Slot
    fn mark_allocated_head(&mut self, origin_addr: PhysAddr, origin_size: u32, padding_slots: u16) {
        *self = Self {
            origin_addr,
            origin_size,
            n_slots: None,
            padding_slots,
            allocation_head: true,
        }
    }

    /// 将非头部 Slot 标记为已分配，并设置相关的元数据
    ///
    /// `origin_addr`: 分配的原始物理地址
    ///
    /// `origin_size`: 分配的原始物理地址大小
    fn mark_allocated(&mut self, origin_addr: PhysAddr, origin_size: u32) {
        *self = Self {
            origin_addr,
            origin_size,
            n_slots: None,
            padding_slots: 0,
            allocation_head: false,
        }
    }
}

impl AreaInner {
    const ONE: NonZeroU16 = NonZeroU16::new(1).unwrap();

    /// 尝试在当前 Area 中分配连续的槽位，实际的分配大小由 `n_slots` 决定，`alloc_size` 仅用于记录原始物理地址的大小
    ///
    /// `n_slots`: 需要分配的槽位数量
    ///
    /// `origin_size`: 需要分配的总大小，单位为字节
    ///
    /// `origin_addr`: 需要映射的原始物理地址
    ///
    /// `align_slots`: 对齐要求，单位为槽位数
    ///
    /// 返回值: 如果分配成功，返回 `Some(local_index)`，表示分配的槽位在当前 Area 中的本地索引；如果分配失败，返回 `None`
    fn try_alloc(
        &mut self,
        n_slots: u16,
        origin_size: u32,
        origin_addr: PhysAddr,
        align_slots: usize,
    ) -> Option<u32> {
        let (padding, local_index) = self.find_consecutive_free(n_slots, align_slots)?;

        self.mark_allocated(local_index, n_slots, origin_size, origin_addr, padding);
        Some(local_index)
    }

    /// 在当前 Area 中查找连续的空闲槽位，满足对齐要求
    ///
    /// `n_slots`: 需要查找的连续空闲槽位数量
    ///
    /// `align_slots`: 对齐要求，单位为槽位数
    ///
    /// 返回值: 如果找到满足条件的连续空闲槽位，返回 `Some((padding, local_index))`，
    /// `padding` 表示为了满足对齐要求需要跳过的槽位数量，
    /// `local_index` 表示找到的连续空闲槽位的起始索引；
    /// 如果未找到，返回 `None`
    fn find_consecutive_free(&self, n_slots: u16, align_slots: usize) -> Option<(u16, u32)> {
        let len = self.slots.len();
        let start = self.index as usize;
        let mut i = start;
        loop {
            let skip = if let Some(available) = self.slots[i].n_slots {
                let available = available.get() as usize;
                let offset = i % align_slots;

                if available >= n_slots as usize + offset {
                    let padding = ((align_slots - offset) % align_slots) as u16;
                    return Some((padding, i as u32));
                }

                available
            } else {
                1
            };

            if i < start && i + skip >= start {
                return None;
            }
            i = (i + skip) % len;
        }
    }

    /// 将一组 Slot 标记为已被分配
    ///
    /// `index`: 分配的起始 Slot 索引
    ///
    /// `n_slots`: 分配的 Slot 数量
    ///
    /// `origin_size`: 分配的原始物理地址大小
    ///
    /// `origin_addr`: 分配的原始物理地址
    ///
    /// `padding_slots`: 为了满足对齐要求而保留的 Slot
    fn mark_allocated(
        &mut self,
        index: u32,
        n_slots: u16,
        origin_size: u32,
        origin_addr: PhysAddr,
        padding_slots: u16,
    ) {
        let i = index as usize;
        let n = n_slots as usize;

        let mut origin_addr = origin_addr;
        let mut origin_size = origin_size;

        self.slots[i].mark_allocated_head(origin_addr, origin_size, padding_slots);

        for slot in &mut self.slots[i + 1..i + n] {
            origin_addr += SLOT_SIZE as usize;
            origin_size = origin_size
                .checked_sub(SLOT_SIZE as u32)
                .expect("Original size is much smaller than allocated size");

            slot.mark_allocated(origin_addr, origin_size);
        }

        for j in (0..i).rev() {
            if self.slots[j].n_slots.is_some() {
                break;
            }

            self.slots[j].n_slots = Some(NonZeroU16::new((i - j) as u16).unwrap());
        }

        self.used += n_slots as u16;
        self.index = if i + n < self.slots.len() {
            (i + n) as u16
        } else {
            0
        };
    }

    /// 释放一组连续的槽位，并尝试合并相邻的空闲槽位
    ///
    /// `local_index`: 需要释放的槽位在当前 Area 中的本地索引
    fn free_slots(&mut self, local_index: u16) {
        let i = local_index as usize;
        let slot = &mut self.slots[i];

        // 这里依赖 Slot 数量刚好是 padding + ceil(origin_size / SLOT_SIZE)
        let n_slots =
            slot.padding_slots as usize + (slot.origin_size as usize).div_ceil(SLOT_SIZE as usize);

        *slot = SlotMeta {
            origin_addr: PhysAddr::new(0),
            origin_size: 0,
            n_slots: Some(Self::ONE),
            padding_slots: 0,
            allocation_head: false,
        };

        // 后面会被合并，所以先不设置 n_slots

        let region = self.expand_free_region(i, n_slots);

        for k in region.clone().rev() {
            self.slots[k].n_slots = Some(NonZeroU16::new((region.end - k) as u16).unwrap());
        }

        self.used -= n_slots as u16;
        self.index = region.start as u16; // 更新索引为释放区域的起始位置，以便下次分配时从这里开始搜索
    }

    /// 扩展连续空闲槽位的范围，向前和向后查找连续的空闲槽位
    ///
    /// `index`: 当前槽位的索引
    ///
    /// `n_slots`: 当前槽位的数量
    ///
    /// 返回值: 返回一个 `Range<usize>`，表示连续空闲槽位的起始和结束索引范围
    fn expand_free_region(&self, index: usize, n_slots: usize) -> Range<usize> {
        let len = self.slots.len();

        let mut start = index;
        while start > 0 && self.slots[start - 1].is_free() {
            start -= 1;
        }

        // 向后只用找一次，因为每一次释放都会合并连续的空闲槽位，所以后续的槽位已经是连续空闲的
        let mut end = index + n_slots;
        if end < len && self.slots[end].is_free() {
            end += self.slots[end]
                .n_slots
                .map(|v| v.get() as usize)
                .unwrap_or(0);
        }

        start..end
    }
}

impl BouncePool {
    /// 获取全局的 BouncePool 实例
    ///
    /// # Safety
    ///
    /// 调用者必须确保 BouncePool 已初始化，否则行为未定义。
    pub fn get_pool<'a>() -> &'a BouncePool {
        unsafe { (*BOUNCE_POOL.get()).assume_init_ref() }
    }

    /// 分配一组连续的 Slot ，并返回对应的物理地址和虚拟地址
    ///
    /// `origin_size`: 原始大小，单位为字节
    ///
    /// `origin_addr`: 需要映射的原始物理地址
    ///
    /// `align`: 对齐要求，单位为字节
    ///
    /// 返回值: 如果分配成功，返回 `Some((BounceAddr, VirtAddr))`，表示分配的物理地址和虚拟地址；否则返回 `None`
    pub fn allocate(
        &self,
        origin_size: u32,
        origin_addr: PhysAddr,
        align: usize,
    ) -> Option<(BounceAddr, VirtAddr)> {
        let n_slots = origin_size.div_ceil(SLOT_SIZE as u32) as u16;
        if n_slots > MAX_ALLOC_SLOTS {
            return None;
        }

        // 计算最小公倍数
        fn gcd(mut a: usize, mut b: usize) -> usize {
            while b != 0 {
                let t = b;
                b = a % b;
                a = t;
            }
            a
        }
        fn lcm(a: usize, b: usize) -> usize {
            a * b / gcd(a, b)
        }
        let align_slots = lcm(align, SLOT_SIZE as usize) / SLOT_SIZE as usize;

        for area in self.areas.iter() {
            let mut inner = area.lock.lock();

            if let Some(local_index) =
                inner.try_alloc(n_slots, origin_size, origin_addr, align_slots)
            {
                let index = inner.slot_offset as usize + local_index as usize;
                let offset = index * SLOT_SIZE as usize;

                let paddr = self.paddr_base + offset;
                let vaddr = self.vaddr_base + offset;

                return Some((paddr, vaddr));
            }
        }
        None
    }

    /// 释放一组连续的 Slot
    ///
    /// `addr`: 需要释放的 Slot 的物理地址
    pub fn deallocate(&self, addr: BounceAddr) {
        let index = (addr - self.paddr_base) / SLOT_SIZE as usize;

        for area in self.areas.iter() {
            let mut area = area.lock.lock();
            let offset = area.slot_offset as usize;

            if index >= offset && index < offset + area.slots.len() {
                area.free_slots((index - offset) as u16);
                return;
            }
        }
    }

    /// 尝试获取 Slot 的指针，如果 `addr` 不在池的范围内，返回 `None`
    pub fn get_ptr<T>(&self, addr: BounceAddr) -> NonNull<T> {
        let offset = addr - self.paddr_base;
        let virt = self.vaddr_base + offset;
        NonNull::new(virt.as_mut_ptr::<T>()).unwrap()
    }

    /// 复制一份 Slot 元数据
    pub fn clone_slot(&self, addr: BounceAddr) -> SlotMeta {
        let index = (addr - self.paddr_base) / SLOT_SIZE as usize;

        for area in self.areas.iter() {
            let area = area.lock.lock();
            let offset = area.slot_offset as usize;

            if index >= offset && index < offset + area.slots.len() {
                return area.slots[index - offset].clone();
            }
        }
        panic!("BouncePool: Invalid Bounce address {:p}", addr.as_paddr());
    }

    /// 检查给定的物理地址是否在 Bounce Pool 的范围内
    pub fn contains(&self, paddr: PhysAddr) -> bool {
        let base = self.paddr_base.as_paddr();
        let size = self.total_slots as usize * SLOT_SIZE as usize;

        base <= paddr && paddr < base + size
    }
}

#[unsafe(export_name = "swiotlb_init")]
pub extern "C" fn init() {
    let order = FrameOrder::from_size(TOTAL_SIZE);

    let frame_options = FrameAllocOptions::new()
        .dynamic(order)
        .fallback(&[ZoneType::MEM32]);
    let page_options = PageAllocOptions::new(frame_options).contiguous(true);

    let pages = page_options
        .allocate()
        .expect("SWIOTLB: failed to allocate pool pages");

    let vaddr_base = pages.start_addr();
    let paddr_base = PageTableOps::<ArchPageTable>::translate(current_root_pt(), vaddr_base)
        .expect("SWIOTLB: failed to translate pool virt addr");
    let paddr_base = BounceAddr(paddr_base.as_usize());

    let mut slots = Box::new_in(
        [const { SlotMeta::default() }; TOTAL_SLOTS],
        Kmalloc::<Atomic>::default(),
    );

    for (i, slot) in slots.iter_mut().enumerate() {
        slot.n_slots = Some(NonZeroU16::new((TOTAL_SLOTS - i) as u16).unwrap());
    }

    let mut areas = Box::new_in(
        [const { MaybeUninit::uninit() }; TOTAL_AREAS],
        Kmalloc::<Atomic>::default(),
    );

    let slots = Box::leak(slots);

    for (i, area_slots) in slots.chunks_mut(SLOT_PER_AREA).enumerate() {
        let area_start = i * SLOT_PER_AREA;

        let inner = AreaInner {
            used: 0,
            index: 0,
            slot_offset: area_start as u32,
            slots: area_slots,
        };

        areas[i].write(IotlbArea {
            lock: Spinlock::new(inner),
        });
    }

    let (raw, allocator) = Box::into_raw_with_allocator(areas);
    let areas = unsafe { Box::from_raw_in(raw as *mut [IotlbArea; TOTAL_AREAS], allocator) };

    let pool = BouncePool {
        vaddr_base,
        paddr_base,
        total_slots: TOTAL_SLOTS as u16,
        areas,
    };
    unsafe { (*BOUNCE_POOL.get()).write(pool) };
}

pub fn acquire(
    paddr: PhysAddr,
    size: usize,
    device: &Device,
) -> Result<(PhysAddr, VirtAddr), MemoryError> {
    if size >= u16::MAX as usize {
        return Err(MemoryError::InvalidSize(size));
    }
    if device.constraints.mask == 0 {
        return Err(MemoryError::InvalidSize(0));
    }

    let align = 1 << device.constraints.mask.trailing_zeros() as usize;
    let (addr, vaddr) = BouncePool::get_pool()
        .allocate(size as u32, paddr, align)
        .ok_or(MemoryError::OutOfMemory)?;
    Ok((addr.as_paddr(), vaddr))
}
