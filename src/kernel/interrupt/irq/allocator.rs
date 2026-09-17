use super::{IrqError, IrqNumber, table::IRQ_DESCRIPTORS};
use crate::lib::rust::{bitset::BitSet, spinlock::Spinlock};
use core::{num::NonZeroUsize, range::Range};

pub(super) const MAX_IRQS: usize = 4096;
type IrqBits = BitSet<[usize; MAX_IRQS / usize::BITS as usize]>;

static ALLOCATOR: Spinlock<IrqBits> = Spinlock::new(BitSet::zeroed(MAX_IRQS));

/// 预留编号范围，发布后由 descriptor 表占用，未发布的编号在 drop 时归还
pub struct IrqReservation {
    number: Range<usize>,
}

impl IrqReservation {
    pub fn new(count: NonZeroUsize) -> Result<Self, IrqError> {
        if count.get() > MAX_IRQS {
            return Err(IrqError::InvalidArgument);
        }

        let base = ALLOCATOR
            .lock()
            .allocate(0, count, 1)
            .ok_or(IrqError::OutOfIrq)?;

        Ok(Self {
            number: (base..base + count.get()).into(),
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

    pub fn free(irq: IrqNumber) {
        ALLOCATOR.lock().clear(irq.get());
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
            // 已发布 descriptor 的编号由表占用，reservation 只归还尚未发布的部分
            if IRQ_DESCRIPTORS.lookup(irq).is_none() {
                Self::free(irq);
            }
        }
    }
}
