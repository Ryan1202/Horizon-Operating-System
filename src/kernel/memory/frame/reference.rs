use core::{
    mem::{self, ManuallyDrop},
    ops::{Deref, DerefMut},
    ptr::NonNull,
    sync::atomic::{AtomicUsize, Ordering},
};

use crate::{
    arch::ArchPageTable,
    kernel::memory::{
        KLINEAR_BASE,
        frame::{
            FRAME_MANAGER, Frame, FrameAllocator, FrameData, FrameNumber, FrameRange, FrameTag,
            anonymous::Anonymous, assigned::AssignedFixed, buddy::FrameOrder,
        },
        page::{
            PageTable, current_root_pt, linear_table_ptr,
            lock::{NormalPtLock, PageLock, PtPage},
            table::drop_table,
        },
    },
};

#[repr(transparent)]
pub struct FrameRc {
    count: AtomicUsize,
}

impl FrameRc {
    const EXCLUSIVE: usize = 0;

    fn update(&self, f: impl Fn(usize) -> Option<usize>) -> Option<usize> {
        self.count
            .fetch_update(Ordering::Release, Ordering::Acquire, f)
            .ok()
    }

    pub fn acquire(&self) -> Option<()> {
        self.update(|value| (value != Self::EXCLUSIVE).then_some(value + 1))
            .map(|_| ())
    }

    pub fn release(&self) -> Option<usize> {
        self.update(|value| {
            if value == Self::EXCLUSIVE {
                Some(value) // 已经是独占状态，不修改
            } else if value == 1 {
                None // 防止从 1 变成 0 导致误判为独占状态
            } else {
                Some(value - 1)
            }
        })
    }

    pub fn count(&self) -> usize {
        self.count.load(Ordering::Relaxed)
    }

    pub fn is_unique(&self, frame: &Frame) -> bool {
        self.count() == Self::EXCLUSIVE && !matches!(frame.get_tag(), FrameTag::Buddy)
    }

    pub fn try_upgrade(&self) -> Option<()> {
        self.count
            .compare_exchange(1, Self::EXCLUSIVE, Ordering::AcqRel, Ordering::Acquire)
            .ok()
            .map(|_| ())
    }
}

pub struct SharedFrames {
    frame: NonNull<Frame>,
    order: FrameOrder,
}

impl SharedFrames {
    pub fn new(frame: NonNull<Frame>) -> Option<Self> {
        unsafe { frame.as_ref().refcount.acquire()? };
        Some(SharedFrames {
            frame,
            order: frame_order(frame),
        })
    }

    /// 尝试从指针创建 `SharedFrames` 而不增加引用计数
    ///
    /// 如果为独占引用则返回None
    ///
    /// # Safety
    ///
    /// 调用者必须确保该帧当前没有其他引用，否则可能导致数据竞争、释放后使用等未定义行为
    pub unsafe fn from_raw(frame: NonNull<Frame>) -> Option<Self> {
        let refcount = unsafe { frame.as_ref().refcount.count() };
        if refcount != FrameRc::EXCLUSIVE {
            let order = frame_order(frame);
            Some(SharedFrames { frame, order })
        } else {
            None
        }
    }

    pub fn order(&self) -> FrameOrder {
        self.order
    }

    pub fn try_upgrade(self) -> Option<UniqueFrames> {
        let mut shared = ManuallyDrop::new(self);
        let frame = shared.frame;
        if unsafe { frame.as_ref().refcount.try_upgrade() }.is_none() {
            unsafe { ManuallyDrop::drop(&mut shared) };
            return None;
        }

        Some(UniqueFrames {
            start: frame,
            order: shared.order,
        })
    }
}

impl Deref for SharedFrames {
    type Target = Frame;

    fn deref(&self) -> &Self::Target {
        unsafe { self.frame.as_ref() }
    }
}

impl Clone for SharedFrames {
    fn clone(&self) -> Self {
        unsafe {
            self.frame
                .as_ref()
                .refcount
                .acquire()
                .expect("clone shared frame from unique state");
        }
        SharedFrames {
            frame: self.frame,
            order: self.order,
        }
    }
}

