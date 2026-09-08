//! 编号预留与指针发布相互独立。
//!
//! 指针表的 None 不代表编号可复用；位图在 descriptor 完全释放后才清零。

use super::{IrqError, IrqNumber};
use crate::lib::rust::{bitmap::Bitmap, spinlock::Spinlock};
use core::{alloc::Layout, num::NonZeroUsize, range::Range};

pub(super) const MAX_IRQS: usize = 4096;

static ALLOCATOR: Spinlock<Option<Bitmap<1>>> = Spinlock::new(None);

/// 构造失败自动归还编号；成功发布后由最后一个租约归还。
pub struct IrqReservation {
    number: Range<IrqNumber>,
}

impl IrqReservation {
    pub fn new(count: usize) -> Result<Self, IrqError> {
        let mut guard = ALLOCATOR.lock();
        let allocator = guard.get_or_try_insert_with(|| Bitmap::try_new(MAX_IRQS as u32))?;

        let base = allocator.allocate(Layout::from_size_align(count, 1).unwrap())?;

        Ok(Self {
            number: (IrqNumber(base as u32)..IrqNumber(base as u32 + count as u32)).into(),
        })
    }

    pub fn reserve(range: Range<usize>) -> Result<Self, IrqError> {
        let mut guard = ALLOCATOR.lock();
        let allocator = guard.get_or_try_insert_with(|| Bitmap::try_new(MAX_IRQS as u32))?;

        let start = range.start;
        let count = (range.end - range.start) as usize;
        allocator.assign(start, NonZeroUsize::new(count).unwrap())?;

        let number = (IrqNumber(start as u32)..IrqNumber((start + count) as u32)).into();
        Ok(Self { number })
    }

    pub fn iter(&self) -> impl Iterator<Item = IrqNumber> {
        (self.number.start.get()..self.number.end.get())
            .into_iter()
            .map(|i| IrqNumber(i as u32))
    }

    pub fn get(&self, index: usize) -> Option<IrqNumber> {
        if index >= self.number.end.get() - self.number.start.get() {
            None
        } else {
            Some(IrqNumber((self.number.start.get() + index) as u32))
        }
    }
}

impl Drop for IrqReservation {
    fn drop(&mut self) {
        let mut guard = ALLOCATOR.lock();
        let allocator = guard
            .as_mut()
            .expect("Failed to drop IRQ number: Allocator not exist!");

        allocator.deallocate(self.number.start.get() as usize);
    }
}
