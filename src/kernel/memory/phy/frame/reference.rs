use core::{
    fmt::Write,
    ops::{Deref, DerefMut},
    ptr::NonNull,
    sync::atomic::{AtomicUsize, Ordering},
};

use crate::{
    ConsoleOutput,
    kernel::memory::phy::frame::{FRAME_MANAGER, Frame, FrameAllocator},
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
        FrameRef { frame: self.frame }
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
    unsafe {
        let count = frame.as_ref().refcount.release().unwrap();
        if count == 0 {
            let frame = frame.as_mut();

            FRAME_MANAGER
                .free(frame)
                .inspect_err(|e| {
                    let mut output = ConsoleOutput;
                    writeln!(
                        output,
                        "Warning: Failed to free frame ({}). Error {:?}",
                        frame.to_frame_number(),
                        e
                    )
                    .unwrap();
                })
                .unwrap();
        }
    }
}
