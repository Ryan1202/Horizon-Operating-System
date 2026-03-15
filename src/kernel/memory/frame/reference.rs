use core::{
    mem::{self, ManuallyDrop},
    ops::{Deref, DerefMut},
    ptr::NonNull,
    sync::atomic::{AtomicUsize, Ordering},
};

use crate::kernel::memory::frame::{
    FRAME_MANAGER, Frame, FrameAllocator, FrameData, FrameNumber, FrameRange, FrameTag,
    buddy::FrameOrder,
};

#[repr(transparent)]
pub struct FrameRc {
    count: AtomicUsize,
}

impl FrameRc {
    const EXCLUSIVE: usize = 0;

    pub(super) const fn new() -> Self {
        Self {
            count: AtomicUsize::new(Self::EXCLUSIVE), // 初始引用计数为 0，视作被 Buddy 独占
        }
    }

    fn update(&self, f: impl Fn(usize) -> Option<usize>) -> Option<usize> {
        self.count
            .fetch_update(Ordering::Release, Ordering::Acquire, f)
            .ok()
    }

    fn acquire(&self) -> Option<()> {
        self.update(|value| (value != Self::EXCLUSIVE).then_some(value + 1))
            .map(|_| ())
    }

    fn acquire_exclusive(&self, frame: &Frame) -> Option<()> {
        self.is_exclusive(frame).then_some(())
    }

    fn release(&self) -> Option<usize> {
        self.update(|value| {
            if value == Self::EXCLUSIVE {
                Some(value) // 已经是独占状态，不修改
            } else if value != Self::EXCLUSIVE + 1 {
                // 防止释放时意外落入独占状态
                Some(value - 1)
            } else {
                None
            }
        })
    }

    pub fn get(&self) -> usize {
        self.count.load(Ordering::Relaxed)
    }

    pub fn is_exclusive(&self, frame: &Frame) -> bool {
        self.get() == Self::EXCLUSIVE && !matches!(frame.get_tag(), FrameTag::Buddy)
    }
}

impl Default for FrameRc {
    fn default() -> Self {
        Self::new()
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
            order: FrameOrder::new(0),
        })
    }

    /// 尝试从原始指针创建 `SharedFrames` 而不增加引用计数
    ///
    /// # Safety
    ///
    /// 调用者必须确保该帧当前没有其他引用，否则可能导致数据竞争、释放后使用等未定义行为
    pub unsafe fn from_raw(frame: NonNull<Frame>, order: FrameOrder) -> Option<Self> {
        let refcount = unsafe { frame.as_ref().refcount.get() };
        if refcount != FrameRc::EXCLUSIVE {
            Some(SharedFrames { frame, order })
        } else {
            None
        }
    }
}

impl Deref for SharedFrames {
    type Target = Frame;

    fn deref(&self) -> &Self::Target {
        unsafe { self.frame.as_ref() }
    }
}

impl Drop for SharedFrames {
    fn drop(&mut self) {
        auto_free(self.frame);
    }
}

pub struct UniqueFrames {
    start: NonNull<Frame>,
    order: FrameOrder,
}

impl UniqueFrames {
    /// 尝试从原始指针创建 `UniqueFrames` 而不增加引用计数
    ///
    /// # Safety
    ///
    /// 调用者必须确保该帧当前没有其他引用，否则可能导致数据竞争和未定义行为
    pub unsafe fn try_from_raw(mut frame: NonNull<Frame>) -> Option<Self> {
        let frame_ref = unsafe { frame.as_mut() };
        if frame_ref.refcount.is_exclusive(frame_ref) {
            Some(UniqueFrames {
                start: frame,
                order: FrameOrder::new(0),
            })
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
        debug_assert_eq!(frame_ref.refcount.get(), FrameRc::EXCLUSIVE);

        match frame_ref.get_tag() {
            FrameTag::Buddy | FrameTag::Free | FrameTag::Unavailable => {
                Some(ManuallyDrop::new(UniqueFrames { start, order }))
            }
            FrameTag::Tail => {
                let range = unsafe { frame_ref.get_data().range };

                let head = range.start;
                let head_frame = Frame::get_raw(head);
                let head_ref = unsafe { head_frame.as_ref() };
                if matches!(
                    head_ref.get_tag(),
                    FrameTag::Buddy | FrameTag::Free | FrameTag::Unavailable
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

        let next_frame_number = self.to_frame_number() + (1 << new_order.get());
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
    ) -> Result<(), (ManuallyDrop<Self>, ManuallyDrop<Self>)>
    where
        A: FrameAllocator,
        F: FnOnce(&A, ManuallyDrop<Self>, ManuallyDrop<Self>),
    {
        let low_ = low.deref_mut();
        let high_ = high.deref_mut();

        if low_.order != high_.order || low_.get_tag() != high_.get_tag() {
            return Err((low, high));
        }

        let expected = low_.to_frame_number() + (1 << low_.order.get());
        if expected != high_.to_frame_number() {
            return Err((low, high));
        }

        f(allocator, low, high);
        Ok(())
    }

    pub(super) fn set_tail_frames(&mut self) {
        let count = self.order.to_count().get();

        let start = FrameNumber::from_frame(self.start);
        let end = start + count;
        let range = FrameRange { start, end };

        unsafe {
            for i in 1..count {
                let frame = Frame::get_raw(start + i).as_mut();

                frame.replace(
                    FrameTag::Tail,
                    FrameData {
                        range: ManuallyDrop::new(range),
                    },
                );
            }
        }
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
        auto_free(self.start);
    }
}

fn auto_free(mut frame: NonNull<Frame>) {
    // 释放引用计数
    let frame_ref = unsafe { frame.as_ref() };
    let count = frame_ref.refcount.release();
    if frame_ref.mapcount.load(Ordering::Relaxed) > 0 {
        // 如果还有映射存在，不立即释放帧资源，等待最后一个映射解除时再释放
        return;
    }

    match count {
        Some(1) => {
            frame_ref
                .refcount
                .count
                .store(FrameRc::EXCLUSIVE, Ordering::Relaxed);
            last_free(unsafe { frame.as_mut() });
        }
        Some(0) if let FrameTag::Buddy = frame_ref.get_tag() => {
            last_free(unsafe { frame.as_mut() });
        }
        Some(_) => {
            // 还有其他引用存在，不释放
        }
        None => {
            // double-free 或引用计数已经为 0
            // 记录错误但不 panic
            printk!(
                "ERROR: double-free detected for frame {}, addr = {:#x} (memory leaked)",
                unsafe { frame.as_ref().to_frame_number() },
                frame.addr()
            );
        }
    }
}

fn last_free(frame: &mut Frame) {
    // 最后一个引用被释放——归还给分配器
    let frame_number = frame.to_frame_number();

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
