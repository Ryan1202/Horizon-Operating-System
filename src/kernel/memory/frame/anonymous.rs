use core::{
    mem::ManuallyDrop,
    sync::atomic::{AtomicUsize, Ordering},
};

use crate::kernel::memory::frame::{Frame, FrameData, FrameTag, buddy::FrameOrder};

pub struct Anonymous {
    mapcount: AtomicUsize,
    order: FrameOrder,
}

impl Anonymous {
    pub const fn new(order: FrameOrder) -> Self {
        Self {
            mapcount: AtomicUsize::new(0),
            order,
        }
    }

    pub fn order(&self) -> FrameOrder {
        self.order
    }

    pub fn mapcount(&self) -> usize {
        self.mapcount.load(Ordering::Relaxed)
    }

    pub fn replace_frame(self, frame: &mut Frame) {
        let anonymous = ManuallyDrop::new(self);
        unsafe {
            frame.replace(FrameTag::Anonymous, FrameData { anonymous });
        }
    }
}
