use core::marker::PhantomData;

use crate::kernel::interrupt::irq::{Domain, allocator::MAX_IRQS};

/// Core 分配的虚拟编号；复制编号不延长 mapping 生命周期。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IrqNumber(pub(super) u32);

pub static INVALID_IRQ: IrqNumber = IrqNumber(MAX_IRQS as u32);

impl IrqNumber {
    pub const fn get(&self) -> usize {
        self.0 as usize
    }
}

/// 显式擦除 domain 类型后的硬件编号
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RawIrq(u32);
impl RawIrq {
    pub const fn new(number: u32) -> Self {
        Self(number)
    }

    pub const fn get(self) -> u32 {
        self.0
    }
}

pub struct HardwareIrq<D: Domain> {
    number: RawIrq,
    _marker: PhantomData<D>,
}

impl<D: Domain> HardwareIrq<D> {
    pub const fn new(number: RawIrq) -> Self {
        Self {
            number,
            _marker: PhantomData,
        }
    }

    pub const fn get(self) -> u32 {
        self.number.get()
    }

    pub const fn into_raw(self) -> RawIrq {
        self.number
    }
}

impl<D: Sized + Domain> Clone for HardwareIrq<D> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<D: Sized + Domain> Copy for HardwareIrq<D> {}
