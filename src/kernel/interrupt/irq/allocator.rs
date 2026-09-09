//! 编号预留与发布分离

use super::{IrqDescriptor, IrqError, IrqNumber, table::IRQ_DESCRIPTORS};
use crate::lib::rust::{bitset::BitSet, spinlock::Spinlock};
use core::{num::NonZeroUsize, range::Range};

pub(super) const MAX_IRQS: usize = 4096;
type IrqBits = BitSet<[usize; MAX_IRQS / usize::BITS as usize]>;

static ALLOCATOR: Spinlock<IrqBits> = Spinlock::new(BitSet::zeroed(MAX_IRQS));

/// Drop 归还未发布部分；发布成功的编号继续由常驻指针表占用
pub struct IrqReservation {
    number: Range<usize>,
}

impl IrqReservation {
    pub fn new(count: usize) -> Result<Self, IrqError> {
        if count == 0 || count > MAX_IRQS {
            return Err(IrqError::InvalidArgument);
        }

        let base = ALLOCATOR
            .lock()
            .allocate(0, NonZeroUsize::new(count).unwrap(), 1)
            .ok_or(IrqError::OutOfIrq)?;

        Ok(Self {
            number: (base..base + count).into(),
        })
    }

    pub fn reserve(range: Range<usize>) -> Result<Self, IrqError> {
        if range.start >= range.end || range.end > MAX_IRQS {
            return Err(IrqError::InvalidArgument);
        }

        let count = NonZeroUsize::new(range.end - range.start).unwrap();

        if !ALLOCATOR.lock().assign(range.start, count) {
            return Err(IrqError::Busy);
        }

        Ok(Self { number: range })
    }

    /// 只有持有编号预留的调用者才能发布；失败会正常析构传入对象。
    pub fn publish(&mut self, descriptor: IrqDescriptor) -> Result<(), IrqError> {
        let irq = descriptor.irq.get();
        if irq < self.number.start || irq >= self.number.end {
            return Err(IrqError::InvalidIrqNumber(irq));
        }

        IRQ_DESCRIPTORS.publish(descriptor)
    }

    pub fn iter(&self) -> impl Iterator<Item = IrqNumber> + use<> {
        (self.number.start..self.number.end).map(|i| IrqNumber(i as u32))
    }

    pub fn get(&self, index: usize) -> Option<IrqNumber> {
        if index >= self.number.end - self.number.start {
            None
        } else {
            Some(IrqNumber((self.number.start + index) as u32))
        }
    }
}

impl Drop for IrqReservation {
    fn drop(&mut self) {
        for irq in self.iter() {
            // 发布后不撤下，且本 reservation 不可能同时发布；查表后再取分配器锁。
            if IRQ_DESCRIPTORS.lookup(irq).is_none() {
                assert!(
                    ALLOCATOR.lock().try_clear(irq.get()).is_some(),
                    "IRQ reservation lost"
                );
            }
        }
    }
}
