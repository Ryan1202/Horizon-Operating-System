use core::{
    fmt::Write,
    mem,
    ops::{Deref, DerefMut},
    ptr::NonNull,
    sync::atomic::{AtomicUsize, Ordering},
};

use crate::{
    ConsoleOutput,
    kernel::memory::frame::{FRAME_MANAGER, Frame, FrameAllocator},
};

#[repr(transparent)]
pub struct FrameRc {
    count: AtomicUsize,
}

impl FrameRc {
    const EXCLUSIVE: usize = usize::MAX;
    const MAX: usize = Self::EXCLUSIVE - 1;

    pub const fn new() -> Self {
        Self {
            count: AtomicUsize::new(0),
        }
    }

    fn update(&self, f: impl Fn(usize) -> Option<usize>) -> Option<usize> {
        self.count
            .fetch_update(Ordering::Release, Ordering::Acquire, f)
            .ok()
    }

    pub fn acquire(&self) -> Option<()> {
        self.update(|value| (value < Self::MAX).then_some(value + 1))
            .map(|_| ())
    }

    pub fn release(&self) -> Option<usize> {
        self.update(|value| {
            if value == Self::EXCLUSIVE {
                Some(0)
            } else if value > 0 {
                Some(value - 1)
            } else {
                None
            }
        })
    }

    pub fn acquire_exclusive(&self) -> Option<()> {
        self.update(|value| (value == 0).then_some(Self::EXCLUSIVE))
            .map(|_| ())
    }

    pub fn get(&self) -> usize {
        self.count.load(Ordering::Acquire)
    }

    pub fn is_exclusive(&self) -> bool {
        self.get() == Self::EXCLUSIVE
    }
}

impl Default for FrameRc {
    fn default() -> Self {
        Self::new()
    }
}

pub struct FrameRef {
    frame: NonNull<Frame>,
}

impl FrameRef {
    pub fn new(frame: NonNull<Frame>) -> Option<Self> {
        unsafe { frame.as_ref().refcount.acquire()? };
        Some(FrameRef { frame })
    }

    pub unsafe fn from_raw(frame: NonNull<Frame>) -> Option<Self> {
        let refcount = unsafe { frame.as_ref().refcount.get() };
        if 0 < refcount && refcount <= FrameRc::MAX {
            Some(FrameRef { frame })
        } else {
            None
        }
    }
}

impl Deref for FrameRef {
    type Target = Frame;

    fn deref(&self) -> &Self::Target {
        unsafe { self.frame.as_ref() }
    }
}

impl Drop for FrameRef {
    fn drop(&mut self) {
        auto_free(self.frame);
    }
}

pub struct FrameMut {
    frame: NonNull<Frame>,
}

impl FrameMut {
    pub fn new(frame: NonNull<Frame>) -> Option<Self> {
        unsafe { frame.as_ref().refcount.acquire_exclusive()? };
        Some(FrameMut { frame })
    }

    pub unsafe fn try_from_raw(frame: NonNull<Frame>) -> Option<Self> {
        if unsafe { frame.as_ref().refcount.is_exclusive() } {
            Some(FrameMut { frame })
        } else {
            None
        }
    }

    pub fn downgrade(self) -> FrameRef {
        unsafe {
            self.frame
                .as_ref()
                .refcount
                .count
                .store(1, Ordering::Relaxed)
        };

        let frame_ref = FrameRef { frame: self.frame };
        mem::forget(self);

        frame_ref
    }
}

impl Deref for FrameMut {
    type Target = Frame;

    fn deref(&self) -> &Self::Target {
        unsafe { self.frame.as_ref() }
    }
}

impl DerefMut for FrameMut {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { self.frame.as_mut() }
    }
}

impl Drop for FrameMut {
    fn drop(&mut self) {
        auto_free(self.frame);
    }
}

fn auto_free(mut frame: NonNull<Frame>) {
    // 1. 释放引用计数
    let count = unsafe { frame.as_ref().refcount.release() };

    match count {
        Some(0) => {
            // 最后一个引用被释放——归还给分配器
            let frame_ref = unsafe { frame.as_mut() };
            let frame_number = frame_ref.to_frame_number();

            if let Err(e) = FRAME_MANAGER.free(frame_ref) {
                // 不 panic，仅记录错误。
                // 内存会泄漏，但系统继续运行。
                // 比 panic 导致整个系统停机要好。
                let mut output = ConsoleOutput;
                let _ = writeln!(
                    output,
                    "ERROR: failed to free frame {}: {:?} (memory leaked)",
                    frame_number, e
                );
            }
        }
        Some(_) => {
            // 还有其他引用存在，不释放
        }
        None => {
            // double-free 或引用计数已经为 0
            // 记录错误但不 panic
            let mut output = ConsoleOutput;
            let _ = writeln!(
                output,
                "ERROR: double-free detected for frame at {:p}",
                frame.as_ptr()
            );
        }
    }
}