impl Drop for SharedFrames {
    fn drop(&mut self) {
        let frame = unsafe { self.frame.as_ref() };

        // 多于 1 个引用直接减少引用计数即可
        if frame.refcount.release().is_some() {
            return;
        }

        // 如果是页表页的最后一个引用需要注意处理
        if let FrameTag::PageTable = frame.get_tag() {
            let root = current_root_pt();
            let root = PtPage::from_ptr(root as *const ArchPageTable);

            let page = KLINEAR_BASE.to_page_number() + self.frame_number().get();

            // 在页表锁释放前还不能修改引用计数，不然页表锁获取不了引用
            if let Some((mut lock, level)) =
                PageLock::<ArchPageTable, NormalPtLock>::lock_page(root, page, linear_table_ptr)
                    .and_then(|mut x| {
                        let (_, level) = x.pop()?;
                        Some((x, level))
                    })
            {
                let index = ArchPageTable::entry_index(page, level);
                unsafe { drop_table(&mut lock, index) };
            } else {
                printk!("Failed to lock page for frame {}", self.frame_number());
            }
        }

        let frame = unsafe { self.frame.as_mut() };
        frame.refcount.release();

        // 修改回默认标签并替换数据以便后续正确释放
        Anonymous::new(self.order).replace_frame(frame);

        // 走 UniqueFrames 的释放路径
        drop(UniqueFrames {
            start: self.frame,
            order: self.order,
        });
    }
}

pub struct UniqueFrames {
    start: NonNull<Frame>,
    order: FrameOrder,
}

impl UniqueFrames {
    /// 尝试从原始指针创建 `UniqueFrames` 而不验证引用计数
    ///
    /// # Safety
    ///
    /// 调用者必须确保该帧当前没有其他引用，否则可能导致数据竞争和未定义行为
    pub unsafe fn try_from_raw(mut frame: NonNull<Frame>) -> Option<Self> {
        let frame_ref = unsafe { frame.as_mut() };
        if frame_ref.refcount.is_unique(frame_ref) {
            let order = frame_order(frame);
            let start = match frame_ref.get_tag() {
                FrameTag::Tail => {
                    let range = unsafe { frame_ref.get_data().range };
                    Frame::get_raw(range.start)
                }
                _ => frame,
            };
            Some(UniqueFrames { start, order })
        } else {
            None
        }
    }

    /// 从信任的分配器获取独占访问权限，成功时返回 `UniqueFrames`
    ///
    /// 由分配器指定帧 order
    pub(super) fn from_allocator<A: FrameAllocator>(
        frame: NonNull<Frame>,
        order: FrameOrder,
        _allocator: &A,
    ) -> Option<ManuallyDrop<Self>> {
        // 根据 order 对齐帧地址
        let frame_number = FrameNumber::from_frame(frame);
        let start = frame_number.align_down(order);
        let start = Frame::get_raw(start);

        let frame_ref = unsafe { frame.as_ref() };
        debug_assert_eq!(frame_ref.refcount.count(), FrameRc::EXCLUSIVE);

        match frame_ref.get_tag() {
            FrameTag::Buddy | FrameTag::Free | FrameTag::Uninited | FrameTag::AssignedFixed => {
                Some(ManuallyDrop::new(UniqueFrames { start, order }))
            }
            FrameTag::HardwareReserved | FrameTag::SystemReserved | FrameTag::BadMemory => {
                Some(ManuallyDrop::new(UniqueFrames { start, order }))
            }
            FrameTag::Tail => {
                let range = unsafe { frame_ref.get_data().range };

                let head = unsafe { Frame::get_raw(range.start).as_ref() };
                if matches!(
                    head.get_tag(),
                    FrameTag::Buddy
                        | FrameTag::Free
                        | FrameTag::Uninited
                        | FrameTag::AssignedFixed
                        | FrameTag::HardwareReserved
                        | FrameTag::SystemReserved
                        | FrameTag::BadMemory
                ) && frame_number + order.to_count().get() <= range.end
                {
                    Some(ManuallyDrop::new(UniqueFrames { start, order }))
                } else {
                    None
                }
            }
            _ => None,
        }
    }

    pub fn downgrade(self) -> SharedFrames {
        self.refcount.count.store(1, Ordering::Relaxed);

        let frame_ref = SharedFrames {
            frame: self.start,
            order: self.order,
        };
        mem::forget(self);

        frame_ref
    }

