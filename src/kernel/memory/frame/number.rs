use core::{
    fmt::Display,
    ops::{Add, Sub},
    ptr::NonNull,
};

use crate::kernel::memory::frame::{Frame, buddy::FrameOrder};

#[repr(transparent)]
#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
pub struct FrameNumber(usize);

impl FrameNumber {
    pub const fn new(num: usize) -> Self {
        FrameNumber(num)
    }

    pub const fn get(&self) -> usize {
        self.0
    }

    pub fn from_frame(frame: NonNull<Frame>) -> Self {
        let frame_number = unsafe { frame.as_ref() }.to_frame_number();
        FrameNumber(frame_number.0)
    }

    pub const fn count_from(self, other: FrameNumber) -> usize {
        self.0.abs_diff(other.0) + 1
    }

    pub const fn align_down(self, order: FrameOrder) -> FrameNumber {
        let mask = (1 << order.get()) - 1;
        FrameNumber(self.0 & !mask)
    }
}

impl Display for FrameNumber {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "FrameNumber {}", self.0)
    }
}

impl Add<usize> for FrameNumber {
    type Output = FrameNumber;

    fn add(self, rhs: usize) -> Self::Output {
        FrameNumber(self.0 + rhs)
    }
}

impl Sub<usize> for FrameNumber {
    type Output = FrameNumber;

    fn sub(self, rhs: usize) -> Self::Output {
        FrameNumber(self.0 - rhs)
    }
}
