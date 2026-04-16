use core::{
    cell::SyncUnsafeCell,
    mem::{self, ManuallyDrop, transmute},
    ptr::NonNull,
    sync::atomic::{AtomicU8, AtomicUsize, Ordering},
};

use crate::{
    CACHELINE_SIZE,
    arch::{ArchPageTable, PhysAddr},
    kernel::memory::{
        VMEMMAP_BASE, VMEMMAP_END,
        arch::ArchMemory,
        frame::{
            anonymous::Anonymous,
            assigned::AssignedFixed,
            buddy::{Buddy, BuddyAllocator, FrameOrder},
            reference::{FrameRc, SharedFrames, UniqueFrames},
            zone::ZoneType,
        },
        slub::Slub,
    },
    lib::rust::spinlock::RwSpinlock,
};

pub mod anonymous;
pub mod assigned;
pub mod buddy;
pub mod early;
pub mod number;
pub mod options;
pub mod reference;
pub mod zone;

pub use number::FrameNumber;

#[unsafe(export_name = "total_pages")]
pub static TOTAL_PAGES: AtomicUsize = AtomicUsize::new(0);
#[unsafe(export_name = "allocated_pages")]
pub static ALLOCATED_PAGES: AtomicUsize = AtomicUsize::new(0);

#[unsafe(no_mangle)]
pub static PREALLOCATED_END_PHY: SyncUnsafeCell<PhysAddr> = SyncUnsafeCell::new(PhysAddr::new(0));
#[unsafe(no_mangle)]
pub static PREALLOCATED_START_PHY: SyncUnsafeCell<PhysAddr> = SyncUnsafeCell::new(PhysAddr::new(0));
#[unsafe(no_mangle)]
pub static VMEMMAP_MAPPED_PAGES: AtomicUsize = AtomicUsize::new(0);

#[cfg(target_pointer_width = "32")]
const MAX_METADATA_SIZE: usize = CACHELINE_SIZE / 2;
#[cfg(target_pointer_width = "64")]
const MAX_METADATA_SIZE: usize = CACHELINE_SIZE;

#[derive(Debug, Clone)]
pub enum FrameError {
    IncorrectFrameType,
    OutOfFrames,
    Conflict,
}

#[derive(Clone, Copy)]
#[repr(C)]
pub struct FrameRange {
    pub start: FrameNumber,
    pub end: FrameNumber,
}

#[repr(C)]
#[cfg_attr(target_pointer_width = "32", repr(align(32)))]
#[cfg_attr(target_pointer_width = "64", repr(align(64)))]
pub struct Frame {
    refcount: FrameRc,
    data: SyncUnsafeCell<FrameData>,
    tag: AtomicU8,
}

pub union FrameData {
    pub unused: (),
    pub range: FrameRange,
    pub(super) buddy: ManuallyDrop<Buddy>,
    pub(super) slub: ManuallyDrop<Slub>,
    pub anonymous: ManuallyDrop<Anonymous>,
    pub assigned: ManuallyDrop<AssignedFixed>,
    pub page_table: ManuallyDrop<RwSpinlock<ArchPageTable>>,
}

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FrameTag {
    /// 未配置（比如为一组页中非首个页）
    Uninited = 0,
    HardwareReserved = 1,
    SystemReserved = 2,
    BadMemory = 3,
    /// `Free` 是初始化时使用的临时类型，分配器初始化完成后不应再出现该类型的页
    Free = 4,
    /// `Anonymous` 用于标识该组页已被分配，但没有标识类型
    Anonymous = 5,
    Buddy = 6,
    Slub = 7,
    /// 跨越单个物理页的大页中，使用头一个页存放元数据，其余页使用 `Tail` 标记
    Tail = 8,
    /// `AssignedFixed` 用于标识被分配给设备的固定页，这些页不会被内核使用或管理，但需要从 buddy 分配器中剔除
    AssignedFixed = 9,
    /// `PageTable` 用于标识页表结构占用的页，这些页由内核使用但不受 buddy 分配器管理
    PageTable = 10,
}

const _: () = assert!(size_of::<Frame>() <= MAX_METADATA_SIZE);

/// Buddy 分配器的虚拟地址存储位置
pub static FRAME_MANAGER: BuddyAllocator = BuddyAllocator::empty();

impl Frame {
    pub fn init_page_table_frame(frame_number: FrameNumber) {
        let frame = unsafe { Self::get_raw(frame_number).as_mut() };
        if frame.get_tag() == FrameTag::PageTable {
            return;
        }

        let page_table = ManuallyDrop::new(RwSpinlock::new());
        unsafe {
            frame.replace(FrameTag::PageTable, FrameData { page_table });
        }
    }

    pub fn get_tag(&self) -> FrameTag {
        unsafe { transmute(self.tag.load(Ordering::Acquire)) }
    }

    /// 修改 Frame 的 tag
    ///
    /// # Safety
    ///
    /// 调用者必须确保 Frame 当前的 tag 和 data 是匹配且有效的
    pub unsafe fn set_tag(&mut self, tag: FrameTag) {
        self.tag.store(unsafe { transmute(tag) }, Ordering::Release);
    }

    pub fn get_data(&self) -> &FrameData {
        unsafe { &*self.data.get() }
    }

    /// 获取 FrameData 的可变引用
    ///
    /// # Safety
    ///
    /// 调用者必须确保当前 FrameTag 与 FrameData 中的数据类型匹配且有效
    pub unsafe fn get_data_mut(&mut self) -> &mut FrameData {
        self.data.get_mut()
    }