    pub fn split(&mut self) -> ManuallyDrop<Self> {
        debug_assert!(self.order.get() > 0, "Cannot split a frame of order 0");

        let new_order = self.order - 1;
        self.order = new_order;

        let next_frame_number = self.frame_number() + (1 << new_order.get());
        let next_frame = Frame::get_raw(next_frame_number);

        ManuallyDrop::new(Self {
            start: next_frame,
            order: new_order,
        })
    }

    pub fn merge<A, F>(
        mut low: ManuallyDrop<Self>,
        mut high: ManuallyDrop<Self>,
        allocator: &A,
        f: F,
    ) -> Result<ManuallyDrop<Self>, (ManuallyDrop<Self>, ManuallyDrop<Self>)>
    where
        A: FrameAllocator,
        F: FnOnce(&A, &mut Self, ManuallyDrop<Self>),
    {
        let low_ = low.deref_mut();
        let high_ = high.deref_mut();

        if low_.order != high_.order || low_.get_tag() != high_.get_tag() {
            return Err((low, high));
        }

        let expected = low_.frame_number() + (1 << low_.order.get());
        if expected != high_.frame_number() {
            return Err((low, high));
        }

        f(allocator, low_, high);
        Ok(low)
    }

    pub(super) fn set_tail_frames(&mut self) {
        let count = self.order.to_count().get();

        let start = FrameNumber::from_frame(self.start);
        let end = start + count;
        let range = FrameRange { start, end };

        unsafe {
            for i in 1..count {
                let frame = Frame::get_raw(start + i).as_mut();

                frame.replace(FrameTag::Tail, FrameData { range });
            }
        }
    }

    pub fn order(&self) -> FrameOrder {
        self.order
    }
}

impl Deref for UniqueFrames {
    type Target = Frame;

    fn deref(&self) -> &Self::Target {
        unsafe { self.start.as_ref() }
    }
}

impl DerefMut for UniqueFrames {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { self.start.as_mut() }
    }
}

impl Drop for UniqueFrames {
    fn drop(&mut self) {
        match self.get_tag() {
            FrameTag::AssignedFixed => restore_assigned(self),
            _ => free_last(self),
        }
    }
}

fn restore_assigned(frame: &mut UniqueFrames) {
    let start = frame.frame_number();
    let (order, original_tag) = unsafe {
        let assigned: &AssignedFixed = &frame.get_data().assigned;
        (assigned.order(), assigned.get_original_tag())
    };

    let count = order.to_count().get();
    unsafe {
        frame.set_tail_frames();

        match original_tag {
            // 数据页则释放
            FrameTag::Anonymous => {
                free_last(frame);
            }

            // 标记类页恢复
            FrameTag::HardwareReserved
            | FrameTag::SystemReserved
            | FrameTag::BadMemory
            | FrameTag::Free => {
                let end = start + count;
                let range = FrameRange { start, end };
                frame.replace(original_tag, FrameData { range });
            }

            // 其他类型不应出现，直接 panic
            _ => {
                unreachable!(
                    "Invalid original tag {:?} for assigned frame {}",
                    original_tag, start
                );
            }
        }
    }
}

fn free_last(frame: &mut Frame) {
    // 最后一个引用被释放——归还给分配器
    let frame_number = frame.frame_number();

    if let Err(e) = FRAME_MANAGER.deallocate(frame) {
        // 不 panic，仅记录错误。
        // 内存会泄漏，但系统继续运行。
        // 比 panic 导致整个系统停机要好。
        printk!(
            "ERROR: failed to free frame {}: {:?} (memory leaked)",
            frame_number,
            e
        );
    }
}

fn frame_order(frame: NonNull<Frame>) -> FrameOrder {
    unsafe {
        let frame_ref = frame.as_ref();
        let data = frame_ref.get_data();
        match frame_ref.get_tag() {
            FrameTag::Anonymous => data.anonymous.order(),
            FrameTag::AssignedFixed => data.assigned.order(),
            FrameTag::Buddy => data.buddy.order(),
            FrameTag::Tail => {
                let range = data.range;
                FrameOrder::from_count(range.end.count_from(range.start))
            }
            _ => FrameOrder::new(0),
        }
    }
}
