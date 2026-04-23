use core::mem::{self, ManuallyDrop};

use crate::{
    arch::ArchPageTable,
    kernel::memory::frame::{Frame, FrameData, FrameNumber, FrameTag, reference::UniqueFrames},
    lib::rust::spinlock::RwSpinlock,
};

/// 页表页的 Frame 元数据
pub struct PageTable<T> {
    pub lock: RwSpinlock<T>,
}

impl<T> PageTable<T> {
    pub fn new() -> Self {
        Self {
            lock: RwSpinlock::new(),
        }
    }
}

impl PageTable<ArchPageTable> {
    pub fn early_init(frame_number: FrameNumber) {
        if Frame::get_tag_relaxed(frame_number) == FrameTag::PageTable {
            return;
        }

        let frame = Frame::get_raw(frame_number);
        let mut frame = unsafe { UniqueFrames::try_from_raw(frame).unwrap() };

        Self::new().replace_frame(&mut frame);
        let frame = frame.downgrade();

        // 通过 forget 增加一个额外的计数使其无法自动释放
        mem::forget(frame);
    }

    pub fn replace_frame(self, frame: &mut Frame) {
        let page_table = ManuallyDrop::new(self);
        unsafe {
            frame.replace(FrameTag::PageTable, FrameData { page_table });
        }
    }
}