    /// 替换 Frame 的 tag 和 data，自动释放旧数据
    ///
    /// # Safety
    ///
    /// 调用者必须确保 Frame 当前的 tag 和 data 是匹配且有效的
    pub unsafe fn replace(&mut self, tag: FrameTag, data: FrameData) {
        let _tag = self.get_tag();
        let _data = self.data.get_mut();
        unsafe {
            match _tag {
                FrameTag::Anonymous => {
                    ManuallyDrop::drop(&mut _data.anonymous);
                }
                FrameTag::AssignedFixed => {
                    ManuallyDrop::drop(&mut _data.assigned);
                }
                FrameTag::Buddy => {
                    ManuallyDrop::drop(&mut _data.buddy);
                }
                FrameTag::PageTable => {
                    ManuallyDrop::drop(&mut _data.page_table);
                }
                FrameTag::Free
                | FrameTag::SystemReserved
                | FrameTag::HardwareReserved
                | FrameTag::BadMemory
                | FrameTag::Tail => {}
                FrameTag::Slub => {
                    ManuallyDrop::drop(&mut _data.slub);
                }
                FrameTag::Uninited => {}
            }
        }

        *_data = data;
        unsafe { self.set_tag(tag) };
    }
}

const FRAMES: *mut Frame = VMEMMAP_BASE.as_usize() as *mut Frame;

/// 工具函数实现
impl Frame {
    pub fn get_tag_relaxed(frame_number: FrameNumber) -> FrameTag {
        assert!(frame_number.get() <= usize::MAX / ArchPageTable::PAGE_SIZE + 1);
        unsafe {
            transmute(
                Self::get_raw(frame_number)
                    .as_ref()
                    .tag
                    .load(Ordering::Relaxed),
            )
        }
    }

    #[inline(always)]
    pub fn get(frame_number: FrameNumber) -> Option<SharedFrames> {
        SharedFrames::new(Self::get_raw(frame_number))
    }

    pub fn get_raw(frame_number: FrameNumber) -> NonNull<Frame> {
        assert!(frame_number.get() <= usize::MAX / ArchPageTable::PAGE_SIZE + 1);
        let addr = unsafe { FRAMES.add(frame_number.get()) };
        assert!(
            addr.addr() < VMEMMAP_END.as_usize(),
            "vmemmap address out of range"
        );
        unsafe { NonNull::new_unchecked(addr as *mut Frame) }
    }

    #[inline(always)]
    pub fn from_addr(addr: PhysAddr) -> Option<SharedFrames> {
        let page_num = addr.to_frame_number();
        Frame::get(page_num)
    }

    pub fn start_addr(&self) -> PhysAddr {
        let addr = self as *const Frame as usize;
        assert!(addr >= VMEMMAP_BASE.as_usize());
        assert!(addr < VMEMMAP_END.as_usize());
        let frame_number = unsafe { (self as *const Frame).offset_from(FRAMES) } as usize;
        PhysAddr::new(frame_number * ArchPageTable::PAGE_SIZE)
    }

    /// 从子对象指针获取父对象指针
    ///
    /// # Safety
    ///
    /// 调用者必须确保 `child` 确实是 `Frame` 内部某个字段的指针
    pub unsafe fn from_child<T>(child: &mut T) -> &mut Frame {
        let addr = child as *mut T;
        assert!(addr.addr() >= VMEMMAP_BASE.as_usize());
        assert!(addr.addr() < VMEMMAP_END.as_usize());
        unsafe {
            addr.map_addr(|v| v & !(size_of::<Self>() - 1))
                .cast::<Self>()
                .as_mut()
                .unwrap()
        }
    }

    pub fn frame_number(&self) -> FrameNumber {
        let frame_number = unsafe { (self as *const Frame).offset_from(FRAMES) } as usize;
        FrameNumber::new(frame_number)
    }
}

pub const fn frame_count(size: usize) -> usize {
    size.div_ceil(ArchPageTable::PAGE_SIZE)
}

pub trait FrameAllocator {
    fn allocate(&self, zone_type: ZoneType, order: FrameOrder) -> Option<UniqueFrames>;
    fn deallocate(&self, page: &mut Frame) -> Result<(), FrameError>;
    /// 从 buddy 分配器中剔除指定物理内存区域（如 ioremap 需要映射的物理内存）
    ///
    /// 需要注意：`start` 须对齐到 `order`
    fn assign(&self, start: FrameNumber, order: FrameOrder) -> Result<UniqueFrames, FrameError>;
}

#[unsafe(export_name = "assign_frames")]
fn assign_frames_c(paddr: usize, count: usize) {
    let paddr = PhysAddr::new(paddr);
    if !paddr.is_page_aligned() {
        printk!(
            "assign_frames(): WARNING: paddr is not page aligned! paddr: {}",
            paddr.as_usize()
        );
    }

    let frame_number = paddr.to_frame_number();

    let mut start = frame_number;
    let end = start + count;
    while start < end {
        let order = FrameOrder::new(start.get().trailing_zeros() as u8);

        let _ = FRAME_MANAGER
            .assign(start, order)
            .map(|frame| mem::forget(frame))
            .inspect_err(|e| {
                printk!(
                    "Assign frame error! error:{:?}, paddr:{}, count:{}",
                    e,
                    paddr,
                    count
                )
            });

        start = start + order.to_count().get();
    }
}
