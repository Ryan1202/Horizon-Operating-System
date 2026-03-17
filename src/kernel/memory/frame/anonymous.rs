use core::sync::atomic::{AtomicUsize, Ordering};

use crate::kernel::memory::frame::buddy::FrameOrder;

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

    pub fn acquire(&self) {
        self.mapcount.fetch_add(1, Ordering::Relaxed);
    }

    pub fn release(&self) -> usize {
        self.mapcount.fetch_sub(1, Ordering::Relaxed)
    }

    pub fn get_order(&self) -> FrameOrder {
        self.order
    }
}
