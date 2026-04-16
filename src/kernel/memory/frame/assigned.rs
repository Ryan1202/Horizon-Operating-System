use core::{
    mem::ManuallyDrop,
    sync::atomic::{AtomicUsize, Ordering},
};

use crate::kernel::memory::frame::{Frame, FrameData, FrameRange, FrameTag, buddy::FrameOrder};

pub struct AssignedFixed {
    mapcount: AtomicUsize,
    order: FrameOrder,
    original_tag: FrameTag,
    original_range: FrameRange,
}

impl AssignedFixed {
    pub const fn new(
        order: FrameOrder,
        original_tag: FrameTag,
        original_range: FrameRange,
    ) -> Self {
        Self {
            mapcount: AtomicUsize::new(0),
            order,
            original_tag,
            original_range,
        }
    }

    pub fn acquire(&self) {
        self.mapcount.fetch_add(1, Ordering::Relaxed);
    }

    pub fn release(&self) -> usize {
        self.mapcount.fetch_sub(1, Ordering::Relaxed)
    }

    pub fn get_order(&self) -> FrameOrder {
        self.order
    }

    pub fn get_original_tag(&self) -> FrameTag {
        self.original_tag
    }

    pub fn get_original_range(&self) -> FrameRange {
        self.original_range
    }

    pub fn replace_frame(self, frame: &mut Frame) {
        let assigned = ManuallyDrop::new(self);
        unsafe {
            frame.replace(FrameTag::AssignedFixed, FrameData { assigned });
        }
    }
}
