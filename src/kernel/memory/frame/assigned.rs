use core::{
    mem::ManuallyDrop,
    sync::atomic::{AtomicUsize, Ordering},
};

use crate::kernel::memory::frame::{Frame, FrameData, FrameTag, buddy::FrameOrder};

pub struct AssignedFixed {
    mapcount: AtomicUsize,
    order: FrameOrder,
    original_tag: FrameTag,
}

impl AssignedFixed {
    pub const fn new(order: FrameOrder, original_tag: FrameTag) -> Self {
        Self {
            mapcount: AtomicUsize::new(0),
            order,
            original_tag,
        }
    }

    pub fn order(&self) -> FrameOrder {
        self.order
    }

    pub fn mapcount(&self) -> usize {
        self.mapcount.load(Ordering::Relaxed)
    }

    pub fn get_original_tag(&self) -> FrameTag {
        self.original_tag
    }

    pub fn replace_frame(self, frame: &mut Frame) {
        let assigned = ManuallyDrop::new(self);
        unsafe {
            frame.replace(FrameTag::AssignedFixed, FrameData { assigned });
        }
    }
}
